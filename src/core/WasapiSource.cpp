#include "WasapiSource.h"

#include "AudioDevices.h"
#include "Resampler.h"
#include "SharedBus.h"

#include <audioclient.h>
#if __has_include(<audioclientactivationparams.h>)
#include <audioclientactivationparams.h>
#else
// MinGW ainda não traz este header (SDK do Windows 10 2004+). Definições
// equivalentes às do SDK oficial, só para compilar fora do MSVC.
typedef enum AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
} AUDIOCLIENT_ACTIVATION_TYPE;
typedef enum PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
} PROCESS_LOOPBACK_MODE;
typedef struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;
typedef struct AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union {
        AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
    };
} AUDIOCLIENT_ACTIVATION_PARAMS;
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace novaon {

namespace {

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, definido aqui para não depender de ksuser.
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

template <class T> void safeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

// Handler da ativação assíncrona do process loopback.
class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    ActivateHandler() { done_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~ActivateHandler() {
        safeRelease(client_);
        if (done_) CloseHandle(done_);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        } else if (riid == __uuidof(IAgileObject)) {
            *ppv = static_cast<IAgileObject*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ULONG(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return ULONG(r);
    }

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hrActivate = E_FAIL;
        IUnknown* unk = nullptr;
        hr_ = op->GetActivateResult(&hrActivate, &unk);
        if (SUCCEEDED(hr_)) hr_ = hrActivate;
        if (SUCCEEDED(hr_) && unk)
            hr_ = unk->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client_));
        safeRelease(unk);
        SetEvent(done_);
        return S_OK;
    }

    // Espera a ativação e entrega o IAudioClient (o chamador faz Release).
    HRESULT wait(IAudioClient** out, DWORD timeoutMs) {
        if (WaitForSingleObject(done_, timeoutMs) != WAIT_OBJECT_0) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        if (FAILED(hr_)) return hr_;
        *out = client_;
        client_ = nullptr;
        return S_OK;
    }

private:
    LONG ref_ = 1;
    HANDLE done_ = nullptr;
    HRESULT hr_ = E_PENDING;
    IAudioClient* client_ = nullptr;
};

HRESULT activateProcessLoopback(uint32_t pid, bool exclude, IAudioClient** out) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        exclude ? PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
                : PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv{};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    ActivateHandler* handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &pv, handler, &op);
    if (SUCCEEDED(hr)) hr = handler->wait(out, 5000);
    safeRelease(op);
    handler->Release();
    return hr;
}

// Formato que pedimos ao Windows: 48 kHz estéreo. O modo AUTOCONVERTPCM faz
// o próprio motor de áudio converter taxa e canais com qualidade.
WAVEFORMATEXTENSIBLE floatFormat() {
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = 2;
    f.Format.nSamplesPerSec = kBusSampleRate;
    f.Format.wBitsPerSample = 32;
    f.Format.nBlockAlign = 8;
    f.Format.nAvgBytesPerSec = kBusSampleRate * 8;
    f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f.Samples.wValidBitsPerSample = 32;
    f.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    f.SubFormat = kSubtypeFloat;
    return f;
}

// O process loopback só aceita PCM simples (como no exemplo da Microsoft).
WAVEFORMATEX pcm16Format() {
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = kBusSampleRate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = kBusSampleRate * 4;
    f.cbSize = 0;
    return f;
}

class ComScope {
public:
    ComScope() { hr_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { if (SUCCEEDED(hr_)) CoUninitialize(); }
private:
    HRESULT hr_;
};

} // namespace

const char* sourceStateName(SourceState s) {
    switch (s) {
    case SourceState::Stopped: return "parada";
    case SourceState::Connecting: return "conectando";
    case SourceState::Running: return "ativa";
    case SourceState::Waiting: return "aguardando";
    }
    return "?";
}

WasapiSource::WasapiSource(const SourceConfig& cfg, size_t ringFrames)
    : cfg_(cfg), ring_(ringFrames, 2) {}

WasapiSource::~WasapiSource() { stop(); }

void WasapiSource::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { threadMain(); });
}

void WasapiSource::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    state_ = SourceState::Stopped;
}

