#include "App.h"
#include "Theme.h"

#include "imgui_internal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <set>

namespace novaon::ui {

namespace {

constexpr float kMinDb = -60.0f;
constexpr float kMaxDb = 12.0f;
constexpr const char* kVersion = "v0.2.0";

float toDb(float v) { return v > 1e-6f ? 20.0f * std::log10(v) : -120.0f; }
float fromDb(float db) { return db <= kMinDb ? 0.0f : std::pow(10.0f, db / 20.0f); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return c < 128 ? char(std::toupper(c)) : char(c); });
    return s;
}

// Nomes vão para o .ini e para o nome da memória compartilhada: sem espaços.
std::string sanitizeName(const char* in) {
    std::string out;
    for (const char* p = in; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (std::isalnum(c) || c == '_' || c == '-') out += char(c);
        else if (c == ' ') out += '_';
    }
    return out;
}

void copyTo(char* dst, size_t n, const std::string& s) {
    std::snprintf(dst, n, "%s", s.c_str());
}

const char* typeLabel(SourceType t) {
    switch (t) {
    case SourceType::Capture: return "Entrada (microfone ou instrumento)";
    case SourceType::Loopback: return "Tudo o que toca numa saída";
    case SourceType::App: return "Só um programa";
    case SourceType::AppExclude: return "Tudo, menos um programa";
    }
    return "";
}

// Ícones simples desenhados na mão (sem fonte de ícones).
void drawIcon(ImDrawList* dl, int kind, ImVec2 c, float s, ImU32 color) {
    const float t = std::max(1.5f, s * 0.09f);
    switch (kind) {
    case 0: // mixer: três faders
        for (int i = -1; i <= 1; ++i) {
            const float x = c.x + i * s * 0.32f;
            dl->AddLine(ImVec2(x, c.y - s * 0.42f), ImVec2(x, c.y + s * 0.42f), color, t);
            const float ky = c.y + (i == 0 ? -0.18f : i < 0 ? 0.15f : 0.02f) * s;
            dl->AddRectFilled(ImVec2(x - s * 0.12f, ky - s * 0.07f),
                              ImVec2(x + s * 0.12f, ky + s * 0.07f), color, 2.0f);
        }
        break;
    case 1: // fontes: microfone
        dl->AddRect(ImVec2(c.x - s * 0.15f, c.y - s * 0.45f), ImVec2(c.x + s * 0.15f, c.y + s * 0.08f),
                    color, s * 0.15f, 0, t);
        dl->PathArcTo(ImVec2(c.x, c.y - s * 0.05f), s * 0.3f, 0.0f, IM_PI, 16);
        dl->PathStroke(color, 0, t);
        dl->AddLine(ImVec2(c.x, c.y + s * 0.25f), ImVec2(c.x, c.y + s * 0.42f), color, t);
        dl->AddLine(ImVec2(c.x - s * 0.15f, c.y + s * 0.42f), ImVec2(c.x + s * 0.15f, c.y + s * 0.42f),
                    color, t);
        break;
    case 2: // OBS: transmissão
        dl->AddCircleFilled(c, s * 0.09f, color);
        dl->PathArcTo(c, s * 0.25f, -IM_PI * 0.25f, IM_PI * 0.25f, 10);
        dl->PathStroke(color, 0, t);
        dl->PathArcTo(c, s * 0.25f, IM_PI * 0.75f, IM_PI * 1.25f, 10);
        dl->PathStroke(color, 0, t);
        dl->PathArcTo(c, s * 0.43f, -IM_PI * 0.25f, IM_PI * 0.25f, 12);
        dl->PathStroke(color, 0, t);
        dl->PathArcTo(c, s * 0.43f, IM_PI * 0.75f, IM_PI * 1.25f, 12);
        dl->PathStroke(color, 0, t);
        break;
    case 4: // programa: janela
        dl->AddRect(ImVec2(c.x - s * 0.42f, c.y - s * 0.34f), ImVec2(c.x + s * 0.42f, c.y + s * 0.34f), color,
                    s * 0.08f, 0, t);
        dl->AddLine(ImVec2(c.x - s * 0.42f, c.y - s * 0.14f), ImVec2(c.x + s * 0.42f, c.y - s * 0.14f), color, t);
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.1f, c.y - s * 0.02f), ImVec2(c.x - s * 0.1f, c.y + s * 0.24f),
                              ImVec2(c.x + s * 0.14f, c.y + s * 0.11f), color);
        break;
    case 5: // saída: alto-falante
        dl->AddRectFilled(ImVec2(c.x - s * 0.4f, c.y - s * 0.12f), ImVec2(c.x - s * 0.2f, c.y + s * 0.12f), color);
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.22f, c.y), ImVec2(c.x + s * 0.06f, c.y - s * 0.34f),
                              ImVec2(c.x + s * 0.06f, c.y + s * 0.34f), color);
        dl->PathArcTo(ImVec2(c.x + s * 0.06f, c.y), s * 0.32f, -IM_PI * 0.3f, IM_PI * 0.3f, 10);
        dl->PathStroke(color, 0, t);
        break;
    case 3: // ajustes: engrenagem
        dl->AddCircle(c, s * 0.17f, color, 16, t);
        for (int i = 0; i < 8; ++i) {
            const float a = i * IM_PI / 4.0f;
            dl->AddLine(ImVec2(c.x + std::cos(a) * s * 0.28f, c.y + std::sin(a) * s * 0.28f),
                        ImVec2(c.x + std::cos(a) * s * 0.42f, c.y + std::sin(a) * s * 0.42f), color, t);
        }
        break;
    }
}

} // namespace

void App::Meter::feed(float peakLinear, float dt) {
    const float in = toDb(peakLinear);
    db = std::max(in, db - 28.0f * dt); // cai 28 dB por segundo
    if (in >= holdDb) {
        holdDb = in;
        holdT = 1.5f;
    } else if ((holdT -= dt) <= 0.0f) {
        holdDb = std::max(db, holdDb - 40.0f * dt);
    }
}

// ---------------------------------------------------------------- modelo

App::App(std::wstring configPath, float scale) : configPath_(std::move(configPath)), scale_(scale) {
    refreshHardware();
    loadFromFile();
    if (!statusIsError_) startEngine();
}

App::~App() { onClose(); }

void App::onClose() {
    if (dirty_) saveNow();
    stopEngine();
}

void App::loadFromFile() {
    EngineConfig cfg;
    std::string err;
    if (!loadConfig(configPath_, cfg, err)) {
        // Sem arquivo (ou inválido): começa com o microfone da Scarlett num bus "live".
        cfg = EngineConfig{};
        SourceConfig mic;
        mic.name = "mic";
        mic.type = SourceType::Capture;
        mic.device = scarlettPresent_ ? L"Focusrite" : L"default";
        mic.inputChannel = 1;
        cfg.sources.push_back(mic);
        BusConfig live;
        live.name = "live";
        live.gains.emplace_back("mic", 1.0f);
        cfg.buses.push_back(live);
        status_ = "Configuração não carregada (" + err + "). Comecei uma nova; ela será gravada ao mudar algo.";
        statusIsError_ = true;
        statusT_ = 12.0f;
    }

    cfg_ = cfg;
    routes_.assign(cfg_.buses.size(), std::vector<Route>(cfg_.sources.size()));
    for (size_t b = 0; b < cfg_.buses.size(); ++b) {
        for (const auto& [src, g] : cfg_.buses[b].gains) {
            for (size_t s = 0; s < cfg_.sources.size(); ++s) {
                if (lower(cfg_.sources[s].name) != src) continue;
                Route& r = routes_[b][s];
                r.routed = true;
                // Ganho 0 no arquivo = mudo; o fader volta para 0 dB ao desmutar.
                r.muted = g <= 0.0f;
                r.gain = g > 0.0f ? g : 1.0f;
            }
        }
        cfg_.buses[b].gains.clear(); // a verdade passa a ser routes_
    }
    selBus_ = 0;
}

EngineConfig App::toConfig(bool fullMatrix) const {
    EngineConfig out;
    out.latencyMs = cfg_.latencyMs;
    out.sources = cfg_.sources;
    for (size_t b = 0; b < cfg_.buses.size(); ++b) {
        BusConfig bc;
        bc.name = cfg_.buses[b].name;
        for (size_t s = 0; s < cfg_.sources.size(); ++s) {
            const Route& r = routes_[b][s];
            // O motor recebe a matriz inteira (ganho 0 onde não há rota), para
            // que ligar uma rota nova funcione na hora, sem reiniciar.
            if (r.routed || fullMatrix)
                bc.gains.emplace_back(lower(cfg_.sources[s].name), effectiveGain(r));
        }
        out.buses.push_back(bc);
    }
    return out;
}

