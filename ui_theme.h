#pragma once

#include <windows.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace ui {

using namespace Gdiplus;

class GdiplusSession {
public:
    GdiplusSession() {
        GdiplusStartupInput input;
        GdiplusStartup(&token_, &input, nullptr);
    }
    ~GdiplusSession() {
        if (token_) GdiplusShutdown(token_);
    }
    GdiplusSession(const GdiplusSession&) = delete;
    GdiplusSession& operator=(const GdiplusSession&) = delete;

private:
    ULONG_PTR token_ = 0;
};

namespace palette {
inline Color Sidebar()      { return Color(255, 15, 42, 68); }
inline Color SidebarHover() { return Color(255, 27, 58, 92); }
inline Color SidebarText()  { return Color(255, 203, 213, 225); }
inline Color Accent()       { return Color(255, 37, 99, 235); }
inline Color Background()   { return Color(255, 244, 247, 251); }
inline Color Card()         { return Color(255, 255, 255, 255); }
inline Color Border()       { return Color(255, 226, 232, 240); }
inline Color TextMain()     { return Color(255, 15, 23, 42); }
inline Color TextMuted()    { return Color(255, 100, 116, 139); }
inline Color White()        { return Color(255, 255, 255, 255); }
inline Color Success()      { return Color(255, 22, 163, 74); }
inline Color Warning()      { return Color(255, 234, 88, 12); }
inline Color Danger()       { return Color(255, 220, 38, 38); }
}  // namespace palette

inline COLORREF ToColorRef(const Color& c) {
    return RGB(c.GetR(), c.GetG(), c.GetB());
}

inline FontFamily& UiFamily() {
    static FontFamily family(L"Segoe UI");
    return family;
}

inline void AddRoundRect(GraphicsPath& path, const RectF& r, float radius) {
    const float d = radius * 2.0f;
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

inline void FillRoundRect(Graphics& g, const RectF& r, float radius, const Color& color) {
    GraphicsPath path;
    AddRoundRect(path, r, radius);
    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

inline void StrokeRoundRect(Graphics& g, const RectF& r, float radius, const Color& color, float width = 1.0f) {
    GraphicsPath path;
    AddRoundRect(path, r, radius);
    Pen pen(color, width);
    g.DrawPath(&pen, &path);
}

inline void DrawUiText(Graphics& g, const wchar_t* text, const RectF& r, float pixels,
                     const Color& color, bool bold = false, bool centered = false) {
    Font font(&UiFamily(), pixels, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetAlignment(centered ? StringAlignmentCenter : StringAlignmentNear);
    g.DrawString(text, -1, &font, r, &format, &brush);
}

enum class ButtonKind { Primary, Secondary, Danger, Nav, NavActive };

inline void DrawButton(const DRAWITEMSTRUCT* item, ButtonKind kind) {
    Graphics g(item->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    RectF r(static_cast<REAL>(item->rcItem.left), static_cast<REAL>(item->rcItem.top),
            static_cast<REAL>(item->rcItem.right - item->rcItem.left),
            static_cast<REAL>(item->rcItem.bottom - item->rcItem.top));
    r.Inflate(-1.0f, -1.0f);

    Color background = palette::Card();
    Color foreground = palette::TextMain();
    bool bordered = false;
    const bool nav = (kind == ButtonKind::Nav || kind == ButtonKind::NavActive);

    switch (kind) {
    case ButtonKind::Primary:   background = palette::Accent();  foreground = palette::White(); break;
    case ButtonKind::Danger:    background = palette::Danger();  foreground = palette::White(); break;
    case ButtonKind::Nav:       background = palette::Sidebar(); foreground = palette::SidebarText(); break;
    case ButtonKind::NavActive: background = palette::Accent();  foreground = palette::White(); break;
    case ButtonKind::Secondary: bordered = true; break;
    }

    if (item->itemState & ODS_DISABLED) foreground = palette::TextMuted();

    FillRoundRect(g, r, 8.0f, background);
    if (bordered) StrokeRoundRect(g, r, 8.0f, palette::Border());

    wchar_t text[256] = {0};
    GetWindowTextW(item->hwndItem, text, 256);
    RectF textRect = r;
    if (nav) {
        textRect.X += 16.0f;
        textRect.Width -= 16.0f;
    }
    DrawUiText(g, text, textRect, 14.0f, foreground, false, !nav);
}

}  // namespace ui
