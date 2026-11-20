#include "Mixer.h"

#include <avrt.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace novaon {

namespace {

constexpr size_t kMaxBlock = 1024;   // maior bloco processado por vez
constexpr LONGLONG kTickHns = 20000; // acorda a cada 2 ms

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

void updatePeak(std::atomic<float>& peak, float v) {
    float cur = peak.load(std::memory_order_relaxed);
    while (v > cur && !peak.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

} // namespace

Mixer::Mixer(const EngineConfig& cfg)
    : cfg_(cfg), targetFrames_(size_t(cfg.latencyMs) * kBusSampleRate / 1000) {
    // Buffer de cada fonte: 500 ms, bem acima da folga, para absorver engasgos.
    for (const auto& s : cfg_.sources)
        sources_.push_back(std::make_unique<WasapiSource>(s, kBusSampleRate / 2));
    sourceBlocks_.assign(sources_.size(), std::vector<float>(kMaxBlock * 2, 0.0f));
    sourcePeaks_ = std::make_unique<std::atomic<float>[]>(sources_.size());
    for (size_t i = 0; i < sources_.size(); ++i) sourcePeaks_[i].store(0.0f);
    mixBlock_.assign(kMaxBlock * 2, 0.0f);

    for (const auto& b : cfg_.buses) {
        auto bus = std::make_unique<Bus>();
        bus->name = b.name;
        for (const auto& [srcName, gain] : b.gains) {
            for (size_t i = 0; i < cfg_.sources.size(); ++i) {
                if (lower(cfg_.sources[i].name) == srcName) {
                    bus->routes.push_back(std::make_unique<Route>(i, gain));
                    break;
                }
            }
        }
        buses_.push_back(std::move(bus));
    }
}

Mixer::~Mixer() { stop(); }

bool Mixer::start(std::string& error) {
    for (auto& bus : buses_) {
        if (!bus->writer.open(utf8ToWide(bus->name))) {
            error = "não foi possível criar a memória compartilhada do bus '" + bus->name + "'";
            return false;
        }
    }
    for (auto& s : sources_) s->start();
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
    return true;
}

void Mixer::stop() {
    if (running_.exchange(false) && thread_.joinable()) thread_.join();
    for (auto& s : sources_) s->stop();
}

bool Mixer::setGain(const std::string& bus, const std::string& source, float gain) {
    const std::string b = lower(bus), s = lower(source);
    for (auto& bb : buses_) {
        if (lower(bb->name) != b) continue;
        for (auto& r : bb->routes) {
            if (lower(cfg_.sources[r->source].name) == s) {
                r->gain.store(std::clamp(gain, 0.0f, 4.0f), std::memory_order_relaxed);
                return true;
            }
        }
    }
    return false;
}

std::vector<BusStats> Mixer::busStats() {
    std::vector<BusStats> out;
    for (auto& b : buses_) {
        BusStats s;
        s.name = b->name;
        s.peakL = b->peakL.exchange(0.0f, std::memory_order_relaxed);
        s.peakR = b->peakR.exchange(0.0f, std::memory_order_relaxed);
        s.clips = b->clips.load(std::memory_order_relaxed);
        out.push_back(s);
    }
    return out;
}

std::vector<SourceStats> Mixer::sourceStats() {
    std::vector<SourceStats> out;
    for (size_t i = 0; i < sources_.size(); ++i) {
        auto& src = sources_[i];
        SourceStats s;
        s.peak = sourcePeaks_[i].exchange(0.0f, std::memory_order_relaxed);
        s.name = src->config().name;
        s.state = src->state();
        s.bufferedFrames = src->ring().available();
        s.underruns = src->underruns();
        s.drops = src->drops();
        s.reconnects = src->reconnects();
        out.push_back(s);
    }
    return out;
}

void Mixer::threadMain() {
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (mmcss) AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);

    // Timer de alta resolução (Win10 1803+); cai para o comum se não houver.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    uint64_t produced = 0;

    while (running_.load(std::memory_order_relaxed)) {
        LARGE_INTEGER due;
        due.QuadPart = -kTickHns;
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer, 50);

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const uint64_t elapsed = uint64_t(now.QuadPart - t0.QuadPart);
        const uint64_t should = elapsed / uint64_t(freq.QuadPart) * kBusSampleRate +
                                (elapsed % uint64_t(freq.QuadPart)) * kBusSampleRate / uint64_t(freq.QuadPart);
        uint64_t pending = should > produced ? should - produced : 0;

        // Se a thread ficou parada mais de 100 ms (sistema travou, DPC alto),
        // não tentamos "recuperar" o atraso: pulamos o relógio para frente.
        if (pending > kBusSampleRate / 10) {
            lateTicks_.fetch_add(1, std::memory_order_relaxed);
            produced = should - std::min<uint64_t>(should, targetFrames_ / 2);
            pending = should - produced;
        }

        while (pending > 0) {
            const size_t n = size_t(std::min<uint64_t>(pending, kMaxBlock));
            process(n);
            produced += n;
            pending -= n;
        }
    }

    if (timer) CloseHandle(timer);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
}

void Mixer::process(size_t frames) {
    for (size_t i = 0; i < sources_.size(); ++i) {
        float* blk = sourceBlocks_[i].data();
        sources_[i]->pull(blk, frames, targetFrames_);
        float p = 0.0f;
        for (size_t k = 0; k < frames * 2; ++k) p = std::max(p, std::fabs(blk[k]));
        updatePeak(sourcePeaks_[i], p);
    }

    for (auto& bus : buses_) {
        float* mix = mixBlock_.data();
        std::fill(mix, mix + frames * 2, 0.0f);
        for (auto& r : bus->routes) {
            const float g = r->gain.load(std::memory_order_relaxed);
            if (g <= 0.0f) continue;
            const float* src = sourceBlocks_[r->source].data();
            for (size_t k = 0; k < frames * 2; ++k) mix[k] += src[k] * g;
        }
        float pl = 0.0f, pr = 0.0f;
        bool clipped = false;
        for (size_t k = 0; k < frames; ++k) {
            float& l = mix[k * 2];
            float& r = mix[k * 2 + 1];
            if (std::fabs(l) > 1.0f || std::fabs(r) > 1.0f) clipped = true;
            l = std::clamp(l, -1.0f, 1.0f);
            r = std::clamp(r, -1.0f, 1.0f);
            pl = std::max(pl, std::fabs(l));
            pr = std::max(pr, std::fabs(r));
        }
        if (clipped) bus->clips.fetch_add(1, std::memory_order_relaxed);
        updatePeak(bus->peakL, pl);
        updatePeak(bus->peakR, pr);
        bus->writer.write(mix, frames);
    }
}

} // namespace novaon