void App::scheduleSave(bool structural) {
    dirty_ = true;
    saveAt_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
    if (structural && running()) needsRestart_ = true;
}

void App::saveNow() {
    std::string err;
    if (saveConfig(configPath_, toConfig(false), err)) {
        dirty_ = false;
    } else {
        status_ = err;
        statusIsError_ = true;
        statusT_ = 8.0f;
        saveAt_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    }
}

void App::pushGain(size_t bus, size_t src) {
    if (mixer_)
        mixer_->setGain(cfg_.buses[bus].name, cfg_.sources[src].name, effectiveGain(routes_[bus][src]));
    scheduleSave(false);
}

bool App::nameTaken(const std::string& name, bool bus, int ignore) const {
    const std::string n = lower(name);
    if (bus) {
        for (size_t i = 0; i < cfg_.buses.size(); ++i)
            if (int(i) != ignore && lower(cfg_.buses[i].name) == n) return true;
    } else {
        for (size_t i = 0; i < cfg_.sources.size(); ++i)
            if (int(i) != ignore && lower(cfg_.sources[i].name) == n) return true;
    }
    return false;
}

// ----------------------------------------------------------------- motor

void App::startEngine() {
    stopEngine();
    engineCfg_ = toConfig(true);
    auto m = std::make_unique<Mixer>(engineCfg_);
    std::string err;
    if (!m->start(err)) {
        status_ = "Não consegui iniciar: " + err;
        statusIsError_ = true;
        statusT_ = 10.0f;
        return;
    }
    mixer_ = std::move(m);
    needsRestart_ = false;
    lateSeen_ = lateTicks_ = 0;
    status_ = "Motor iniciado.";
    statusIsError_ = false;
    statusT_ = 3.0f;
}

void App::stopEngine() {
    if (!mixer_) return;
    mixer_->stop();
    mixer_.reset();
    srcStats_.clear();
    busStats_.clear();
    needsRestart_ = false;
}

void App::pollStats(float dt) {
    if (mixer_) {
        for (auto& s : mixer_->sourceStats()) srcStats_[lower(s.name)] = s;
        for (auto& b : mixer_->busStats()) busStats_[lower(b.name)] = b;
        lateTicks_ = mixer_->lateTicks();
        if (lateTicks_ != lateSeen_) {
            lateSeen_ = lateTicks_;
            lateFlash_ = 6.0f;
        }
    }
    for (const auto& s : cfg_.sources) {
        auto it = srcStats_.find(lower(s.name));
        srcMeters_[lower(s.name)].feed(it != srcStats_.end() && mixer_ ? it->second.peak : 0.0f, dt);
    }
    for (const auto& b : cfg_.buses) {
        auto it = busStats_.find(lower(b.name));
        const bool ok = it != busStats_.end() && mixer_;
        busMetersL_[lower(b.name)].feed(ok ? it->second.peakL : 0.0f, dt);
        busMetersR_[lower(b.name)].feed(ok ? it->second.peakR : 0.0f, dt);
    }
    lateFlash_ = std::max(0.0f, lateFlash_ - dt);
}

void App::refreshHardware() {
    devices_ = listDevices();
    scarlettPresent_ = std::any_of(devices_.begin(), devices_.end(), [](const DeviceInfo& d) {
        const std::string n = lower(wideToUtf8(d.name));
        return n.find("focusrite") != std::string::npos || n.find("scarlett") != std::string::npos;
    });
}

// ------------------------------------------------------------ componentes

float App::spacedWidth(const char* text, ImFont* font, float spacing) const {
    float w = 0.0f;
    int n = 0;
    for (const char* p = text; *p;) {
        unsigned int c = 0;
        p += ImTextCharFromUtf8(&c, p, nullptr);
        w += font->GetCharAdvance(ImWchar(c));
        ++n;
    }
    return w + spacing * std::max(0, n - 1);
}

void App::spacedText(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 color, ImFont* font,
                     float spacing) const {
    char buf[8];
    for (const char* p = text; *p;) {
        unsigned int c = 0;
        const int len = ImTextCharFromUtf8(&c, p, nullptr);
        std::snprintf(buf, sizeof(buf), "%.*s", len, p);
        dl->AddText(font, font->FontSize, pos, color, buf);
        pos.x += font->GetCharAdvance(ImWchar(c)) + spacing;
        p += len;
    }
}

// "01  TÍTULO  ─────────────  LINK ›"
bool App::sectionHeader(const char* num, const char* title, const char* link) {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* f = fonts().bold;
    const float sp = S(1.6f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = f->FontSize;

    spacedText(dl, pos, num, col(p.accent), f, sp);
    const float x1 = pos.x + spacedWidth(num, f, sp) + S(14);
    spacedText(dl, ImVec2(x1, pos.y), title, col(p.textDim), f, sp);
    float lineEnd = pos.x + w;
    bool clicked = false;
    if (link) {
        std::string l = std::string(link) + "  \xE2\x80\xBA";
        const float lw = spacedWidth(l.c_str(), f, sp);
        const ImVec2 lp(pos.x + w - lw, pos.y);
        ImGui::SetCursorScreenPos(lp);
        ImGui::PushID(title);
        clicked = ImGui::InvisibleButton("link", ImVec2(lw, h));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        spacedText(dl, lp, l.c_str(), col(hov ? p.accentHi : p.accent), f, sp);
        lineEnd = lp.x - S(16);
    }
    const float lineStart = x1 + spacedWidth(title, f, sp) + S(16);
    if (lineEnd > lineStart)
        dl->AddLine(ImVec2(lineStart, pos.y + h * 0.55f), ImVec2(lineEnd, pos.y + h * 0.55f),
                    col(p.border), 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + S(14)));
    ImGui::Dummy(ImVec2(0, 0));
    return clicked;
}

// Botão grande de duas linhas (como os formatos do NovaOndaPirate).
bool App::pill(const char* id, const char* title, const char* sub, bool selected, float w, float h) {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 b(a.x + w, a.y + h);
    const float r = S(12);
    if (selected) {
        dl->AddRectFilled(a, b, col(hov ? p.accentHi : p.accent), r);
    } else {
        dl->AddRectFilled(a, b, col(hov ? p.panelHi : p.bg), r);
        dl->AddRect(a, b, col(p.border), r, 0, 1.0f);
    }
    ImFont* fb = fonts().bold;
    ImFont* fs = fonts().small;
    const float sp1 = S(0.8f), sp2 = S(1.4f);
    const float tw = spacedWidth(title, fb, sp1);
    const float sw = sub ? spacedWidth(sub, fs, sp2) : 0.0f;
    const float total = fb->FontSize + (sub ? fs->FontSize : 0.0f);
    float y = a.y + (h - total) * 0.5f;
    const ImU32 tc = selected ? col(ImVec4(0.06f, 0.04f, 0.1f, 1)) : col(p.text);
    spacedText(dl, ImVec2(a.x + (w - tw) * 0.5f, y), title, tc, fb, sp1);
    if (sub) {
        y += fb->FontSize;
        spacedText(dl, ImVec2(a.x + (w - sw) * 0.5f, y), sub,
                   selected ? col(ImVec4(0.06f, 0.04f, 0.1f, 0.75f)) : col(p.textDim), fs, sp2);
    }
    return clicked;
}

