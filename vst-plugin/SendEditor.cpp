#include "SendEditor.h"

#include "public.sdk/source/common/pluginview.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
// O gdiplus.h usa min/max sem namespace; com NOMINMAX eles somem.
namespace Gdiplus {
using std::max;
using std::min;
} // namespace Gdiplus
#include <gdiplus.h>

namespace novaon {

namespace {

using namespace Gdiplus;
using Steinberg::tresult;
using Steinberg::kResultTrue;
using Steinberg::kResultFalse;

constexpr int kWidth = 380;
constexpr int kHeight = 252;

// Mesma paleta da interface do app (src/gui/Theme.cpp).
Color rgb(unsigned v, BYTE a = 255) { return Color(a, BYTE(v >> 16), BYTE(v >> 8), BYTE(v)); }
const unsigned kBg = 0x0C0C12, kPanel = 0x15151D, kPanelHi = 0x1D1D28, kBorder = 0x2A2A38,
               kText = 0xF2F2F7, kDim = 0x8B8B9E, kAccent = 0xA06BF5, kAccentHi = 0xC4A5FF,
               kOk = 0x3DDC97, kWarn = 0xF5C24B, kDanger = 0xF2557A;

void roundRect(GraphicsPath& p, const RectF& r, float rad) {
    const float d = rad * 2.0f;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

void fillRound(Graphics& g, const RectF& r, float rad, const Color& fill, const Color* border = nullptr) {
    GraphicsPath path;
    roundRect(path, r, rad);
    SolidBrush b(fill);
    g.FillPath(&b, &path);
    if (border) {
        Pen pen(*border, 1.0f);
        g.DrawPath(&pen, &path);
    }
}

void text(Graphics& g, const std::wstring& s, Font& f, const Color& c, float x, float y) {
    SolidBrush b(c);
    g.DrawString(s.c_str(), INT(s.size()), &f, PointF(x, y), StringFormat::GenericTypographic(), &b);
}

// Títulos em maiúsculas espaçadas, como na interface do app.
void spaced(Graphics& g, const std::wstring& s, Font& f, const Color& c, float x, float y, float gap) {
    SolidBrush b(c);
    for (wchar_t ch : s) {
        RectF box;
        g.MeasureString(&ch, 1, &f, PointF(0, 0), StringFormat::GenericTypographic(), &box);
        g.DrawString(&ch, 1, &f, PointF(x, y), StringFormat::GenericTypographic(), &b);
        x += (ch == L' ' ? f.GetSize() * 0.35f : box.Width) + gap;
    }
}

float toDb(float v) { return v > 1e-6f ? 20.0f * std::log10(v) : -120.0f; }

ULONG_PTR g_gdiplusToken = 0;
int g_gdiplusUsers = 0;
const wchar_t* kClassName = L"NovaonFocusSendView";

HINSTANCE moduleHandle() {
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleHandle), &h);
    return h;
}

class SendView : public Steinberg::CPluginView {
public:
    explicit SendView(SendViewModel* model) : CPluginView(nullptr), model_(model) {
        Steinberg::ViewRect r(0, 0, kWidth, kHeight);
        setRect(r);
    }

    tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override {
        return std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override {
        if (isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;
        if (g_gdiplusUsers++ == 0) {
            GdiplusStartupInput in;
            GdiplusStartup(&g_gdiplusToken, &in, nullptr);
        }
        registerClass();
        hwnd_ = CreateWindowExW(0, kClassName, L"NovaonFocus Send", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                0, 0, kWidth, kHeight, static_cast<HWND>(parent), nullptr, moduleHandle(), this);
        if (hwnd_) SetTimer(hwnd_, 1, 33, nullptr);
        return CPluginView::attached(parent, type);
    }

    tresult PLUGIN_API removed() override {
        if (hwnd_) {
            KillTimer(hwnd_, 1);
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        if (--g_gdiplusUsers == 0) GdiplusShutdown(g_gdiplusToken);
        return CPluginView::removed();
    }

private:
    static void registerClass() {
        static bool done = false;
        if (done) return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &SendView::wndProc;
        wc.hInstance = moduleHandle();
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        done = true;
    }

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto* self = reinterpret_cast<SendView*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!self) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            self->tick();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            self->click(short(LOWORD(lp)), short(HIWORD(lp)));
            return 0;
        case WM_MOUSEMOVE: {
            const int hot = self->hitChannel(short(LOWORD(lp)), short(HIWORD(lp)));
            if (hot != self->hot_) {
                self->hot_ = hot;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            self->hot_ = 0;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            if (self->hot_) {
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); // IDC_HAND
                return TRUE;
            }
            break;
        case WM_PRINTCLIENT:
            self->paint(reinterpret_cast<HDC>(wp));
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            self->paint(dc);
            EndPaint(h, &ps);
            return 0;
        }
        default:
            break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    // Botões de canal: 8 quadrados na linha do cartão "01 CANAL".
    RectF channelRect(int ch) const {
        return RectF(28.0f + float(ch - 1) * 41.0f, 96.0f, 36.0f, 30.0f);
    }

    int hitChannel(int x, int y) const {
        for (int ch = 1; ch <= 8; ++ch) {
            const RectF r = channelRect(ch);
            if (x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height) return ch;
        }
        return 0;
    }

    void click(int x, int y) {
        const int ch = hitChannel(x, y);
        if (ch) {
            model_->requestChannel(ch);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void tick() {
        for (int s = 0; s < 2; ++s) {
            const float p = model_->takePeak(s);
            meter_[s] = std::max(p, meter_[s] * 0.82f); // queda suave
            if (p >= hold_[s]) {
                hold_[s] = p;
                holdAge_[s] = 0;
            } else if (++holdAge_[s] > 30) {
                hold_[s] *= 0.9f;
            }
        }
    }

    void paint(HDC target) {
        HDC mem = CreateCompatibleDC(target);
        HBITMAP bmp = CreateCompatibleBitmap(target, kWidth, kHeight);
        HGDIOBJ old = SelectObject(mem, bmp);
        {
            Graphics g(mem);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            draw(g);
        }
        BitBlt(target, 0, 0, kWidth, kHeight, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }

    void draw(Graphics& g) {
        FontFamily family(L"Segoe UI");
        Font title(&family, 13.0f, FontStyleBold, UnitPixel);
        Font label(&family, 10.5f, FontStyleBold, UnitPixel);
        Font body(&family, 12.5f, FontStyleRegular, UnitPixel);
        Font bodyBold(&family, 12.5f, FontStyleBold, UnitPixel);
        Font caption(&family, 11.0f, FontStyleRegular, UnitPixel);

        SolidBrush bg(rgb(kBg));
        g.FillRectangle(&bg, 0, 0, kWidth, kHeight);

        const int ch = model_->channel();
        const bool processing = model_->dawProcessing();
        const bool connected = processing && model_->appConnected();

        // Cabeçalho.
        spaced(g, L"NOVAONFOCUS", title, rgb(kText), 20, 16, 2.2f);
        spaced(g, L"SEND", title, rgb(kAccentHi), 146, 16, 2.2f);
        text(g, L"Envia o áudio da DAW para o NovaonFocus", caption, rgb(kDim), 20, 36);

        // Selo de status no canto.
        const Color badge = connected ? rgb(kOk) : processing ? rgb(kWarn) : rgb(kDim);
        const std::wstring badgeText = connected ? L"CONECTADO" : L"AGUARDANDO";
        const float bw = connected ? 92.0f : 98.0f;
        fillRound(g, RectF(float(kWidth) - 16.0f - bw, 16.0f, bw, 20.0f), 10.0f,
                  Color(40, badge.GetR(), badge.GetG(), badge.GetB()));
        SolidBrush dot(badge);
        g.FillEllipse(&dot, float(kWidth) - 16.0f - bw + 9.0f, 23.0f, 6.0f, 6.0f);
        spaced(g, badgeText, label, badge, float(kWidth) - 16.0f - bw + 20.0f, 20.5f, 1.2f);

        // Cartão 01: canal.
        const Color border = rgb(kBorder);
        fillRound(g, RectF(16, 62, float(kWidth) - 32, 76), 10.0f, rgb(kPanel), &border);
        spaced(g, L"01", label, rgb(kAccent), 28, 72, 1.5f);
        spaced(g, L"CANAL", label, rgb(kDim), 50, 72, 1.8f);
        for (int c = 1; c <= 8; ++c) {
            const RectF r = channelRect(c);
            const bool sel = c == ch;
            const bool hot = c == hot_;
            const Color edge = sel ? rgb(kAccent) : hot ? rgb(kAccent, 140) : border;
            fillRound(g, r, 8.0f, sel ? rgb(kAccent) : hot ? rgb(kPanelHi) : rgb(kPanelHi, 160), &edge);
            const std::wstring n = std::to_wstring(c);
            RectF box;
            g.MeasureString(n.c_str(), 1, &bodyBold, PointF(0, 0), StringFormat::GenericTypographic(), &box);
            text(g, n, bodyBold, sel ? rgb(0xFFFFFF) : rgb(kText), r.X + (r.Width - box.Width) / 2.0f,
                 r.Y + (r.Height - box.Height) / 2.0f);
        }

        // Cartão 02: status e nível.
        fillRound(g, RectF(16, 148, float(kWidth) - 32, 90), 10.0f, rgb(kPanel), &border);
        spaced(g, L"02", label, rgb(kAccent), 28, 158, 1.5f);
        spaced(g, L"STATUS", label, rgb(kDim), 50, 158, 1.8f);

        std::wstring line1, line2;
        if (!processing) {
            line1 = L"A DAW não está mandando áudio para o plugin";
            line2 = L"Confira se o plugin está ativo no Master";
        } else if (connected) {
            line1 = L"O NovaonFocus está recebendo o canal " + std::to_wstring(ch);
            const uint32_t sr = model_->sampleRate();
            wchar_t buf[64];
            swprintf(buf, 64, L"DAW a %.1f kHz · convertido para 48 kHz", double(sr) / 1000.0);
            line2 = buf;
        } else {
            line1 = L"O NovaonFocus não está lendo o canal " + std::to_wstring(ch);
            line2 = L"No .ini: type = send e channel = " + std::to_wstring(ch);
        }
        text(g, line1, body, connected ? rgb(kText) : rgb(kWarn), 28, 176);
        text(g, line2, caption, rgb(kDim), 28, 193);

        // Medidores L/R de -60 a 0 dB.
        const float mx = 46.0f, mw = float(kWidth) - 32.0f - 30.0f - 50.0f;
        for (int s = 0; s < 2; ++s) {
            const float y = 213.0f + float(s) * 11.0f;
            text(g, s == 0 ? L"L" : L"R", caption, rgb(kDim), 28, y - 4.0f);
            fillRound(g, RectF(mx, y, mw, 6.0f), 3.0f, rgb(kPanelHi));
            const float db = toDb(meter_[s]);
            const float frac = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
            if (frac > 0.0f) {
                const Color c = db > -1.0f ? rgb(kDanger) : db > -6.0f ? rgb(kWarn) : rgb(kAccent);
                fillRound(g, RectF(mx, y, std::max(6.0f, mw * frac), 6.0f), 3.0f, c);
            }
            const float hfrac = std::clamp((toDb(hold_[s]) + 60.0f) / 60.0f, 0.0f, 1.0f);
            if (hfrac > 0.0f) {
                SolidBrush hb(rgb(kAccentHi));
                g.FillRectangle(&hb, mx + mw * hfrac - 1.5f, y, 2.0f, 6.0f);
            }
            wchar_t buf[16];
            if (db <= -60.0f) wcscpy(buf, L"-∞");
            else swprintf(buf, 16, L"%.0f", double(db));
            text(g, buf, caption, rgb(kDim), mx + mw + 8.0f, y - 4.0f);
        }
    }

    SendViewModel* model_;
    HWND hwnd_ = nullptr;
    int hot_ = 0;
    float meter_[2] = {0, 0};
    float hold_[2] = {0, 0};
    int holdAge_[2] = {0, 0};
};

} // namespace

Steinberg::IPlugView* createSendView(SendViewModel* model) { return new SendView(model); }

} // namespace novaon
