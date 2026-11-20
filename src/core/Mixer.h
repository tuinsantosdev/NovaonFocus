#pragma once
// Motor de mixagem: lê as fontes num relógio próprio de 48 kHz, soma cada
// barramento com seus ganhos e publica o resultado em memória compartilhada
// para o plugin do OBS.

#include "Config.h"
#include "SharedBus.h"
#include "WasapiSource.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace novaon {

struct BusStats {
    std::string name;
    float peakL = 0.0f;
    float peakR = 0.0f;
    uint64_t clips = 0;
};

struct SourceStats {
    std::string name;
    SourceState state = SourceState::Stopped;
    size_t bufferedFrames = 0;
    uint64_t underruns = 0;
    uint64_t drops = 0;
    uint64_t reconnects = 0;
    float peak = 0.0f; // pico desde a última consulta (antes dos ganhos)
};

class Mixer {
public:
    explicit Mixer(const EngineConfig& cfg);
    ~Mixer();

    bool start(std::string& error);
    void stop();

    // Ganho em tempo real (thread-safe). Retorna false se não existir.
    bool setGain(const std::string& bus, const std::string& source, float gain);

    // Leitura para a interface; zera os picos.
    std::vector<BusStats> busStats();
    std::vector<SourceStats> sourceStats();
    uint64_t lateTicks() const { return lateTicks_.load(std::memory_order_relaxed); }

private:
    struct Route {
        size_t source;
        std::atomic<float> gain;
        Route(size_t s, float g) : source(s), gain(g) {}
    };
    struct Bus {
        std::string name;
        std::vector<std::unique_ptr<Route>> routes;
        SharedBusWriter writer;
        std::atomic<float> peakL{0.0f}, peakR{0.0f};
        std::atomic<uint64_t> clips{0};
    };

    void threadMain();
    void process(size_t frames);

    EngineConfig cfg_;
    size_t targetFrames_;
    std::vector<std::unique_ptr<WasapiSource>> sources_;
    std::vector<std::unique_ptr<Bus>> buses_;
    std::vector<std::vector<float>> sourceBlocks_;
    std::unique_ptr<std::atomic<float>[]> sourcePeaks_;
    std::vector<float> mixBlock_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> lateTicks_{0};
};

} // namespace novaon