bool App::linkButton(const char* label) {
    const Palette& p = palette();
    ImFont* f = fonts().bold;
    const float sp = S(1.4f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = spacedWidth(label, f, sp);
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("lnk", ImVec2(w, f->FontSize));
    ImGui::PopID();
    const bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    spacedText(ImGui::GetWindowDrawList(), pos, label, col(hov ? p.accentHi : p.accent), f, sp);
    return clicked;
}

// Fader vertical em dB (-inf..+12). Duplo clique volta para 0 dB; a roda
// do mouse anda 1 dB.
bool App::fader(const char* id, float& gain, float w, float h, bool dim) {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    const float pad = S(10);
    const float top = a.y + pad, bot = a.y + h - pad;

    auto dbToY = [&](float db) {
        const float t = (std::clamp(db, kMinDb, kMaxDb) - kMinDb) / (kMaxDb - kMinDb);
        return bot - t * (bot - top);
    };
    bool changed = false;
    float db = gain > 0.0f ? toDb(gain) : kMinDb;
    if (act) {
        const float t = std::clamp((bot - ImGui::GetIO().MousePos.y) / (bot - top), 0.0f, 1.0f);
        float nd = kMinDb + t * (kMaxDb - kMinDb);
        if (std::fabs(nd) < 0.6f) nd = 0.0f; // imã no 0 dB
        db = nd;
        changed = true;
    }
    if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        db = 0.0f;
        changed = true;
    }
    if (hov && ImGui::GetIO().MouseWheel != 0.0f) {
        db = std::clamp(std::round(db + ImGui::GetIO().MouseWheel), kMinDb, kMaxDb);
        changed = true;
    }
    if (hov || act) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    const float cx = a.x + w * 0.5f;
    // Marcas de escala.
    for (float m : {12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f, -60.0f}) {
        const float y = dbToY(m);
        const float len = m == 0.0f ? S(9) : S(5);
        dl->AddLine(ImVec2(a.x + S(2), y), ImVec2(a.x + S(2) + len, y),
                    col(m == 0.0f ? p.textDim : p.border), 1.0f);
    }
    // Trilho e parte preenchida.
    const float tw = S(4);
    dl->AddRectFilled(ImVec2(cx - tw * 0.5f, top), ImVec2(cx + tw * 0.5f, bot), col(p.border), tw);
    const float y = dbToY(db);
    dl->AddRectFilled(ImVec2(cx - tw * 0.5f, y), ImVec2(cx + tw * 0.5f, bot),
                      col(dim ? p.textDim : p.accent, dim ? 0.5f : 1.0f), tw);
    // Botão.
    const float kw = w * 0.42f, kh = S(14);
    dl->AddRectFilled(ImVec2(cx - kw, y - kh * 0.5f), ImVec2(cx + kw, y + kh * 0.5f),
                      col(act ? p.accentHi : hov ? p.text : ImVec4(0.85f, 0.85f, 0.9f, 1), dim ? 0.55f : 1.0f),
                      S(5));
    dl->AddLine(ImVec2(cx - kw * 0.55f, y), ImVec2(cx + kw * 0.55f, y), col(p.bg), S(1.5f));

    if (changed) gain = fromDb(db);
    return changed;
}

void App::meterBar(ImVec2 a, ImVec2 b, const Meter& m, bool vertical) const {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, col(p.bg), S(3));
    auto frac = [](float db) { return std::clamp((db - kMinDb) / (0.0f - kMinDb), 0.0f, 1.0f); };
    const float f = frac(m.db), hf = frac(m.holdDb);
    const float warnF = frac(-18.0f), dangF = frac(-6.0f);
    auto seg = [&](float f0, float f1, const ImVec4& c) {
        if (f1 <= f0) return;
        if (vertical) {
            const float h = b.y - a.y;
            dl->AddRectFilled(ImVec2(a.x, b.y - f1 * h), ImVec2(b.x, b.y - f0 * h), col(c));
        } else {
            const float w = b.x - a.x;
            dl->AddRectFilled(ImVec2(a.x + f0 * w, a.y), ImVec2(a.x + f1 * w, b.y), col(c));
        }
    };
    seg(0.0f, std::min(f, warnF), p.accent);
    seg(warnF, std::min(f, dangF), p.warn);
    seg(dangF, f, p.danger);
    if (m.holdDb > kMinDb) {
        const ImVec4& hc = m.holdDb > -6.0f ? p.danger : m.holdDb > -18.0f ? p.warn : p.accentHi;
        if (vertical) {
            const float y = b.y - hf * (b.y - a.y);
            dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col(hc), S(2));
        } else {
            const float x = a.x + hf * (b.x - a.x);
            dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), col(hc), S(2));
        }
    }
}

void App::stateDot(ImVec2 c, SourceState st, bool exists) const {
    const Palette& p = palette();
    ImVec4 color = p.textDim;
    if (exists) {
        switch (st) {
        case SourceState::Running: color = p.ok; break;
        case SourceState::Connecting: color = p.warn; break;
        case SourceState::Waiting: color = p.warn; break;
        case SourceState::Stopped: color = p.textDim; break;
        }
    }
    ImGui::GetWindowDrawList()->AddCircleFilled(c, S(4), col(color));
}

std::string App::sourceSubtitle(const SourceConfig& s) const {
    std::string d;
    switch (s.type) {
    case SourceType::Capture:
        d = s.inputChannel == 1 ? "ENTRADA 1" : s.inputChannel == 2 ? "ENTRADA 2" : "ENTRADAS 1+2";
        break;
    case SourceType::Loopback: d = "SAÍDA INTEIRA"; break;
    case SourceType::App: d = "PROGRAMA"; break;
    case SourceType::AppExclude: d = "TUDO MENOS"; break;
    }
    const std::wstring& what =
        (s.type == SourceType::App || s.type == SourceType::AppExclude) ? s.exe : s.device;
    std::string w = wideToUtf8(what);
    if (w == "default") w = "padrão do Windows";
    return d + " \xC2\xB7 " + w;
}

// -------------------------------------------------------------- moldura

void App::frame() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - last_).count(), 0.0f, 0.25f);
    last_ = now;

    pollStats(dt);
    if ((hwTimer_ -= dt) <= 0.0f) {
        hwTimer_ = 5.0f;
        refreshHardware();
    }
    if (dirty_ && now >= saveAt_) saveNow();
    if (statusT_ > 0.0f) statusT_ -= dt;
    if (selBus_ >= cfg_.buses.size()) selBus_ = 0;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("NovaonFocus", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);
    ImGui::PushFont(fonts().body);

    const float topH = S(64), botH = S(84), sideW = S(104);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float W = vp->WorkSize.x, H = vp->WorkSize.y;

    ImGui::SetCursorScreenPos(origin);
    drawTopBar(topH);

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + topH));
    drawSidebar(sideW);

    // Área das páginas.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + sideW, origin.y + topH));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(22)));
    ImGui::BeginChild("page", ImVec2(W - sideW, H - topH - botH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(8)));
    switch (page_) {
    case Page::Mixer: drawMixerPage(); break;
    case Page::Sources: drawSourcesPage(); break;
    case Page::Obs: drawObsPage(); break;
    case Page::Settings: drawSettingsPage(); break;
    }
    drawSourceEditor();
    drawBusEditor();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + H - botH));
    drawBottomBar(botH);

    ImGui::PopFont();
    ImGui::End();
}

void App::drawTopBar(float h) {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float W = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(a, ImVec2(a.x + W, a.y + h), col(p.bg));
    dl->AddLine(ImVec2(a.x, a.y + h - 1), ImVec2(a.x + W, a.y + h - 1), col(p.border));

    // Logo.
    const float ls = S(38);
    const ImVec2 l0(a.x + S(22), a.y + (h - ls) * 0.5f);
    dl->AddRectFilled(l0, ImVec2(l0.x + ls, l0.y + ls), col(p.accent), S(9));
    ImFont* ft = fonts().title;
    const ImVec2 ns = ft->CalcTextSizeA(ft->FontSize, FLT_MAX, 0.0f, "N");
    dl->AddText(ft, ft->FontSize, ImVec2(l0.x + (ls - ns.x) * 0.5f, l0.y + (ls - ns.y) * 0.5f),
                col(ImVec4(0.06f, 0.04f, 0.1f, 1)), "N");
    ImFont* fb = fonts().bold;
    const float ty = a.y + (h - fb->FontSize * 1.15f) * 0.5f;
    const ImVec2 t1(l0.x + ls + S(14), ty);
    const ImVec2 w1 = fb->CalcTextSizeA(fb->FontSize * 1.15f, FLT_MAX, 0.0f, "Novaon");
    dl->AddText(fb, fb->FontSize * 1.15f, t1, col(p.text), "Novaon");
    dl->AddText(fb, fb->FontSize * 1.15f, ImVec2(t1.x + w1.x, t1.y), col(p.accent), "Focus");

    // Relógio no centro.
    std::time_t tt = std::time(nullptr);
    const std::tm lt = *std::localtime(&tt);
    char clock[8];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", lt.tm_hour, lt.tm_min);
    ImFont* fc = fonts().title;
    const float cs = fc->FontSize * 1.25f;
    const ImVec2 cz = fc->CalcTextSizeA(cs, FLT_MAX, 0.0f, clock);
    dl->AddText(fc, cs, ImVec2(a.x + (W - cz.x) * 0.5f, a.y + (h - cz.y) * 0.5f), col(p.text), clock);

    // Selos à direita.
    ImFont* fs = fonts().small;
    const float sp = S(1.4f);
    float x = a.x + W - S(22);
    const ImVec2 vz = fs->CalcTextSizeA(fs->FontSize, FLT_MAX, 0.0f, kVersion);
    x -= vz.x;
    dl->AddText(fs, fs->FontSize, ImVec2(x, a.y + (h - vz.y) * 0.5f), col(p.textDim), kVersion);
    auto badge = [&](const char* text, const ImVec4& dot) {
        const float tw = spacedWidth(text, fb, sp * 0.6f);
        const float bw = tw + S(42), bh = S(30);
        x -= bw + S(12);
        const ImVec2 b0(x, a.y + (h - bh) * 0.5f);
        dl->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), col(p.border), bh * 0.5f, 0, 1.0f);
        dl->AddCircleFilled(ImVec2(b0.x + S(16), b0.y + bh * 0.5f), S(3.5f), col(dot));
        spacedText(dl, ImVec2(b0.x + S(27), b0.y + (bh - fb->FontSize) * 0.5f), text, col(p.text), fb,
                   sp * 0.6f);
    };
    badge(running() ? "MOTOR ATIVO" : "MOTOR PARADO", running() ? p.accent : p.textDim);
    badge(scarlettPresent_ ? "SCARLETT OK" : "SCARLETT AUSENTE", scarlettPresent_ ? p.ok : p.danger);

    ImGui::Dummy(ImVec2(W, h));
}

