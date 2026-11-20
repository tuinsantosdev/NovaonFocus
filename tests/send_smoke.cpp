// Teste de ponta a ponta do caminho DAW -> app -> OBS, sem placa de som:
//  1. carrega o novaonfocus-send.vst3 como uma DAW faria;
//  2. toca um seno de 1 kHz a 44,1 kHz nele por alguns segundos;
//  3. roda o mixer do NovaonFocus com uma fonte "send" e um bus "live";
//  4. confere que o seno chegou ao bus (o que o plugin do OBS lê).
//
//   send_smoke <caminho do .vst3>

#include "Config.h"
#include "Mixer.h"
#include "SharedBus.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace Steinberg;

static int fail(const char* msg) {
    std::printf("FALHOU: %s\n", msg);
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return fail("uso: send_smoke <plugin.vst3>");

    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) return fail("LoadLibrary");
    using InitFn = bool (*)();
    using FactoryFn = IPluginFactory* (*)();
    auto init = reinterpret_cast<InitFn>(GetProcAddress(dll, "InitDll"));
    auto getFactory = reinterpret_cast<FactoryFn>(GetProcAddress(dll, "GetPluginFactory"));
    if (!init || !getFactory || !init()) return fail("exports InitDll/GetPluginFactory");
    IPluginFactory* factory = getFactory();
    if (!factory || factory->countClasses() != 1) return fail("factory");
    PClassInfo info{};
    factory->getClassInfo(0, &info);
    std::printf("classe: %s (%s)\n", info.name, info.category);

    Vst::IComponent* comp = nullptr;
    if (factory->createInstance(info.cid, Vst::IComponent::iid, reinterpret_cast<void**>(&comp)) != kResultOk || !comp)
        return fail("createInstance");
    if (comp->initialize(nullptr) != kResultOk) return fail("initialize");
    Vst::IAudioProcessor* proc = nullptr;
    if (comp->queryInterface(Vst::IAudioProcessor::iid, reinterpret_cast<void**>(&proc)) != kResultOk)
        return fail("IAudioProcessor");
    Vst::SpeakerArrangement st = Vst::SpeakerArr::kStereo;
    if (proc->setBusArrangements(&st, 1, &st, 1) != kResultTrue) return fail("setBusArrangements");
    Vst::ProcessSetup setup{Vst::kRealtime, Vst::kSample32, 512, 44100.0};
    if (proc->setupProcessing(setup) != kResultOk) return fail("setupProcessing");
    comp->setActive(true);
    proc->setProcessing(true);

    // Parâmetro de canal visível e automatizável.
    Vst::IEditController* ctrl = nullptr;
    if (comp->queryInterface(Vst::IEditController::iid, reinterpret_cast<void**>(&ctrl)) != kResultOk)
        return fail("IEditController");
    Vst::ParameterInfo pinfo{};
    if (ctrl->getParameterCount() != 1 || ctrl->getParameterInfo(0, pinfo) != kResultOk ||
        !(pinfo.flags & Vst::ParameterInfo::kCanAutomate) || pinfo.stepCount != 7)
        return fail("parâmetro Canal");

    // Janela do plugin presa numa janela nossa, como a DAW faz.
    IPlugView* view = ctrl->createView(Vst::ViewType::kEditor);
    if (!view) return fail("o plugin não tem janela");
    if (view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) return fail("HWND");
    ViewRect vr;
    view->getSize(&vr);
    HWND host = CreateWindowExW(0, L"STATIC", L"host", WS_POPUP, 0, 0, vr.getWidth(), vr.getHeight(),
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (view->attached(host, kPlatformTypeHWND) != kResultOk) return fail("attached");
    HWND child = FindWindowExW(host, nullptr, L"NovaonFocusSendView", nullptr);
    if (!child) return fail("janela do plugin não foi criada");
    auto pump = [] {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    };

    // Mixer com a fonte "send" no canal 1.
    const wchar_t* cfgPath = L"send_smoke.ini";
    {
        std::ofstream f(cfgPath);
        f << "[engine]\nlatency_ms = 20\n[source daw]\ntype = send\nchannel = 1\n[bus smoke]\ndaw = 1.0\n";
    }
    novaon::EngineConfig cfg;
    std::string err;
    if (!novaon::loadConfig(cfgPath, cfg, err)) return fail(err.c_str());
    novaon::Mixer mixer(cfg);
    if (!mixer.start(err)) return fail(err.c_str());

    // Toca 3 s de seno em tempo real, em blocos de 441 quadros (10 ms).
    std::vector<float> l(512), r(512), ol(512), orr(512);
    float* ins[2] = {l.data(), r.data()};
    float* outs[2] = {ol.data(), orr.data()};
    Vst::AudioBusBuffers inBus{}, outBus{};
    inBus.numChannels = outBus.numChannels = 2;
    inBus.channelBuffers32 = ins;
    outBus.channelBuffers32 = outs;
    Vst::ProcessData data{};
    data.processMode = Vst::kRealtime;
    data.symbolicSampleSize = Vst::kSample32;
    data.numSamples = 441;
    data.numInputs = data.numOutputs = 1;
    data.inputs = &inBus;
    data.outputs = &outBus;
    // Alimenta no ritmo do relógio real, como uma placa de som faria.
    LARGE_INTEGER freq, t0, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    uint64_t t = 0;
    for (;;) {
        QueryPerformanceCounter(&now);
        const double secs = double(now.QuadPart - t0.QuadPart) / double(freq.QuadPart);
        if (secs > 3.0) break;
        while (double(t) < secs * 44100.0) {
            for (int i = 0; i < 441; ++i) {
                const double ph = 2.0 * 3.14159265358979 * 1000.0 * double(t + uint64_t(i)) / 44100.0;
                l[size_t(i)] = r[size_t(i)] = 0.5f * float(std::sin(ph));
            }
            t += 441;
            if (proc->process(data) != kResultOk) return fail("process");
            if (ol[100] != l[100]) return fail("o plugin não deixou o som passar");
        }
        pump();
        Sleep(1);
    }

    // Confere o envio na memória compartilhada.
    HANDLE sendMap = OpenFileMappingW(FILE_MAP_READ, FALSE, novaon::sharedSendName(1).c_str());
    if (!sendMap) return fail("memória do envio não existe");
    auto* sh = static_cast<novaon::SharedBusHeader*>(MapViewOfFile(sendMap, FILE_MAP_READ, 0, 0, novaon::sharedBusBytes()));
    std::printf("envio: %u Hz, %llu quadros escritos\n", sh->sampleRate, (unsigned long long)sh->writeFrames.load());
    if (sh->sampleRate != 44100 || sh->writeFrames.load() != t) return fail("cabeçalho do envio");
    if (sh->readerTick == 0 || int32_t(GetTickCount() - sh->readerTick) > 1000) return fail("o app não marcou presença");

    // Confere o bus: pico perto de 0,5 e frequência perto de 1 kHz.
    HANDLE busMap = OpenFileMappingW(FILE_MAP_READ, FALSE, novaon::sharedBusName(L"smoke").c_str());
    if (!busMap) return fail("memória do bus não existe");
    auto* bh = static_cast<novaon::SharedBusHeader*>(MapViewOfFile(busMap, FILE_MAP_READ, 0, 0, novaon::sharedBusBytes()));
    const uint64_t w = bh->writeFrames.load();
    const float* bd = novaon::sharedBusData(bh);
    float peak = 0.0f;
    int crossings = 0;
    float prev = 0.0f;
    for (uint64_t i = w - 24000; i < w; ++i) { // últimos 500 ms
        const float v = bd[(i % novaon::kBusCapacityFrames) * 2];
        peak = std::max(peak, std::fabs(v));
        if (prev < 0.0f && v >= 0.0f) ++crossings;
        prev = v;
    }
    const auto stats = mixer.sourceStats();
    std::printf("bus: pico %.3f, %d ciclos em 500 ms, buffer da fonte %.1f ms\n", peak, crossings,
                stats[0].bufferedFrames * 1000.0 / 48000.0);
    // Foto da janela do plugin (vai como artefato do CI).
    {
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = vr.getWidth();
        bi.bmiHeader.biHeight = vr.getHeight();
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mem, bmp);
        SendMessageW(child, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem), PRF_CLIENT);
        GdiFlush();
        const DWORD imgBytes = DWORD(vr.getWidth() * vr.getHeight() * 4);
        BITMAPFILEHEADER fh{};
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
        fh.bfSize = fh.bfOffBits + imgBytes;
        std::ofstream img("send_view.bmp", std::ios::binary);
        img.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
        img.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
        img.write(static_cast<const char*>(bits), imgBytes);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }

    mixer.stop();
    view->removed();
    view->release();
    DestroyWindow(host);
    ctrl->release();
    proc->setProcessing(false);
    comp->setActive(false);
    comp->terminate();

    if (peak < 0.4f || peak > 0.6f) return fail("nível do seno no bus");
    if (crossings < 480 || crossings > 520) return fail("frequência do seno no bus");
    std::printf("OK\n");
    return 0;
}
