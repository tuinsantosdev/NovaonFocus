// Plugin VST3 "NovaonFocus Send".
//
// Coloque no Master (ou em qualquer canal) do FL Studio. Ele deixa o som
// passar sem mudar nada e manda uma cópia para o app NovaonFocus por memória
// compartilhada. Isso funciona com o Focusrite USB ASIO, que passa por fora
// do mixer do Windows e por isso não aparece em nenhum loopback.
//
// O plugin só escreve: quem cuida de reamostrar e de segurar o atraso fixo
// é o app (fonte "send"), então a DAW não ganha latência nenhuma.

// Tem que vir primeiro: ele renomeia setState/getState do IEditController
// com um #define e os outros headers do SDK precisam ver o nome já trocado.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "SendEditor.h"
#include "SharedBus.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

using namespace Steinberg;

namespace {

constexpr Vst::ParamID kChannelParam = 0;

// Identificador único do plugin (não mudar: a DAW usa para achar o plugin
// salvo nos projetos).
const FUID kSendUID(0x6E4F7641, 0x53656E64, 0x9A3C41D7, 0x2B8E5F10);

int channelFromNormalized(Vst::ParamValue v) {
    return std::clamp(int(std::lround(v * (novaon::kSendChannels - 1))) + 1, 1, novaon::kSendChannels);
}

Vst::ParamValue normalizedFromChannel(int ch) {
    return double(ch - 1) / double(novaon::kSendChannels - 1);
}

class SendEffect : public Vst::SingleComponentEffect, public novaon::SendViewModel {
public:
    static FUnknown* createInstance(void*) {
        return static_cast<Vst::IAudioProcessor*>(new SendEffect());
    }

    tresult PLUGIN_API initialize(FUnknown* context) override {
        const tresult r = SingleComponentEffect::initialize(context);
        if (r != kResultOk) return r;

        addAudioInput(STR16("Entrada"), Vst::SpeakerArr::kStereo);
        addAudioOutput(STR16("Saída"), Vst::SpeakerArr::kStereo);

        // kCanAutomate faz o canal aparecer também na lista genérica de
        // parâmetros da DAW (Browse parameters no FL Studio).
        auto* list = new Vst::StringListParameter(STR16("Canal"), kChannelParam, nullptr,
                                                  Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsList);
        list->appendString(STR16("Canal 1"));
        list->appendString(STR16("Canal 2"));
        list->appendString(STR16("Canal 3"));
        list->appendString(STR16("Canal 4"));
        list->appendString(STR16("Canal 5"));
        list->appendString(STR16("Canal 6"));
        list->appendString(STR16("Canal 7"));
        list->appendString(STR16("Canal 8"));
        parameters.addParameter(list);
        return kResultOk;
    }

