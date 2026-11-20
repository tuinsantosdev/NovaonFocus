#pragma once
// Cores, fontes e medidas da interface num lugar só. Para mudar a estética,
// mexa só em Theme.cpp (os tokens) — o resto da interface lê daqui.

#include "imgui.h"

namespace novaon::ui {

struct Palette {
    ImVec4 bg;          // fundo da janela
    ImVec4 panel;       // painéis e cartões
    ImVec4 panelHi;     // cartão com o mouse em cima / campos
    ImVec4 border;
    ImVec4 text;
    ImVec4 textDim;
    ImVec4 accent;      // destaque principal (botões, fader)
    ImVec4 accentHi;
    ImVec4 accent2;     // destaque secundário (abas, seleção)
    ImVec4 ok;          // fonte rodando, medidor baixo
    ImVec4 warn;        // medidor médio, avisos
    ImVec4 danger;      // clip, mudo, erro
};

struct Fonts {
    ImFont* body = nullptr;
    ImFont* bold = nullptr;
    ImFont* title = nullptr;
    ImFont* small = nullptr;
};

const Palette& palette();
Fonts& fonts();

// Aplica o estilo do ImGui e carrega as fontes. `scale` = DPI / 96.
void apply(float scale);

ImU32 col(const ImVec4& c, float alpha = 1.0f);

} // namespace novaon::ui