void App::drawSidebar(float w) {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float H = ImGui::GetMainViewport()->WorkSize.y - S(64) - S(84);
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + H), col(p.bg));
    dl->AddLine(ImVec2(a.x + w - 1, a.y), ImVec2(a.x + w - 1, a.y + H), col(p.border));

    struct Item { Page page; int icon; const char* label; };
    const Item items[] = {
        {Page::Mixer, 0, "MIXER"},
        {Page::Sources, 1, "FONTES"},
        {Page::Obs, 2, "OBS"},
        {Page::Settings, 3, "AJUSTES"},
    };
    ImFont* fs = fonts().small;
    const float sp = S(1.3f);
    float y = a.y + S(18);
    for (const Item& it : items) {
        const ImVec2 b0(a.x + S(12), y), b1(a.x + w - S(12), y + S(76));
        ImGui::SetCursorScreenPos(b0);
        ImGui::PushID(it.label);
        if (ImGui::InvisibleButton("nav", ImVec2(b1.x - b0.x, b1.y - b0.y))) page_ = it.page;
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const bool sel = page_ == it.page;
        if (sel) {
            dl->AddRectFilled(b0, b1, col(p.accent, 0.16f), S(12));
            dl->AddRect(b0, b1, col(p.accent, 0.8f), S(12), 0, 1.0f);
        } else if (hov) {
            dl->AddRectFilled(b0, b1, col(p.panelHi), S(12));
        }
        const ImU32 c = col(sel ? p.text : p.textDim);
        drawIcon(dl, it.icon, ImVec2((b0.x + b1.x) * 0.5f, b0.y + S(28)), S(24), c);
        const float lw = spacedWidth(it.label, fs, sp);
        spacedText(dl, ImVec2((b0.x + b1.x - lw) * 0.5f, b0.y + S(50)), it.label, c, fs, sp);
        y += S(86);
    }
    ImGui::SetCursorScreenPos(a);
    ImGui::Dummy(ImVec2(w, H));
}

void App::drawBottomBar(float h) {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float W = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(a, ImVec2(a.x + W, a.y + h), col(p.bg));
    dl->AddLine(ImVec2(a.x, a.y), ImVec2(a.x + W, a.y), col(p.border));

    // Botão redondo de ligar/desligar.
    const float r = S(24);
    const ImVec2 c(a.x + S(22) + r, a.y + h * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    if (ImGui::InvisibleButton("power", ImVec2(r * 2, r * 2))) {
        if (running()) stopEngine();
        else startEngine();
    }
    const bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip(running() ? "Parar o roteamento" : "Iniciar o roteamento");
    }
    if (running()) {
        dl->AddCircleFilled(c, r, col(hov ? p.accentHi : p.accent), 40);
        dl->AddRectFilled(ImVec2(c.x - S(7), c.y - S(7)), ImVec2(c.x + S(7), c.y + S(7)),
                          col(ImVec4(0.06f, 0.04f, 0.1f, 1)), S(2));
    } else {
        dl->AddCircle(c, r - 1, col(hov ? p.accentHi : p.accent), 40, S(2));
        dl->AddTriangleFilled(ImVec2(c.x - S(5), c.y - S(9)), ImVec2(c.x - S(5), c.y + S(9)),
                              ImVec2(c.x + S(9), c.y), col(hov ? p.accentHi : p.accent));
    }

    // Texto de estado.
    ImFont* fb = fonts().bold;
    ImFont* fs = fonts().small;
    const float tx = c.x + r + S(16);
    char sub[128];
    std::snprintf(sub, sizeof(sub), "%zu fontes \xC2\xB7 %zu barramentos \xC2\xB7 folga %d ms",
                  cfg_.sources.size(), cfg_.buses.size(), cfg_.latencyMs);
    dl->AddText(fb, fb->FontSize, ImVec2(tx, c.y - fb->FontSize), col(p.text),
                running() ? "Roteamento ativo" : "Roteamento parado");
    dl->AddText(fs, fs->FontSize, ImVec2(tx, c.y + S(2)), col(p.textDim), sub);

    // Aviso de reinício ou mensagem.
    float mx = tx + S(320);
    if (needsRestart_) {
        const char* label = "APLICAR MUDANÇAS";
        const float bw = spacedWidth(label, fb, S(1.2f)) + S(36), bh = S(36);
        const ImVec2 b0(mx, c.y - bh * 0.5f);
        ImGui::SetCursorScreenPos(b0);
        if (ImGui::InvisibleButton("apply", ImVec2(bw, bh))) {
            if (dirty_) saveNow();
            startEngine();
        }
        const bool h2 = ImGui::IsItemHovered();
        if (h2) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Reinicia o motor com as fontes novas (corte de ~1 s no áudio)");
        }
        dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), col(p.accent, h2 ? 0.35f : 0.2f), bh * 0.5f);
        dl->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), col(p.accent), bh * 0.5f, 0, 1.0f);
        spacedText(dl, ImVec2(b0.x + S(18), c.y - fb->FontSize * 0.5f), label, col(p.accentHi), fb,
                   S(1.2f));
        mx += bw + S(16);
    }
    std::string msg;
    ImVec4 mc = p.textDim;
    if (lateFlash_ > 0.0f) {
        msg = "O Windows travou o áudio por mais de 100 ms. Veja AJUSTES > DIAGNÓSTICO.";
        mc = p.warn;
    } else if (statusT_ > 0.0f && !status_.empty()) {
        msg = status_;
        mc = statusIsError_ ? p.danger : p.textDim;
    }
    if (!msg.empty()) {
        ImGui::PushClipRect(ImVec2(mx, a.y), ImVec2(a.x + W - S(300), a.y + h), true);
        dl->AddText(fs, fs->FontSize, ImVec2(mx, c.y - fs->FontSize * 0.5f), col(mc), msg.c_str());
        ImGui::PopClipRect();
    }

    // Contador de falhas e medidor do barramento selecionado.
    uint64_t falhas = 0;
    for (const auto& [n, s] : srcStats_) falhas += s.underruns;
    char ftxt[48];
    std::snprintf(ftxt, sizeof(ftxt), "%llu", (unsigned long long)falhas);
    const float bx = a.x + W - S(22) - S(150);
    const float lw = spacedWidth("FALHAS", fb, S(1.6f));
    const float nx = bx - S(18) - lw - S(10) - fb->CalcTextSizeA(fb->FontSize, FLT_MAX, 0, ftxt).x;
    dl->AddText(fb, fb->FontSize, ImVec2(nx, c.y - fb->FontSize * 0.5f),
                col(falhas ? p.warn : p.accent), ftxt);
    spacedText(dl, ImVec2(bx - S(18) - lw, c.y - fb->FontSize * 0.5f), "FALHAS", col(p.textDim), fb,
               S(1.6f));
    if (!cfg_.buses.empty()) {
        const std::string key = lower(cfg_.buses[selBus_].name);
        meterBar(ImVec2(bx, c.y - S(5)), ImVec2(bx + S(150), c.y - S(1)), busMetersL_[key], false);
        meterBar(ImVec2(bx, c.y + S(1)), ImVec2(bx + S(150), c.y + S(5)), busMetersR_[key], false);
    }

    ImGui::SetCursorScreenPos(a);
    ImGui::Dummy(ImVec2(W, h));
}

