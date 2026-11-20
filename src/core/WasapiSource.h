#pragma once
// Uma fonte de áudio capturada via WASAPI em sua própria thread:
//  - capture:     entrada de um dispositivo (ex.: microfone da Scarlett)
//  - loopback:    tudo o que toca num dispositivo de saída (áudio do PC)
//  - app:         só o áudio de um programa (process loopback, Win10 2004+)
//  - app_exclude: tudo, menos um programa (ex.: PC sem o Discord)
//  - send:        o que o plugin VST3 "NovaonFocus Send" manda da DAW
//                 (funciona com o ASIO da Focusrite, que o Windows não vê)
//
// A thread reconecta sozinha quando o dispositivo some ou o programa
// fecha, para que um problema de USB não derrube o roteamento inteiro.

#include "Config.h"
#include "RingBuffer.h"

#include <atomic>
#include <string>
#include <thread>

namespace novaon {

enum class SourceState { Stopped, Connecting, Running, Waiting };

class WasapiSource {
public:
    WasapiSource(const SourceConfig& cfg, size_t ringFrames);
    ~WasapiSource();
    WasapiSource(const WasapiSource&) = delete;
    WasapiSource& operator=(const WasapiSource&) = delete;

    // Folga desejada no buffer (definida pelo mixer antes do start).
    void setTargetFrames(size_t frames) { targetFrames_ = frames; }

    void start();
    void stop();

    const SourceConfig& config() const { return cfg_; }
    SourceState state() const { return state_.load(std::memory_order_relaxed); }
    FrameRing& ring() { return ring_; }

    // Chamado pela thread do mixer. Preenche exatamente `frames` quadros
    // estéreo, completando com silêncio se faltar dado. `targetFrames` é
    // a folga de latência desejada; o excesso acima de 2x é descartado
    // para corrigir deriva de relógio entre dispositivos.
    void pull(float* out, size_t frames, size_t targetFrames);

    uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    uint64_t drops() const { return drops_.load(std::memory_order_relaxed); }
    uint64_t reconnects() const { return reconnects_.load(std::memory_order_relaxed); }

private:
    void threadMain();
    bool runOnce(); // retorna quando a sessão termina; false = falha ao abrir
    bool runSend(); // fonte "send": lê a memória compartilhada do plugin VST3

    SourceConfig cfg_;
    FrameRing ring_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<SourceState> state_{SourceState::Stopped};
    size_t targetFrames_ = 960;

    // Estado do consumidor (só a thread do mixer mexe).
    bool primed_ = false;
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> drops_{0};
    std::atomic<uint64_t> reconnects_{0};
};

const char* sourceStateName(SourceState s);

} // namespace novaon