void WasapiSource::threadMain() {
    ComScope com;
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    bool first = true;
    while (running_.load()) {
        if (!first) reconnects_.fetch_add(1, std::memory_order_relaxed);
        first = false;
        state_ = SourceState::Connecting;
        if (!runOnce()) {
            // Dispositivo ausente ou programa fechado: tenta de novo em 1 s.
            state_ = SourceState::Waiting;
            for (int i = 0; i < 10 && running_.load(); ++i) Sleep(100);
        }
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
}

bool WasapiSource::runOnce() {
    if (cfg_.type == SourceType::Send) return runSend();

    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    HANDLE event = nullptr;
    uint32_t pid = 0;
    bool isPcm16 = false;
    bool ok = false;

    auto cleanup = [&] {
        if (client) client->Stop();
        safeRelease(capture);
        safeRelease(client);
        safeRelease(device);
        if (event) CloseHandle(event);
    };

    HRESULT hr = S_OK;
    const bool isApp = cfg_.type == SourceType::App || cfg_.type == SourceType::AppExclude;
    if (isApp) {
        pid = findRootProcess(cfg_.exe);
        if (pid == 0) { cleanup(); return false; }
        hr = activateProcessLoopback(pid, cfg_.type == SourceType::AppExclude, &client);
        if (FAILED(hr)) { cleanup(); return false; }
        WAVEFORMATEX fmt = pcm16Format();
        isPcm16 = true;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                200000, 0, &fmt, nullptr);
    } else {
        const bool capt = cfg_.type == SourceType::Capture;
        device = findDevice(cfg_.device, capt);
        if (!device) { cleanup(); return false; }
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&client));
        if (FAILED(hr)) { cleanup(); return false; }
        WAVEFORMATEXTENSIBLE fmt = floatFormat();
        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                      AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (!capt) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        // Buffer de 10 ms: curto o bastante para latência baixa, longo o
        // bastante para não estourar quando o sistema engasga.
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 100000, 0,
                                reinterpret_cast<WAVEFORMATEX*>(&fmt), nullptr);
    }
    if (FAILED(hr)) { cleanup(); return false; }

    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event || FAILED(client->SetEventHandle(event)) ||
        FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture))) ||
        FAILED(client->Start())) {
        cleanup();
        return false;
    }

    state_ = SourceState::Running;
    ok = true;
    std::vector<float> scratch(4096 * 2);
    ULONGLONG lastCheck = GetTickCount64();

    while (running_.load()) {
        const DWORD w = WaitForSingleObject(event, 100);
        if (w == WAIT_FAILED) break;

        // Loopback não sinaliza durante silêncio, então checamos de vez em
        // quando se o programa ainda existe.
        const ULONGLONG now = GetTickCount64();
        if (isApp && now - lastCheck > 2000) {
            lastCheck = now;
            if (!processAlive(pid)) break;
        }

        bool deviceLost = false;
        for (;;) {
            UINT32 packet = 0;
            hr = capture->GetNextPacketSize(&packet);
            if (FAILED(hr)) { deviceLost = true; break; }
            if (packet == 0) break;

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) { deviceLost = true; break; }

            if (scratch.size() < size_t(frames) * 2) scratch.resize(size_t(frames) * 2);
            float* dst = scratch.data();
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill(dst, dst + size_t(frames) * 2, 0.0f);
            } else if (isPcm16) {
                const int16_t* src = reinterpret_cast<const int16_t*>(data);
                for (size_t i = 0; i < size_t(frames) * 2; ++i) dst[i] = float(src[i]) / 32768.0f;
            } else {
                const float* src = reinterpret_cast<const float*>(data);
                std::copy(src, src + size_t(frames) * 2, dst);
            }

            // Scarlett 2i2: entrada 1 chega na esquerda, entrada 2 na direita.
            // Para um microfone só, duplicamos o canal escolhido nos dois lados.
            if (cfg_.inputChannel == 1 || cfg_.inputChannel == 2) {
                const size_t off = size_t(cfg_.inputChannel - 1);
                for (size_t i = 0; i < frames; ++i) {
                    const float v = dst[i * 2 + off];
                    dst[i * 2] = v;
                    dst[i * 2 + 1] = v;
                }
            }

            ring_.write(dst, frames);
            capture->ReleaseBuffer(frames);
        }
        if (deviceLost) break; // AUDCLNT_E_DEVICE_INVALIDATED etc.: reconecta
    }

    cleanup();
    return ok;
}

