#include "Theme.h"

#include <filesystem>
#include <string>

namespace novaon::ui {

namespace {

constexpr ImVec4 hex(unsigned rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                  (rgb & 0xFF) / 255.0f, a);
}

// Paleta do NovaOndaPirate: preto azulado, cartões com borda fina e
// destaque roxo/lilás.
const Palette kPalette{
    /* bg       */ hex(0x0C0C12),
    /* panel    */ hex(0x15151D),
    /* panelHi  */ hex(0x1D1D28),
    /* border   */ hex(0x2A2A38),
    /* text     */ hex(0xF2F2F7),
    /* textDim  */ hex(0x8B8B9E),
    /* accent   */ hex(0xA06BF5),
    /* accentHi */ hex(0xC4A5FF),
    /* accent2  */ hex(0x7C4DDB),
    /* ok       */ hex(0x3DDC97),
    /* warn     */ hex(0xF5C24B),
    /* danger   */ hex(0xF2557A),
};

// Fontes do Windows, na ordem de preferência.
#ifndef NOVAON_FONT_BODY
#define NOVAON_FONT_BODY "C:\\Windows\\Fonts\\segoeui.ttf"
#define NOVAON_FONT_BOLD "C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf"
#endif
const char* kBodyFonts[] = {NOVAON_FONT_BODY};
const char* kBoldFonts[] = {NOVAON_FONT_BOLD};

Fonts g_fonts;

// Latim (com acentos) + pontuação geral (·, ›, –).
const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0};

ImFont* loadFirst(const char* const* paths, size_t n, float size) {
    ImGuiIO& io = ImGui::GetIO();
    for (size_t i = 0; i < n; ++i) {
        std::error_code ec;
        if (std::filesystem::exists(paths[i], ec))
            return io.Fonts->AddFontFromFileTTF(paths[i], size, nullptr, kRanges);
    }
    ImFontConfig cfg;
    cfg.SizePixels = size;
    return io.Fonts->AddFontDefault(&cfg);
}

} // namespace

const Palette& palette() { return kPalette; }
Fonts& fonts() { return g_fonts; }

ImU32 col(const ImVec4& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

void apply(float scale) {
    const Palette& p = kPalette;
    g_fonts.body = loadFirst(kBodyFonts, std::size(kBodyFonts), 17.0f * scale);
    g_fonts.bold = loadFirst(kBoldFonts, std::size(kBoldFonts), 17.0f * scale);
    g_fonts.title = loadFirst(kBoldFonts, std::size(kBoldFonts), 24.0f * scale);
    g_fonts.small = loadFirst(kBodyFonts, std::size(kBodyFonts), 14.0f * scale);

    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(14, 14);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 12;
    s.GrabMinSize = 14;
    s.WindowRounding = 10;
    s.ChildRounding = 14;
    s.FrameRounding = 10;
    s.PopupRounding = 12;
    s.GrabRounding = 10;
    s.TabRounding = 6;
    s.ScrollbarRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    s.PopupBorderSize = 1;
    s.ScaleAllSizes(scale);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = p.panel;
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_FrameBg] = p.panelHi;
    c[ImGuiCol_FrameBgHovered] = p.border;
    c[ImGuiCol_FrameBgActive] = p.border;
    c[ImGuiCol_TitleBg] = p.panel;
    c[ImGuiCol_TitleBgActive] = p.panel;
    c[ImGuiCol_MenuBarBg] = p.panel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_ScrollbarGrabHovered] = p.textDim;
    c[ImGuiCol_ScrollbarGrabActive] = p.accent;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accentHi;
    c[ImGuiCol_Button] = p.panelHi;
    c[ImGuiCol_ButtonHovered] = p.border;
    c[ImGuiCol_ButtonActive] = p.accent;
    c[ImGuiCol_Header] = p.panelHi;
    c[ImGuiCol_HeaderHovered] = p.border;
    c[ImGuiCol_HeaderActive] = p.border;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_Tab] = p.panel;
    c[ImGuiCol_TabHovered] = p.panelHi;
    c[ImGuiCol_TabSelected] = p.panelHi;
    c[ImGuiCol_TabSelectedOverline] = p.accent2;
    c[ImGuiCol_TabDimmed] = p.panel;
    c[ImGuiCol_TabDimmedSelected] = p.panelHi;
    c[ImGuiCol_TableHeaderBg] = p.panelHi;
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableBorderLight] = p.border;
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_NavHighlight] = p.accent;
}

} // namespace novaon::ui
