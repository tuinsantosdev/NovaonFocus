// NovaonFocus: roteador de áudio para a Scarlett 2i2 (3ª geração) e o OBS.
//
//   novaonfocus devices          lista entradas e saídas de áudio
//   novaonfocus apps             lista programas que estão tocando som
//   novaonfocus run [arquivo]    roda o roteamento (padrão: novaonfocus.ini)

#include "AudioDevices.h"
#include "Config.h"
#include "Mixer.h"

#include <objbase.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>

using namespace novaon;

namespace {

std::atomic<bool> g_quit{false};

BOOL WINAPI onCtrl(DWORD) {
    g_quit = true;
    return TRUE;
}

std::string u8(const std::wstring& w) { return wideToUtf8(w); }

float toDb(float v) { return v > 1e-6f ? 20.0f * std::log10(v) : -120.0f; }

int cmdDevices() {
    std::printf("Entradas (type = capture):\n");
    for (const auto& d : listDevices())
        if (d.isCapture) std::printf("  %s%s\n", u8(d.name).c_str(), d.isDefault ? "  [padrão]" : "");
    std::printf("\nSaídas (type = loopback):\n");
    for (const auto& d : listDevices())
        if (!d.isCapture) std::printf("  %s%s\n", u8(d.name).c_str(), d.isDefault ? "  [padrão]" : "");
    return 0;
}

int cmdApps() {
    std::printf("Programas com sessão de áudio (type = app, exe = ...):\n");
    std::set<std::wstring> seen;
    for (const auto& a : listAudioApps()) {
        if (a.exe.empty() || !seen.insert(a.exe + L"|" + a.device).second) continue;
        std::printf("  %-28s pid %-6u em %s\n", u8(a.exe).c_str(), a.pid, u8(a.device).c_str());
    }
    return 0;
}

void printStatus(Mixer& mixer) {
    std::printf("\nFontes:\n");
    for (const auto& s : mixer.sourceStats()) {
        std::printf("  %-12s %-11s buffer %5.1f ms  falhas %llu  descartes %llu  reconexões %llu\n",
                    s.name.c_str(), sourceStateName(s.state),
                    s.bufferedFrames * 1000.0 / kBusSampleRate,
                    (unsigned long long)s.underruns, (unsigned long long)s.drops,
                    (unsigned long long)s.reconnects);
    }
    std::printf("Barramentos (pico desde a última consulta):\n");
    for (const auto& b : mixer.busStats()) {
        std::printf("  %-12s L %6.1f dB  R %6.1f dB  clips %llu\n", b.name.c_str(),
                    toDb(b.peakL), toDb(b.peakR), (unsigned long long)b.clips);
    }
    std::printf("Atrasos do mixer (thread parada > 100 ms): %llu\n\n",
                (unsigned long long)mixer.lateTicks());
}

void printHelp() {
    std::printf(
        "Comandos:\n"
        "  status                       mostra fontes, barramentos e falhas\n"
        "  set <bus> <fonte> <ganho>    muda o volume (0 = mudo, 1 = normal)\n"
        "  quit                         encerra\n\n");
}

int cmdRun(const std::wstring& path) {
    EngineConfig cfg;
    std::string err;
    if (!loadConfig(path, cfg, err)) {
        std::fprintf(stderr, "Erro na configuração: %s\n", err.c_str());
        return 1;
    }
    Mixer mixer(cfg);
    if (!mixer.start(err)) {
        std::fprintf(stderr, "Erro ao iniciar: %s\n", err.c_str());
        return 1;
    }
    SetConsoleCtrlHandler(onCtrl, TRUE);

    std::printf("NovaonFocus rodando com %zu fontes e %zu barramentos (folga %d ms).\n",
                cfg.sources.size(), cfg.buses.size(), cfg.latencyMs);
    std::printf("No OBS, adicione a fonte \"NovaonFocus Bus\" e escolha o barramento.\n\n");
    printHelp();

    // Leitura de comandos numa thread separada; a principal vigia falhas.
    std::thread input([&] {
        std::string line;
        while (!g_quit && std::getline(std::cin, line)) {
            std::istringstream ss(line);
            std::string cmd;
            ss >> cmd;
            if (cmd == "quit" || cmd == "sair") {
                g_quit = true;
            } else if (cmd == "status") {
                printStatus(mixer);
            } else if (cmd == "set") {
                std::string bus, src;
                float g = -1.0f;
                ss >> bus >> src >> g;
                if (g < 0.0f || !mixer.setGain(bus, src, g))
                    std::printf("Não encontrei essa rota. Uso: set <bus> <fonte> <ganho>\n");
                else
                    std::printf("ok\n");
            } else if (!cmd.empty()) {
                printHelp();
            }
        }
        g_quit = true;
    });
    input.detach();

    uint64_t lastLate = 0, lastUnder = 0;
    while (!g_quit) {
        Sleep(1000);
        uint64_t under = 0;
        for (const auto& s : mixer.sourceStats()) under += s.underruns;
        const uint64_t late = mixer.lateTicks();
        if (late != lastLate)
            std::printf("[aviso] o mixer ficou parado mais de 100 ms. Rode o LatencyMon para achar o driver culpado.\n");
        if (under > lastUnder + 5)
            std::printf("[aviso] fontes sem dado a tempo. Considere subir latency_ms no [engine].\n");
        lastLate = late;
        lastUnder = under;
    }

    std::printf("Encerrando...\n");
    mixer.stop();
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    const std::wstring cmd = argc > 1 ? argv[1] : L"";
    int rc = 0;
    if (cmd == L"devices") {
        rc = cmdDevices();
    } else if (cmd == L"apps") {
        rc = cmdApps();
    } else if (cmd == L"run") {
        rc = cmdRun(argc > 2 ? argv[2] : L"novaonfocus.ini");
    } else {
        std::printf(
            "NovaonFocus: roteador de áudio para Scarlett 2i2 + OBS\n\n"
            "  novaonfocus devices          lista entradas e saídas\n"
            "  novaonfocus apps             lista programas tocando som\n"
            "  novaonfocus run [arquivo]    roda o roteamento (padrão novaonfocus.ini)\n");
        rc = cmd.empty() ? 0 : 1;
    }

    CoUninitialize();
    return rc;
}