bool WasapiSource::runSend() {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sharedSendName(cfg_.sendChannel).c_str());
    if (!mapping) return false; // plugin ainda não foi carregado na DAW
    auto* header = static_cast<SharedBusHeader*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sharedBusBytes()));
    if (!header) {
        CloseHandle(mapping);
        return false;
    }

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    AdaptiveResampler resampler;
    std::vector<float> in(1024 * 2);
    std::vector<float> out;
    out.reserve(8192);
    uint64_t readPos = 0;
    bool synced = false;
    double fill = double(targetFrames_);
    const float* data = sharedBusData(header);
    state_ = SourceState::Waiting;

    while (running_.load()) {
        LARGE_INTEGER due;
        due.QuadPart = -10000; // 1 ms
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer, 20);

        const uint32_t rate = header->sampleRate;
        const bool valid = header->magic == kBusMagic && header->version == kBusVersion &&
                           header->channels == kBusChannels &&
                           header->capacityFrames == kBusCapacityFrames && rate >= 8000;
        // Sem escrita há 250 ms: DAW fechada, plugin removido ou áudio parado.
        const bool alive = valid && GetTickCount64() - header->heartbeatMs.load(std::memory_order_relaxed) < 250;
        if (!alive) {
            state_ = SourceState::Waiting;
            sendRate_.store(0, std::memory_order_relaxed);
            sendPpm_.store(0.0f, std::memory_order_relaxed);
            synced = false;
            continue;
        }

        const uint64_t w = header->writeFrames.load(std::memory_order_acquire);
        if (!synced || w < readPos || w - readPos > kBusCapacityFrames / 2) {
            // Começa do presente: nunca toca áudio velho acumulado.
            readPos = w;
            resampler.reset();
            fill = double(targetFrames_);
            synced = true;
        }
        state_ = SourceState::Running;

        // Controle de deriva: mede quanto o buffer desta fonte está cheio em
        // relação à folga desejada e ajusta a razão em até ±0,5%. Assim o
        // atraso fica parado no alvo em vez de crescer com o tempo.
        fill = fill * 0.99 + double(ring_.available()) * 0.01;
        const double target = double(targetFrames_ + 96);
        double err = (fill - target) / target;
        err = std::clamp(err, -1.0, 1.0);
        const double step = double(rate) / double(kBusSampleRate) * (1.0 + 0.005 * err);
        sendRate_.store(rate, std::memory_order_relaxed);
        sendPpm_.store(float(0.005 * err * 1e6), std::memory_order_relaxed);

        while (readPos < w) {
            const size_t n = size_t(std::min<uint64_t>(w - readPos, 1024));
            for (size_t i = 0; i < n; ++i) {
                const size_t slot = size_t((readPos + i) % kBusCapacityFrames);
                in[i * 2] = data[slot * 2];
                in[i * 2 + 1] = data[slot * 2 + 1];
            }
            readPos += n;
            out.clear();
            resampler.process(in.data(), n, step, out);
            if (!out.empty()) ring_.write(out.data(), out.size() / 2);
        }
    }

    sendRate_.store(0, std::memory_order_relaxed);
    sendPpm_.store(0.0f, std::memory_order_relaxed);
    if (timer) CloseHandle(timer);
    UnmapViewOfFile(header);
    CloseHandle(mapping);
    return true;
}

void WasapiSource::pull(float* out, size_t frames, size_t targetFrames) {
    size_t avail = ring_.available();

    // Aguarda juntar a folga antes de começar a tocar (evita estalos).
    if (!primed_) {
        if (avail < targetFrames + frames) {
            std::fill(out, out + frames * 2, 0.0f);
            return;
        }
        primed_ = true;
    }

    // Excesso de dado acumulado (deriva de relógio ou engasgo do mixer):
    // descarta para manter a latência sob controle.
    const size_t maxFrames = targetFrames * 2 + frames;
    if (avail > maxFrames) {
        ring_.skip(avail - (targetFrames + frames));
        drops_.fetch_add(1, std::memory_order_relaxed);
        avail = ring_.available();
    }

    const size_t got = ring_.read(out, frames);
    if (got < frames) {
        std::fill(out + got * 2, out + frames * 2, 0.0f);
        // Só conta como falha se a fonte estava ativa; fonte em silêncio
        // (loopback sem som tocando) simplesmente não entrega pacotes.
        if (state_.load(std::memory_order_relaxed) == SourceState::Running && got > 0)
            underruns_.fetch_add(1, std::memory_order_relaxed);
        primed_ = false;
    }
}

} // namespace novaon
