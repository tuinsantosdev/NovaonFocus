#pragma once
// Janela do plugin NovaonFocus Send (Win32/GDI, sem dependências extras),
// no mesmo visual da interface do app: fundo escuro e destaque roxo.

#include "pluginterfaces/gui/iplugview.h"

#include <cstdint>

namespace novaon {

// O que a janela precisa saber do plugin. Tudo é chamado na thread da
// interface e só lê valores atômicos atualizados pela thread de áudio.
class SendViewModel {
public:
    virtual ~SendViewModel() = default;
    virtual int channel() const = 0;
    virtual void requestChannel(int channel) = 0; // 1 a 8
    virtual float takePeak(int side) = 0;          // 0 = L, 1 = R; zera o pico
    virtual uint32_t sampleRate() const = 0;
    virtual bool appConnected() const = 0;         // o app está lendo o canal
    virtual bool dawProcessing() const = 0;        // a DAW está chamando process()
};

Steinberg::IPlugView* createSendView(SendViewModel* model);

} // namespace novaon
