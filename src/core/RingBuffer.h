#pragma once
// Buffer circular lock-free de um produtor e um consumidor (SPSC).
// Usado entre a thread de captura de cada fonte e a thread do mixer.
// Nenhuma alocação ou lock acontece depois do construtor.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace novaon {

class FrameRing {
public:
    // capacityFrames é arredondado para a próxima potência de 2.
    FrameRing(size_t capacityFrames, size_t channels) : channels_(channels) {
        size_t cap = 1;
        while (cap < capacityFrames) cap <<= 1;
        capacity_ = cap;
        mask_ = cap - 1;
        data_.assign(cap * channels, 0.0f);
    }

    size_t channels() const { return channels_; }
    size_t capacity() const { return capacity_; }

    size_t available() const {
        return static_cast<size_t>(write_.load(std::memory_order_acquire) -
                                   read_.load(std::memory_order_relaxed));
    }

    // Produtor. Se não houver espaço, os quadros mais novos são descartados
    // e o contador de overflow sobe (o consumidor está atrasado).
    size_t write(const float* interleaved, size_t frames) {
        const uint64_t w = write_.load(std::memory_order_relaxed);
        const uint64_t r = read_.load(std::memory_order_acquire);
        const size_t space = capacity_ - static_cast<size_t>(w - r);
        if (frames > space) {
            overflows_.fetch_add(1, std::memory_order_relaxed);
            frames = space;
        }
        for (size_t i = 0; i < frames; ++i) {
            float* dst = &data_[((w + i) & mask_) * channels_];
            const float* src = interleaved ? &interleaved[i * channels_] : nullptr;
            for (size_t c = 0; c < channels_; ++c) dst[c] = src ? src[c] : 0.0f;
        }
        write_.store(w + frames, std::memory_order_release);
        return frames;
    }

    // Consumidor. Lê até `frames` quadros; retorna quantos leu.
    size_t read(float* interleaved, size_t frames) {
        const uint64_t r = read_.load(std::memory_order_relaxed);
        const uint64_t w = write_.load(std::memory_order_acquire);
        const size_t avail = static_cast<size_t>(w - r);
        if (frames > avail) frames = avail;
        for (size_t i = 0; i < frames; ++i) {
            const float* src = &data_[((r + i) & mask_) * channels_];
            float* dst = &interleaved[i * channels_];
            for (size_t c = 0; c < channels_; ++c) dst[c] = src[c];
        }
        read_.store(r + frames, std::memory_order_release);
        return frames;
    }

    // Consumidor. Descarta quadros sem copiar (correção de deriva/latência).
    size_t skip(size_t frames) {
        const uint64_t r = read_.load(std::memory_order_relaxed);
        const uint64_t w = write_.load(std::memory_order_acquire);
        const size_t avail = static_cast<size_t>(w - r);
        if (frames > avail) frames = avail;
        read_.store(r + frames, std::memory_order_release);
        return frames;
    }

    uint64_t overflows() const { return overflows_.load(std::memory_order_relaxed); }

private:
    size_t channels_;
    size_t capacity_ = 0;
    size_t mask_ = 0;
    std::vector<float> data_;
    alignas(64) std::atomic<uint64_t> write_{0};
    alignas(64) std::atomic<uint64_t> read_{0};
    std::atomic<uint64_t> overflows_{0};
};

} // namespace novaon