// ---------------------------------------------------------------- páginas

void App::drawMixerPage() {
    const Palette& p = palette();
    const float avail = ImGui::GetContentRegionAvail().x;

    // 01 BARRAMENTOS
    const float cardH = S(140);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(20)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.panel);
    ImGui::BeginChild("buses", ImVec2(avail, cardH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    if (sectionHeader("01", "BARRAMENTOS", "NOVO")) openBusEditor(-1);
    {
        const float pw = S(150), ph = S(56);
        for (size_t b = 0; b < cfg_.buses.size(); ++b) {
            if (b) ImGui::SameLine(0, S(12));
            int n = 0;
            for (const auto& r : routes_[b]) n += r.routed;
            char sub[32];
            std::snprintf(sub, sizeof(sub), "%d FONTE%s", n, n == 1 ? "" : "S");
            ImGui::PushID(int(b));
            if (pill("bus", upper(cfg_.buses[b].name).c_str(), sub, b == selBus_, pw, ph)) selBus_ = b;
            if (ImGui::BeginPopupContextItem("busctx")) {
                if (ImGui::MenuItem("Renomear ou apagar...")) openBusEditor(int(b));
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, S(10)));

    // 02 CANAIS
    const float restH = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("strips", ImVec2(avail, restH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (cfg_.buses.empty()) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }
    const std::string title = "CANAIS \xC2\xB7 " + upper(cfg_.buses[selBus_].name);
    if (sectionHeader("02", title.c_str(), "EDITAR BARRAMENTO")) openBusEditor(int(selBus_));

    const float stripW = S(112), outW = S(150);
    const float stripH = std::max(S(260), ImGui::GetContentRegionAvail().y - S(4));
    for (size_t s = 0; s < cfg_.sources.size(); ++s) {
        if (s) ImGui::SameLine(0, S(12));
        drawStrip(selBus_, s, stripW, stripH);
    }
    if (!cfg_.sources.empty()) ImGui::SameLine(0, S(12));
    // Botão de nova fonte, como uma faixa vazia.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("addsrc", ImVec2(stripW, stripH))) openSourceEditor(-1);
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRect(a0, ImVec2(a0.x + stripW, a0.y + stripH), col(hov ? p.accent : p.border), S(12), 0,
                    1.0f);
        const ImVec2 c(a0.x + stripW * 0.5f, a0.y + stripH * 0.45f);
        dl->AddLine(ImVec2(c.x - S(10), c.y), ImVec2(c.x + S(10), c.y), col(p.accent), S(2));
        dl->AddLine(ImVec2(c.x, c.y - S(10)), ImVec2(c.x, c.y + S(10)), col(p.accent), S(2));
        ImFont* fs = fonts().small;
        const float w = spacedWidth("NOVA FONTE", fs, S(1.3f));
        spacedText(dl, ImVec2(c.x - w * 0.5f, c.y + S(22)), "NOVA FONTE", col(p.textDim), fs, S(1.3f));
    }
    ImGui::SameLine(0, S(24));
    drawBusOutput(selBus_, outW, stripH);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void App::drawStrip(size_t bus, size_t src, float w, float h) {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const SourceConfig& sc = cfg_.sources[src];
    Route& r = routes_[bus][src];
    const std::string key = lower(sc.name);
    auto st = srcStats_.find(key);
    const bool inEngine = st != srcStats_.end();

    ImGui::PushID(int(src));
    ImGui::BeginGroup();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col(p.bg), S(12));
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), col(r.routed ? p.border : p.border, r.routed ? 1.0f : 0.5f),
                S(12), 0, 1.0f);

    // Cabeçalho: ponto de estado e nome.
    ImFont* fb = fonts().bold;
    ImFont* fs = fonts().small;
    stateDot(ImVec2(a.x + S(14), a.y + S(20)), inEngine ? st->second.state : SourceState::Stopped,
             inEngine);
    ImGui::PushClipRect(a, ImVec2(a.x + w - S(6), a.y + h), true);
    dl->AddText(fb, fb->FontSize, ImVec2(a.x + S(24), a.y + S(10)), col(r.routed ? p.text : p.textDim),
                sc.name.c_str());
    ImGui::PopClipRect();
    // Clicar no nome abre o editor da fonte.
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y));
    if (ImGui::InvisibleButton("name", ImVec2(w, S(34)))) openSourceEditor(int(src));
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s\nClique para editar a fonte", sourceSubtitle(sc).c_str());
    }

    const float bodyTop = a.y + S(40);
    const float footH = S(78);
    const float bodyH = h - (bodyTop - a.y) - footH;

    if (!r.routed) {
        // Fonte fora deste barramento: só um botão para incluir.
        const ImVec2 c(a.x + w * 0.5f, bodyTop + bodyH * 0.5f);
        const char* lbl = "INCLUIR";
        const float lw = spacedWidth(lbl, fb, S(1.2f));
        const float bw = lw + S(28), bh = S(34);
        const ImVec2 b0(c.x - bw * 0.5f, c.y - bh * 0.5f);
        ImGui::SetCursorScreenPos(b0);
        if (ImGui::InvisibleButton("incl", ImVec2(bw, bh))) {
            r.routed = true;
            r.muted = false;
            r.gain = 1.0f;
            pushGain(bus, src);
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Mandar esta fonte para o barramento %s", cfg_.buses[bus].name.c_str());
        }
        dl->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), col(hov ? p.accentHi : p.accent), bh * 0.5f, 0, 1.0f);
        spacedText(dl, ImVec2(b0.x + S(14), c.y - fb->FontSize * 0.5f), lbl, col(p.accent), fb, S(1.2f));
        const char* hint = "FORA DESTE BUS";
        const float hw = spacedWidth(hint, fs, S(1.0f));
        spacedText(dl, ImVec2(c.x - hw * 0.5f, b0.y - S(26)), hint, col(p.textDim), fs, S(1.0f));
    } else {
        // Fader + medidor.
        const float meterW = S(8);
        ImGui::SetCursorScreenPos(ImVec2(a.x + S(8), bodyTop));
        if (fader("fader", r.gain, w - S(16) - meterW - S(6), bodyH, r.muted)) {
            r.muted = false; // mexer no fader tira do mudo
            pushGain(bus, src);
        }
        if (ImGui::BeginPopupContextItem("stripctx")) {
            if (ImGui::MenuItem("Voltar para 0 dB")) {
                r.gain = 1.0f;
                pushGain(bus, src);
            }
            if (ImGui::MenuItem("Tirar deste barramento")) {
                r.routed = false;
                pushGain(bus, src);
            }
            if (ImGui::MenuItem("Editar fonte...")) openSourceEditor(int(src));
            ImGui::EndPopup();
        }
        const float mx = a.x + w - S(8) - meterW;
        meterBar(ImVec2(mx, bodyTop + S(10)), ImVec2(mx + meterW, bodyTop + bodyH - S(10)), srcMeters_[key],
                 true);
    }

    // Rodapé: valor em dB e botão de mudo.
    if (r.routed) {
        char db[24];
        const float d = toDb(r.gain);
        if (r.muted) std::snprintf(db, sizeof(db), "MUDO");
        else if (d <= kMinDb + 0.01f) std::snprintf(db, sizeof(db), "-inf dB");
        else std::snprintf(db, sizeof(db), "%+.1f dB", std::fabs(d) < 0.05f ? 0.0f : d);
        const ImVec2 dz = fb->CalcTextSizeA(fb->FontSize, FLT_MAX, 0, db);
        const float fy = a.y + h - footH;
        dl->AddText(fb, fb->FontSize, ImVec2(a.x + (w - dz.x) * 0.5f, fy + S(4)),
                    col(r.muted ? p.danger : p.text), db);

        const float bw = w - S(24), bh = S(28);
        const ImVec2 b0(a.x + S(12), fy + S(36));
        ImGui::SetCursorScreenPos(b0);
        if (ImGui::InvisibleButton("mute", ImVec2(bw, bh))) {
            r.muted = !r.muted;
            pushGain(bus, src);
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (r.muted) {
            dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), col(p.danger, hov ? 0.4f : 0.25f), bh * 0.5f);
            dl->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), col(p.danger), bh * 0.5f, 0, 1.0f);
        } else {
            dl->AddRect(b0, ImVec2(b0.x + bw, b0.y + bh), col(hov ? p.textDim : p.border), bh * 0.5f, 0, 1.0f);
        }
        const char* ml = "MUDO";
        const float mw = spacedWidth(ml, fs, S(1.4f));
        spacedText(dl, ImVec2(b0.x + (bw - mw) * 0.5f, b0.y + (bh - fs->FontSize) * 0.5f), ml,
                   col(r.muted ? p.text : p.textDim), fs, S(1.4f));
    }

    ImGui::SetCursorScreenPos(a);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::EndGroup();
    ImGui::PopID();
}