    tresult PLUGIN_API setBusArrangements(Vst::SpeakerArrangement* inputs, int32 numIns,
                                          Vst::SpeakerArrangement* outputs, int32 numOuts) override {
        // Só estéreo: é o que o barramento do NovaonFocus transporta.
        if (numIns == 1 && numOuts == 1 && inputs[0] == Vst::SpeakerArr::kStereo &&
            outputs[0] == Vst::SpeakerArr::kStereo)
            return kResultTrue;
        return kResultFalse;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 size) override {
        return size == Vst::kSample32 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API setupProcessing(Vst::ProcessSetup& setup) override {
        sampleRate_ = uint32_t(std::lround(setup.sampleRate));
        // Buffer de intercalação alocado aqui, nunca dentro do process().
        scratch_.assign(size_t(std::max(setup.maxSamplesPerBlock, 4096)) * 2, 0.0f);
        return SingleComponentEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setActive(TBool state) override {
        if (state) {
            openWriter();
        } else {
            writer_.close();
            openChannel_ = 0;
        }
        return SingleComponentEffect::setActive(state);
    }

    tresult PLUGIN_API process(Vst::ProcessData& data) override {
        // Troca de canal vinda da interface ou de automação.
        if (data.inputParameterChanges) {
            const int32 count = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < count; ++i) {
                Vst::IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
                if (!q || q->getParameterId() != kChannelParam || q->getPointCount() <= 0) continue;
                Vst::ParamValue v = 0;
                int32 offset = 0;
                if (q->getPoint(q->getPointCount() - 1, offset, v) == kResultTrue)
                    channel_.store(channelFromNormalized(v));
            }
        }
        // Reabrir a memória só acontece quando o canal muda (raro).
        if (channel_.load() != openChannel_) openWriter();

        lastProcessTick_.store(GetTickCount());
        connected_.store(writer_.readerAlive());

        if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1) return kResultOk;
        Vst::AudioBusBuffers& in = data.inputs[0];
        Vst::AudioBusBuffers& out = data.outputs[0];
        if (in.numChannels < 2 || out.numChannels < 2) return kResultOk;

        const int32 n = data.numSamples;
        for (int32 c = 0; c < 2; ++c) {
            float* src = in.channelBuffers32[c];
            float* dst = out.channelBuffers32[c];
            if (src != dst) std::copy(src, src + n, dst);
        }
        out.silenceFlags = in.silenceFlags;

        const float* l = in.channelBuffers32[0];
        const float* r = in.channelBuffers32[1];
        const size_t frames = std::min(size_t(n), scratch_.size() / 2);
        const bool silent = (in.silenceFlags & 3) == 3;
        float pl = 0.0f, pr = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            const float a = silent ? 0.0f : l[i];
            const float b = silent ? 0.0f : r[i];
            scratch_[i * 2] = a;
            scratch_[i * 2 + 1] = b;
            pl = std::max(pl, std::fabs(a));
            pr = std::max(pr, std::fabs(b));
        }
        writer_.write(scratch_.data(), frames);
        raisePeak(peak_[0], pl);
        raisePeak(peak_[1], pr);
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) override {
        IBStreamer s(state, kLittleEndian);
        int32 ch = 1;
        if (!s.readInt32(ch)) return kResultFalse;
        ch = std::clamp(ch, int32(1), int32(novaon::kSendChannels));
        channel_.store(ch);
        setParamNormalized(kChannelParam, normalizedFromChannel(ch));
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* state) override {
        IBStreamer s(state, kLittleEndian);
        return s.writeInt32(channel_.load()) ? kResultOk : kResultFalse;
    }

    tresult PLUGIN_API setParamNormalized(Vst::ParamID tag, Vst::ParamValue value) override {
        if (tag == kChannelParam) channel_.store(channelFromNormalized(value));
        return SingleComponentEffect::setParamNormalized(tag, value);
    }

    IPlugView* PLUGIN_API createView(FIDString name) override {
        if (name && std::strcmp(name, Vst::ViewType::kEditor) == 0) return novaon::createSendView(this);
        return nullptr;
    }

    // --- SendViewModel (thread da interface) ---
    int channel() const override { return channel_.load(); }
    void requestChannel(int ch) override {
        const Vst::ParamValue v = normalizedFromChannel(ch);
        // Avisa a DAW (para salvar no projeto e mandar ao processamento).
        beginEdit(kChannelParam);
        setParamNormalized(kChannelParam, v);
        performEdit(kChannelParam, v);
        endEdit(kChannelParam);
    }
    float takePeak(int side) override { return peak_[side & 1].exchange(0.0f); }
    uint32_t sampleRate() const override { return sampleRate_; }
    bool appConnected() const override { return connected_.load(); }
    bool dawProcessing() const override { return GetTickCount() - lastProcessTick_.load() < 500; }

private:
    static void raisePeak(std::atomic<float>& p, float v) {
        float cur = p.load(std::memory_order_relaxed);
        while (v > cur && !p.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }

    void openWriter() {
        const int ch = channel_.load();
        writer_.openNamed(novaon::sharedSendName(ch), sampleRate_);
        openChannel_ = ch;
    }

    novaon::SharedBusWriter writer_;
    std::vector<float> scratch_;
    uint32_t sampleRate_ = 48000;
    std::atomic<int> channel_{1};
    int openChannel_ = 0;
    std::atomic<float> peak_[2] = {0.0f, 0.0f};
    std::atomic<uint32_t> lastProcessTick_{0};
    std::atomic<bool> connected_{false};
};

} // namespace

BEGIN_FACTORY_DEF("NovaonFocus", "https://github.com/tuinsantosdev/NovaonFocus", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(kSendUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "NovaonFocus Send", 0, Vst::PlugType::kFx, "1.0.0", kVstVersionString,
           SendEffect::createInstance)

END_FACTORY
