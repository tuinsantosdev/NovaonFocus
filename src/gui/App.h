#pragma once
// Interface gráfica do NovaonFocus (Dear ImGui). Edita o mesmo
// novaonfocus.ini do modo terminal e controla o Mixer direto.

#include "AudioDevices.h"
#include "Config.h"
#include "Mixer.h"
#include "imgui.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace novaon::ui {

class App {
public:
    App(std::wstring configPath, float scale);
    ~App();

    void frame();                 // desenha a janela inteira
    void onClose();               // grava pendências e para o motor

private:
    enum class Page { Mixer, Sources, Obs, Settings };

    // Uma célula da matriz barramento x fonte.
    struct Route {
        bool routed = false;
        bool muted = false;
        float gain = 1.0f; // linear, 0..4 (até +12 dB)
    };

    struct Meter {
        float db = -120.0f;   // nível exibido (com queda suave)
        float holdDb = -120.0f;
        float holdT = 0.0f;   // segundos restantes do pico segurado
        void feed(float peakLinear, float dt);
    };

    // Modelo e arquivo.
    void loadFromFile();
    void scheduleSave(bool structural);
    void saveNow();
    EngineConfig toConfig(bool fullMatrix) const;
    float effectiveGain(const Route& r) const { return r.routed && !r.muted ? r.gain : 0.0f; }
    void pushGain(size_t bus, size_t src);
    bool nameTaken(const std::string& name, bool bus, int ignore) const;

    // Motor.
    void startEngine();
    void stopEngine();
    bool running() const { return mixer_ != nullptr; }
    void pollStats(float dt);
    void refreshHardware();

    // Telas.
    void drawTopBar(float h);
    void drawSidebar(float w);
    void drawBottomBar(float h);
    void drawMixerPage();
    void drawSourcesPage();
    void drawObsPage();
    void drawSettingsPage();
    void drawSourceEditor();
    void drawBusEditor();
    void drawStrip(size_t bus, size_t src, float w, float h);
    void drawBusOutput(size_t bus, float w, float h);

    // Componentes.
    float S(float v) const { return v * scale_; }
    bool sectionHeader(const char* num, const char* title, const char* link = nullptr);
    bool pill(const char* id, const char* title, const char* sub, bool selected, float w, float h);
    bool linkButton(const char* label);
    void spacedText(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 color, ImFont* font,
                    float spacing) const;
    float spacedWidth(const char* text, ImFont* font, float spacing) const;
    bool fader(const char* id, float& gain, float w, float h, bool dim);
    void meterBar(ImVec2 p0, ImVec2 p1, const Meter& m, bool vertical) const;
    void stateDot(ImVec2 center, SourceState st, bool exists) const;
    std::string sourceSubtitle(const SourceConfig& s) const;

    void openSourceEditor(int index);
    void openBusEditor(int index);

    std::wstring configPath_;
    float scale_;
    Page page_ = Page::Mixer;
    size_t selBus_ = 0;

    EngineConfig cfg_;                       // fontes, folga e nomes dos barramentos
    std::vector<std::vector<Route>> routes_; // [bus][fonte]

    std::unique_ptr<Mixer> mixer_;
    EngineConfig engineCfg_; // configuração com que o motor está rodando
    bool needsRestart_ = false;

    bool dirty_ = false;
    std::chrono::steady_clock::time_point saveAt_{};

    std::map<std::string, SourceStats> srcStats_;
    std::map<std::string, BusStats> busStats_;
    std::map<std::string, Meter> srcMeters_;
    std::map<std::string, Meter> busMetersL_, busMetersR_;
    uint64_t lateTicks_ = 0, lateSeen_ = 0;
    float lateFlash_ = 0.0f;

    std::vector<DeviceInfo> devices_;
    std::vector<AudioAppInfo> apps_;
    bool scarlettPresent_ = false;
    float hwTimer_ = 0.0f;

    std::string status_;     // mensagem na barra de baixo
    bool statusIsError_ = false;
    float statusT_ = 0.0f;

    // Editor de fonte.
    int editSrc_ = -2; // -2 fechado, -1 nova, >=0 índice
    SourceConfig editCfg_;
    char editName_[64] = {};
    char editExe_[128] = {};
    char editDevice_[256] = {};

    // Editor de barramento.
    int editBus_ = -2;
    char editBusName_[64] = {};

    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

} // namespace novaon::ui