void App::drawBusOutput(size_t bus, float w, float h) {
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const std::string key = lower(cfg_.buses[bus].name);
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col(p.accent, 0.07f), S(12));
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), col(p.accent, 0.6f), S(12), 0, 1.0f);

    ImFont* fb = fonts().bold;
    ImFont* fs = fonts().small;
    const char* t = "SAÍDA";
    spacedText(dl, ImVec2(a.x + (w - spacedWidth(t, fb, S(1.6f))) * 0.5f, a.y + S(12)), t, col(p.accentHi),
               fb, S(1.6f));

    const float top = a.y + S(44), bot = a.y + h - S(90);
    const float mw = S(14);
    const float cx = a.x + w * 0.5f;
    // Escala em dB.
    for (float m : {0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f}) {
        const float f = (m - kMinDb) / (0.0f - kMinDb);
        const float y = bot - f * (bot - top);
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%d", int(m));
        const ImVec2 z = fs->CalcTextSizeA(fs->FontSize * 0.85f, FLT_MAX, 0, lbl);
        dl->AddText(fs, fs->FontSize * 0.85f, ImVec2(cx - mw - S(10) - z.x, y - z.y * 0.5f),
                    col(p.textDim), lbl);
    }
    meterBar(ImVec2(cx - mw - S(2), top), ImVec2(cx - S(2), bot), busMetersL_[key], true);
    meterBar(ImVec2(cx + S(2), top), ImVec2(cx + mw + S(2), bot), busMetersR_[key], true);
    dl->AddText(fs, fs->FontSize * 0.85f, ImVec2(cx - mw + S(2), bot + S(4)), col(p.textDim), "L");
    dl->AddText(fs, fs->FontSize * 0.85f, ImVec2(cx + S(6), bot + S(4)), col(p.textDim), "R");

    auto it = busStats_.find(key);
    const uint64_t clips = it != busStats_.end() ? it->second.clips : 0;
    char ctext[32];
    std::snprintf(ctext, sizeof(ctext), "%llu CLIPS", (unsigned long long)clips);
    const float cw = spacedWidth(ctext, fs, S(1.2f));
    spacedText(dl, ImVec2(cx - cw * 0.5f, bot + S(28)), ctext, col(clips ? p.danger : p.textDim), fs, S(1.2f));

    std::string obs = "OBS: " + cfg_.buses[bus].name;
    const ImVec2 oz = fs->CalcTextSizeA(fs->FontSize, FLT_MAX, 0, obs.c_str());
    dl->AddText(fs, fs->FontSize, ImVec2(cx - oz.x * 0.5f, bot + S(52)), col(p.textDim), obs.c_str());

    ImGui::Dummy(ImVec2(w, h));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("No OBS, a fonte \"NovaonFocus Bus\" com o nome \"%s\" recebe esta saída.",
                          cfg_.buses[bus].name.c_str());
}

