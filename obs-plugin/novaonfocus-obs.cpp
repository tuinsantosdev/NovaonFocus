// Plugin do OBS: fonte de áudio "NovaonFocus Bus".
// Lê um barramento da memória compartilhada escrita pelo app NovaonFocus e
// entrega ao OBS. Cada barramento vira uma fonte separada no mixer do OBS,
// então dá para mandar cada um para uma faixa diferente na gravação.

#include "../src/core/SharedBus.h"

#include <obs-module.h>
#include <util/platform.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

OBS_DECLARE_MODULE()

namespace {

using namespace novaon;

constexpr uint64_t kStartBehindFrames = 480;   // 10 ms atrás do escritor
constexpr uint64_t kMaxBehindFrames = 9600;    // > 200 ms: ressincroniza
constexpr size_t kChunkFrames = 480;

std::wstring toWide(const char* s) {
    if (!s || !*s) return L"live";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(size_t(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

struct BusSource {
    obs_source_t* source = nullptr;
    std::mutex nameLock;
    std::wstring busName = L"live";
    std::atomic<bool> nameChanged{true};
    std::atomic<bool> running{true};
    std::thread thread;

    HANDLE mapping = nullptr;
    SharedBusHeader* header = nullptr;
    uint64_t readPos = 0;

    void closeMapping() {
        if (header) UnmapViewOfFile(header);
        if (mapping) CloseHandle(mapping);
        header = nullptr;
        mapping = nullptr;
    }

    bool openMapping() {
        std::wstring name;
        {
            std::lock_guard<std::mutex> g(nameLock);
            name = busName;
        }
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, sharedBusName(name).c_str());
        if (!mapping) return false;
        void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sharedBusBytes());
        if (!view) { closeMapping(); return false; }
        header = static_cast<SharedBusHeader*>(view);
        if (header->magic != kBusMagic || header->version != kBusVersion ||
            header->channels != kBusChannels || header->capacityFrames != kBusCapacityFrames) {
            closeMapping();
            return false;
        }
        const uint64_t w = header->writeFrames.load(std::memory_order_acquire);
        readPos = w > kStartBehindFrames ? w - kStartBehindFrames : 0;
        return true;
    }

    void run() {
        std::vector<float> chunk(kChunkFrames * 2);
        while (running.load()) {
            if (nameChanged.exchange(false)) closeMapping();
            if (!header && !openMapping()) {
                os_sleep_ms(250); // app ainda não abriu: tenta de novo
                continue;
            }

            const uint64_t w = header->writeFrames.load(std::memory_order_acquire);
            if (w < readPos || w - readPos > kMaxBehindFrames) {
                // App reiniciou ou o OBS engasgou: volta para perto do presente.
                readPos = w > kStartBehindFrames ? w - kStartBehindFrames : 0;
            }

            const float* data = sharedBusData(header);
            while (readPos < w) {
                const size_t n = size_t(std::min<uint64_t>(w - readPos, kChunkFrames));
                for (size_t i = 0; i < n; ++i) {
                    const size_t slot = size_t((readPos + i) % kBusCapacityFrames);
                    chunk[i * 2] = data[slot * 2];
                    chunk[i * 2 + 1] = data[slot * 2 + 1];
                }
                obs_source_audio audio{};
                audio.data[0] = reinterpret_cast<const uint8_t*>(chunk.data());
                audio.frames = uint32_t(n);
                audio.speakers = SPEAKERS_STEREO;
                audio.format = AUDIO_FORMAT_FLOAT;
                audio.samples_per_sec = kBusSampleRate;
                // Carimbo de tempo do fim do bloco que ainda está na fila.
                const uint64_t behind = w - readPos;
                audio.timestamp = os_gettime_ns() - behind * 1000000000ULL / kBusSampleRate;
                obs_source_output_audio(source, &audio);
                readPos += n;
            }
            os_sleep_ms(5);
        }
        closeMapping();
    }
};

const char* busGetName(void*) { return "NovaonFocus Bus"; }

void busUpdate(void* data, obs_data_t* settings) {
    auto* s = static_cast<BusSource*>(data);
    {
        std::lock_guard<std::mutex> g(s->nameLock);
        s->busName = toWide(obs_data_get_string(settings, "bus"));
    }
    s->nameChanged = true;
}

void* busCreate(obs_data_t* settings, obs_source_t* source) {
    auto* s = new BusSource();
    s->source = source;
    busUpdate(s, settings);
    s->thread = std::thread([s] { s->run(); });
    return s;
}

void busDestroy(void* data) {
    auto* s = static_cast<BusSource*>(data);
    s->running = false;
    if (s->thread.joinable()) s->thread.join();
    delete s;
}

void busDefaults(obs_data_t* settings) { obs_data_set_default_string(settings, "bus", "live"); }

obs_properties_t* busProperties(void*) {
    obs_properties_t* props = obs_properties_create();
    obs_properties_add_text(props, "bus", "Barramento (nome do [bus] no novaonfocus.ini)", OBS_TEXT_DEFAULT);
    return props;
}

} // namespace

bool obs_module_load(void) {
    static obs_source_info info{};
    info.id = "novaonfocus_bus";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = busGetName;
    info.create = busCreate;
    info.destroy = busDestroy;
    info.update = busUpdate;
    info.get_defaults = busDefaults;
    info.get_properties = busProperties;
    info.icon_type = OBS_ICON_TYPE_AUDIO_INPUT;
    obs_register_source(&info);
    blog(LOG_INFO, "[NovaonFocus] plugin carregado");
    return true;
}
