#pragma once
// Layout da memória compartilhada de um barramento (bus).
// O app NovaonFocus escreve; o plugin do OBS lê. Este header é usado
// pelos dois lados, então mudanças no layout exigem subir kVersion.

#include <atomic>
#include <cstdint>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace novaon {

constexpr uint32_t kBusMagic = 0x4E564F42;   // 'NVOB'
constexpr uint32_t kBusVersion = 1;
constexpr uint32_t kBusSampleRate = 48000;
constexpr uint32_t kBusChannels = 2;
constexpr uint32_t kBusCapacityFrames = 48000; // 1 segundo de histórico
constexpr size_t kBusHeaderBytes = 128;

struct SharedBusHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t capacityFrames;
    // GetTickCount() da última vez que um leitor (o app, numa fonte "send")
    // olhou este bloco. O plugin VST3 usa para mostrar "conectado".
    uint32_t readerTick;
    // Total de quadros já escritos desde a criação (monotônico).
    alignas(8) std::atomic<uint64_t> writeFrames;
    // GetTickCount64() da última escrita: o leitor detecta app parado.
    alignas(8) std::atomic<uint64_t> heartbeatMs;
};
static_assert(sizeof(SharedBusHeader) <= kBusHeaderBytes, "header grande demais");

inline size_t sharedBusBytes() {
    return kBusHeaderBytes + size_t(kBusCapacityFrames) * kBusChannels * sizeof(float);
}

inline std::wstring sharedBusName(const std::wstring& bus) {
    return L"Local\\NovaonFocus.Bus." + bus;
}

// Canal de envio do plugin VST3 "NovaonFocus Send" (1 a 8). O plugin escreve
// na taxa da DAW (sampleRate no header) e o app reamostra para 48 kHz.
constexpr int kSendChannels = 8;
inline std::wstring sharedSendName(int channel) {
    return L"Local\\NovaonFocus.Send." + std::to_wstring(channel);
}

inline float* sharedBusData(SharedBusHeader* h) {
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(h) + kBusHeaderBytes);
}

// Lado escritor (app para os barramentos, plugin VST3 para os envios).
class SharedBusWriter {
public:
    SharedBusWriter() = default;
    SharedBusWriter(const SharedBusWriter&) = delete;
    SharedBusWriter& operator=(const SharedBusWriter&) = delete;
    ~SharedBusWriter() { close(); }

    bool open(const std::wstring& bus) { return openNamed(sharedBusName(bus), kBusSampleRate); }

    bool openNamed(const std::wstring& fullName, uint32_t sampleRate) {
        close();
        const size_t bytes = sharedBusBytes();
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      0, static_cast<DWORD>(bytes),
                                      fullName.c_str());
        if (!mapping_) return false;
        const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
        void* view = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
        if (!view) { close(); return false; }
        header_ = static_cast<SharedBusHeader*>(view);
        // Se o OBS ainda segura o mapeamento de uma execução anterior,
        // continuamos do mesmo contador para o leitor não se perder.
        const bool reusable = existed && header_->magic == kBusMagic &&
                              header_->version == kBusVersion &&
                              header_->sampleRate == sampleRate &&
                              header_->capacityFrames == kBusCapacityFrames &&
                              header_->channels == kBusChannels;
        if (!reusable) {
            header_->magic = 0;
            header_->version = kBusVersion;
            header_->sampleRate = sampleRate;
            header_->channels = kBusChannels;
            header_->capacityFrames = kBusCapacityFrames;
            header_->readerTick = 0;
            header_->writeFrames.store(0, std::memory_order_relaxed);
            header_->heartbeatMs.store(GetTickCount64(), std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            header_->magic = kBusMagic;
        }
        return true;
    }

    void write(const float* interleaved, size_t frames) {
        if (!header_) return;
        float* data = sharedBusData(header_);
        uint64_t w = header_->writeFrames.load(std::memory_order_relaxed);
        for (size_t i = 0; i < frames; ++i) {
            const size_t slot = static_cast<size_t>((w + i) % kBusCapacityFrames);
            data[slot * 2] = interleaved[i * 2];
            data[slot * 2 + 1] = interleaved[i * 2 + 1];
        }
        header_->writeFrames.store(w + frames, std::memory_order_release);
        header_->heartbeatMs.store(GetTickCount64(), std::memory_order_relaxed);
    }

    // true se algum leitor marcou presença no último segundo.
    bool readerAlive() const {
        if (!header_) return false;
        const uint32_t t = *reinterpret_cast<volatile const uint32_t*>(&header_->readerTick);
        // Diferença com sinal: o leitor grava GetTickCount() | 1, que pode
        // ficar 1 ms "no futuro" e não pode virar um número enorme.
        return t != 0 && int32_t(GetTickCount() - t) < 1000;
    }

    void close() {
        if (header_) UnmapViewOfFile(header_);
        if (mapping_) CloseHandle(mapping_);
        header_ = nullptr;
        mapping_ = nullptr;
    }

private:
    HANDLE mapping_ = nullptr;
    SharedBusHeader* header_ = nullptr;
};

} // namespace novaon