void App::drawSourcesPage() {
    const Palette& p = palette();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(20)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.panel);
    ImGui::BeginChild("sources", ImVec2(avail, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders);
    if (sectionHeader("01", "FONTES DE ÁUDIO", "NOVA FONTE")) openSourceEditor(-1);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* fb = fonts().bold;
    ImFont* fs = fonts().small;
    const float rowH = S(70);
    const float W = ImGui::GetContentRegionAvail().x;
    for (size_t s = 0; s < cfg_.sources.size(); ++s) {
        const SourceConfig& sc = cfg_.sources[s];
        const std::string key = lower(sc.name);
        auto st = srcStats_.find(key);
        const bool inEngine = st != srcStats_.end();
        ImGui::PushID(int(s));
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("row", ImVec2(W, rowH))) openSourceEditor(int(s));
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), col(hov ? p.panelHi : p.bg), S(12));

        // Quadrado com ícone (como a capa da fila do NovaOndaPirate).
        const float q = S(42);
        const ImVec2 q0(a.x + S(14), a.y + (rowH - q) * 0.5f);
        dl->AddRectFilled(q0, ImVec2(q0.x + q, q0.y + q), col(p.accent, 0.9f), S(10));
        drawIcon(dl, sc.type == SourceType::Capture ? 1 : sc.type == SourceType::Loopback ? 5 : 4, ImVec2(q0.x + q * 0.5f, q0.y + q * 0.5f), S(22),
                 col(ImVec4(0.06f, 0.04f, 0.1f, 1)));

        const float tx = q0.x + q + S(16);
        dl->AddText(fb, fb->FontSize, ImVec2(tx, a.y + S(14)), col(p.text), sc.name.c_str());
        spacedText(dl, ImVec2(tx, a.y + S(40)), upper(sourceSubtitle(sc)).c_str(), col(p.textDim), fs, S(1.0f));

        // Estado, medidor e contadores à direita.
        const float rx = a.x + W - S(16);
        const char* stName = !inEngine ? (running() ? "APLICAR PARA ATIVAR" : "PARADA")
                             : st->second.state == SourceState::Running    ? "RODANDO"
                             : st->second.state == SourceState::Connecting ? "CONECTANDO"
                             : st->second.state == SourceState::Waiting    ? "AGUARDANDO"
                                                                           : "PARADA";
        const float sw = spacedWidth(stName, fs, S(1.3f));
        const float pillW = sw + S(34), pillH = S(28);
        const ImVec2 p0(rx - pillW, a.y + (rowH - pillH) * 0.5f);
        const bool good = inEngine && st->second.state == SourceState::Running;
        dl->AddRectFilled(p0, ImVec2(p0.x + pillW, p0.y + pillH), col(good ? p.accent : p.border, 0.18f),
                          pillH * 0.5f);
        dl->AddRect(p0, ImVec2(p0.x + pillW, p0.y + pillH), col(good ? p.accent : p.border), pillH * 0.5f, 0,
                    1.0f);
        stateDot(ImVec2(p0.x + S(14), p0.y + pillH * 0.5f), inEngine ? st->second.state : SourceState::Stopped,
                 inEngine);
        spacedText(dl, ImVec2(p0.x + S(24), p0.y + (pillH - fs->FontSize) * 0.5f), stName,
                   col(good ? p.accentHi : p.textDim), fs, S(1.3f));

        const float mW = S(180);
        const float mx = p0.x - S(24) - mW;
        if (mx > tx + S(260)) {
            meterBar(ImVec2(mx, a.y + rowH * 0.5f - S(3)), ImVec2(mx + mW, a.y + rowH * 0.5f + S(3)),
                     srcMeters_[key], false);
            if (inEngine) {
                char info[96];
                std::snprintf(info, sizeof(info), "buffer %.0f ms \xC2\xB7 falhas %llu \xC2\xB7 reconexões %llu",
                              st->second.bufferedFrames * 1000.0 / kBusSampleRate,
                              (unsigned long long)st->second.underruns,
                              (unsigned long long)st->second.reconnects);
                dl->AddText(fs, fs->FontSize * 0.9f, ImVec2(mx, a.y + rowH * 0.5f + S(9)), col(p.textDim), info);
            }
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, S(4)));
    }
    if (cfg_.sources.empty())
        ImGui::TextColored(p.textDim, "Nenhuma fonte ainda. Clique em NOVA FONTE para começar.");

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void App::drawObsPage() {
    const Palette& p = palette();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(20)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.panel);
    ImGui::BeginChild("obs", ImVec2(avail, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders);

    sectionHeader("01", "COMO LIGAR NO OBS");
    const char* steps[] = {
        "Copie novaonfocus-obs.dll para C:\\Program Files\\obs-studio\\obs-plugins\\64bit\\ e abra o OBS.",
        "Em Fontes, clique em + e escolha \"NovaonFocus Bus\".",
        "Digite o nome do barramento (use COPIAR abaixo).",
        "Para gravar faixas separadas, crie uma fonte por barramento e marque as faixas em "
        "Propriedades avançadas de áudio.",
        "Desative o \"Áudio da área de trabalho\" padrão do OBS para não duplicar o som.",
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* fb = fonts().bold;
    for (int i = 0; i < int(std::size(steps)); ++i) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        char n[4];
        std::snprintf(n, sizeof(n), "%d", i + 1);
        dl->AddCircle(ImVec2(a.x + S(12), a.y + S(11)), S(11), col(p.accent), 24, 1.0f);
        const ImVec2 nz = fb->CalcTextSizeA(fb->FontSize, FLT_MAX, 0, n);
        dl->AddText(fb, fb->FontSize, ImVec2(a.x + S(12) - nz.x * 0.5f, a.y + S(11) - nz.y * 0.5f),
                    col(p.accentHi), n);
        ImGui::SetCursorScreenPos(ImVec2(a.x + S(36), a.y + S(1)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail - S(120));
        ImGui::TextUnformatted(steps[i]);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(6)));
    }

    ImGui::Dummy(ImVec2(0, S(14)));
    sectionHeader("02", "BARRAMENTOS DISPONÍVEIS");
    const float W = ImGui::GetContentRegionAvail().x;
    for (size_t b = 0; b < cfg_.buses.size(); ++b) {
        ImGui::PushID(int(b));
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float rh = S(52);
        dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), col(p.bg), S(12));
        const std::string key = lower(cfg_.buses[b].name);
        dl->AddText(fb, fb->FontSize, ImVec2(a.x + S(18), a.y + (rh - fb->FontSize) * 0.5f), col(p.text),
                    cfg_.buses[b].name.c_str());
        meterBar(ImVec2(a.x + S(180), a.y + rh * 0.5f - S(5)), ImVec2(a.x + S(400), a.y + rh * 0.5f - S(1)),
                 busMetersL_[key], false);
        meterBar(ImVec2(a.x + S(180), a.y + rh * 0.5f + S(1)), ImVec2(a.x + S(400), a.y + rh * 0.5f + S(5)),
                 busMetersR_[key], false);
        ImGui::SetCursorScreenPos(ImVec2(a.x + W - S(110), a.y + (rh - S(30)) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(15));
        if (ImGui::Button("COPIAR", ImVec2(S(92), S(30)))) {
            ImGui::SetClipboardText(cfg_.buses[b].name.c_str());
            status_ = "Nome \"" + cfg_.buses[b].name + "\" copiado. Cole no OBS.";
            statusIsError_ = false;
            statusT_ = 4.0f;
        }
        ImGui::PopStyleVar();
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh + S(8)));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void App::drawSettingsPage() {
    const Palette& p = palette();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(20)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.panel);
    ImGui::BeginChild("settings", ImVec2(avail, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders);

    sectionHeader("01", "LATÊNCIA");
    ImGui::PushTextWrapPos(avail - S(40));
    ImGui::TextColored(p.textDim,
                       "Folga de cada fonte. Menor = menos atraso no OBS. Se ouvir estalos ou as falhas "
                       "subirem, aumente para 30 ou 40 ms.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(std::min(avail - S(60), S(420)));
    int lat = cfg_.latencyMs;
    if (ImGui::SliderInt("##lat", &lat, 5, 100, "%d ms")) {
        cfg_.latencyMs = lat;
        scheduleSave(true);
    }

    ImGui::Dummy(ImVec2(0, S(18)));
    sectionHeader("02", "DIAGNÓSTICO");
    ImGui::Text("Travamentos do sistema (mixer parado > 100 ms): ");
    ImGui::SameLine();
    ImGui::TextColored(lateTicks_ ? p.warn : p.ok, "%llu", (unsigned long long)lateTicks_);
    if (ImGui::BeginTable("diag", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                         ImGuiTableFlags_SizingStretchProp)) {
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, p.bg);
        ImGui::TableSetupColumn("Fonte");
        ImGui::TableSetupColumn("Estado");
        ImGui::TableSetupColumn("Buffer");
        ImGui::TableSetupColumn("Falhas / descartes");
        ImGui::TableSetupColumn("Reconexões");
        ImGui::TableHeadersRow();
        for (const auto& sc : cfg_.sources) {
            auto st = srcStats_.find(lower(sc.name));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(sc.name.c_str());
            if (st == srcStats_.end()) {
                ImGui::TableNextColumn();
                ImGui::TextColored(p.textDim, "fora do motor");
                continue;
            }
            const SourceStats& s = st->second;
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(sourceStateName(s.state));
            ImGui::TableNextColumn();
            ImGui::Text("%.1f ms", s.bufferedFrames * 1000.0 / kBusSampleRate);
            ImGui::TableNextColumn();
            ImGui::TextColored(s.underruns ? p.warn : p.text, "%llu / %llu", (unsigned long long)s.underruns,
                               (unsigned long long)s.drops);
            ImGui::TableNextColumn();
            ImGui::Text("%llu", (unsigned long long)s.reconnects);
        }
        ImGui::PopStyleColor();
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushTextWrapPos(avail - S(40));
    ImGui::TextColored(p.textDim,
                       "Se os travamentos sobem: rode o LatencyMon por 10 minutos, use 48 kHz em tudo, "
                       "plano de energia Alto desempenho, desative a suspensão seletiva de USB e ligue a "
                       "Scarlett numa porta traseira, sem hub.");
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, S(18)));
    sectionHeader("03", "ARQUIVO DE CONFIGURAÇÃO");
    ImGui::TextColored(p.textDim, "%s", wideToUtf8(configPath_).c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(15));
    if (ImGui::Button("ABRIR PASTA")) {
        std::wstring arg = L"/select,\"" + configPath_ + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("RECARREGAR DO ARQUIVO")) {
        const bool wasRunning = running();
        stopEngine();
        statusIsError_ = false;
        loadFromFile();
        dirty_ = false;
        if (wasRunning && !statusIsError_) startEngine();
    }
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------- editores

void App::openSourceEditor(int index) {
    editSrc_ = index;
    if (index >= 0) {
        editCfg_ = cfg_.sources[size_t(index)];
    } else {
        editCfg_ = SourceConfig{};
        editCfg_.type = SourceType::App;
        int n = int(cfg_.sources.size()) + 1;
        std::string name;
        do name = "fonte" + std::to_string(n++);
        while (nameTaken(name, false, -1));
        editCfg_.name = name;
    }
    copyTo(editName_, sizeof(editName_), editCfg_.name);
    copyTo(editExe_, sizeof(editExe_), wideToUtf8(editCfg_.exe));
    copyTo(editDevice_, sizeof(editDevice_), wideToUtf8(editCfg_.device));
    refreshHardware();
    apps_ = listAudioApps();
}

void App::drawSourceEditor() {
    const Palette& p = palette();
    if (editSrc_ == -2) return;
    if (!ImGui::IsPopupOpen("Fonte")) ImGui::OpenPopup("Fonte");
    ImGui::SetNextWindowSize(ImVec2(S(560), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(22)));
    if (!ImGui::BeginPopupModal("Fonte", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PopStyleVar();
        editSrc_ = -2;
        return;
    }
    ImGui::PopStyleVar();
    sectionHeader(editSrc_ < 0 ? "+" : "01", editSrc_ < 0 ? "NOVA FONTE" : "EDITAR FONTE");
    const float fw = ImGui::GetContentRegionAvail().x;

    ImGui::TextColored(p.textDim, "Nome (sem espaços)");
    ImGui::SetNextItemWidth(fw);
    ImGui::InputText("##name", editName_, sizeof(editName_));

    ImGui::TextColored(p.textDim, "O que capturar");
    const SourceType types[] = {SourceType::Capture, SourceType::App, SourceType::AppExclude,
                                SourceType::Loopback};
    for (int i = 0; i < 4; ++i) {
        if (i % 2) ImGui::SameLine(0, S(10));
        ImGui::PushID(i);
        const char* t = i == 0 ? "ENTRADA" : i == 1 ? "PROGRAMA" : i == 2 ? "TUDO MENOS" : "SAÍDA INTEIRA";
        const char* sub = i == 0 ? "MIC / INSTRUMENTO" : i == 1 ? "SÓ UM APP" : i == 2 ? "PC SEM UM APP"
                                                                                       : "TUDO DE UM DISP.";
        if (pill("type", t, sub, editCfg_.type == types[i], (fw - S(10)) * 0.5f, S(52)))
            editCfg_.type = types[i];
        ImGui::PopID();
    }
    ImGui::TextColored(p.textDim, "%s", typeLabel(editCfg_.type));
    ImGui::Dummy(ImVec2(0, S(4)));

    const bool isApp = editCfg_.type == SourceType::App || editCfg_.type == SourceType::AppExclude;
    if (isApp) {
        ImGui::TextColored(p.textDim, "Programa (.exe)");
        ImGui::SetNextItemWidth(fw - S(190));
        ImGui::InputText("##exe", editExe_, sizeof(editExe_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(180));
        if (ImGui::BeginCombo("##apps", "Tocando agora", ImGuiComboFlags_HeightLarge)) {
            std::set<std::wstring> seen;
            for (const auto& a : apps_) {
                if (a.exe.empty() || !seen.insert(a.exe).second) continue;
                const std::string e = wideToUtf8(a.exe);
                if (ImGui::Selectable(e.c_str())) copyTo(editExe_, sizeof(editExe_), e);
            }
            if (seen.empty()) ImGui::TextColored(p.textDim, "Nenhum programa tocando som agora.");
            ImGui::EndCombo();
        }
    } else {
        const bool cap = editCfg_.type == SourceType::Capture;
        ImGui::TextColored(p.textDim, cap ? "Dispositivo de entrada" : "Dispositivo de saída");
        std::string cur = editDevice_;
        if (cur.empty() || cur == "default") cur = "Padrão do Windows";
        ImGui::SetNextItemWidth(fw);
        if (ImGui::BeginCombo("##dev", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("Padrão do Windows")) copyTo(editDevice_, sizeof(editDevice_), "default");
            for (const auto& d : devices_) {
                if (d.isCapture != cap) continue;
                const std::string n = wideToUtf8(d.name);
                if (ImGui::Selectable(n.c_str(), n == editDevice_)) copyTo(editDevice_, sizeof(editDevice_), n);
            }
            ImGui::EndCombo();
        }
        if (cap) {
            ImGui::TextColored(p.textDim, "Canal da Scarlett");
            if (ImGui::RadioButton("Entrada 1", editCfg_.inputChannel == 1)) editCfg_.inputChannel = 1;
            ImGui::SameLine(0, S(18));
            if (ImGui::RadioButton("Entrada 2", editCfg_.inputChannel == 2)) editCfg_.inputChannel = 2;
            ImGui::SameLine(0, S(18));
            if (ImGui::RadioButton("Ambas (estéreo)", editCfg_.inputChannel == 0)) editCfg_.inputChannel = 0;
        }
    }

    // Validação.
    const std::string name = sanitizeName(editName_);
    std::string problem;
    if (name.empty()) problem = "Dê um nome à fonte.";
    else if (nameTaken(name, false, editSrc_)) problem = "Já existe uma fonte com esse nome.";
    else if (isApp && !*editExe_) problem = "Escolha o programa.";
    ImGui::Dummy(ImVec2(0, S(8)));
    if (!problem.empty()) ImGui::TextColored(p.warn, "%s", problem.c_str());
    else ImGui::TextColored(p.textDim, "Mudar fontes reinicia o motor ao clicar em APLICAR MUDANÇAS.");
    ImGui::Dummy(ImVec2(0, S(6)));

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(16));
    const float bw = S(120), bh = S(34);
    if (editSrc_ >= 0) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(p.danger.x, p.danger.y, p.danger.z, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(p.danger.x, p.danger.y, p.danger.z, 0.35f));
        if (ImGui::Button("APAGAR", ImVec2(bw, bh))) {
            const size_t i = size_t(editSrc_);
            cfg_.sources.erase(cfg_.sources.begin() + std::ptrdiff_t(i));
            for (auto& row : routes_) row.erase(row.begin() + std::ptrdiff_t(i));
            scheduleSave(true);
            editSrc_ = -2;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - bw * 2 - S(10)));
    if (ImGui::Button("CANCELAR", ImVec2(bw, bh))) {
        editSrc_ = -2;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0, S(10));
    ImGui::BeginDisabled(!problem.empty());
    ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accentHi);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.04f, 0.1f, 1));
    if (ImGui::Button("SALVAR", ImVec2(bw, bh)) && editSrc_ != -2) {
        SourceConfig sc = editCfg_;
        sc.name = name;
        sc.exe = utf8ToWide(editExe_);
        sc.device = utf8ToWide(*editDevice_ ? editDevice_ : "default");
        if (editSrc_ < 0) {
            cfg_.sources.push_back(sc);
            for (size_t b = 0; b < routes_.size(); ++b) {
                Route r;
                r.routed = b == selBus_; // entra no barramento que está aberto
                routes_[b].push_back(r);
            }
        } else {
            cfg_.sources[size_t(editSrc_)] = sc;
        }
        scheduleSave(true);
        if (!running()) needsRestart_ = false;
        editSrc_ = -2;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

void App::openBusEditor(int index) {
    editBus_ = index;
    std::string name;
    if (index >= 0) {
        name = cfg_.buses[size_t(index)].name;
    } else {
        int n = int(cfg_.buses.size()) + 1;
        do name = "bus" + std::to_string(n++);
        while (nameTaken(name, true, -1));
    }
    copyTo(editBusName_, sizeof(editBusName_), name);
}

void App::drawBusEditor() {
    const Palette& p = palette();
    if (editBus_ == -2) return;
    if (!ImGui::IsPopupOpen("Barramento")) ImGui::OpenPopup("Barramento");
    ImGui::SetNextWindowSize(ImVec2(S(480), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(22)));
    if (!ImGui::BeginPopupModal("Barramento", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PopStyleVar();
        editBus_ = -2;
        return;
    }
    ImGui::PopStyleVar();
    sectionHeader(editBus_ < 0 ? "+" : "01", editBus_ < 0 ? "NOVO BARRAMENTO" : "EDITAR BARRAMENTO");
    const float fw = ImGui::GetContentRegionAvail().x;
    ImGui::TextColored(p.textDim, "Nome (é o que você digita no OBS)");
    ImGui::SetNextItemWidth(fw);
    ImGui::InputText("##bname", editBusName_, sizeof(editBusName_));
    const std::string name = sanitizeName(editBusName_);
    std::string problem;
    if (name.empty()) problem = "Dê um nome ao barramento.";
    else if (nameTaken(name, true, editBus_)) problem = "Já existe um barramento com esse nome.";
    if (!problem.empty()) ImGui::TextColored(p.warn, "%s", problem.c_str());
    else if (editBus_ >= 0 && name != cfg_.buses[size_t(editBus_)].name)
        ImGui::TextColored(p.warn, "Lembre de trocar o nome também na fonte do OBS.");
    else ImGui::TextColored(p.textDim, "Cada barramento vira uma fonte separada no OBS.");
    ImGui::Dummy(ImVec2(0, S(8)));

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(16));
    const float bw = S(120), bh = S(34);
    if (editBus_ >= 0) {
        ImGui::BeginDisabled(cfg_.buses.size() <= 1);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(p.danger.x, p.danger.y, p.danger.z, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(p.danger.x, p.danger.y, p.danger.z, 0.35f));
        if (ImGui::Button("APAGAR", ImVec2(bw, bh))) {
            const size_t i = size_t(editBus_);
            cfg_.buses.erase(cfg_.buses.begin() + std::ptrdiff_t(i));
            routes_.erase(routes_.begin() + std::ptrdiff_t(i));
            if (selBus_ >= cfg_.buses.size()) selBus_ = cfg_.buses.size() - 1;
            scheduleSave(true);
            editBus_ = -2;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - bw * 2 - S(10)));
    if (ImGui::Button("CANCELAR", ImVec2(bw, bh))) {
        editBus_ = -2;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0, S(10));
    ImGui::BeginDisabled(!problem.empty());
    ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accentHi);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.04f, 0.1f, 1));
    if (ImGui::Button("SALVAR", ImVec2(bw, bh)) && editBus_ != -2) {
        if (editBus_ < 0) {
            BusConfig bc;
            bc.name = name;
            cfg_.buses.push_back(bc);
            routes_.emplace_back(cfg_.sources.size());
            selBus_ = cfg_.buses.size() - 1;
        } else {
            cfg_.buses[size_t(editBus_)].name = name;
        }
        scheduleSave(true);
        editBus_ = -2;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

} // namespace novaon::ui
