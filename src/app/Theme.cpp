#include "app/Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <imgui-SFML.h>

#include "app/Paths.h"
#include "app/Settings.h"
#include "platform/Platform.h"

namespace app {

namespace {

ImFont* gHeadlineFont = nullptr;
ImFont* gTitleFont = nullptr;
bool gFontsLoaded = false;
float gFontDensity = 1.0f;
bool gSerif = true;

constexpr float kBodySize = 18.0f;
constexpr float kTitleSize = 24.0f;
constexpr float kHeadlineSize = 32.0f;

std::vector<std::filesystem::path> fontCandidates() {
    std::vector<std::filesystem::path> candidates;
    if (gSerif) {
        const std::filesystem::path fonts = findUpwards("data") / "fonts";
        candidates.push_back(fonts / "SourceSerif4-Regular.ttf");
    }
    // The platform layer knows where this system keeps its interface fonts.
    for (const std::filesystem::path& file : platform::uiFontCandidates()) candidates.push_back(file);
    return candidates;
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    gHeadlineFont = nullptr;
    gTitleFont = nullptr;
    bool loaded = false;
    std::error_code ec;
    // Sizes stay in points; on a high-DPI display the glyphs are rasterized with more pixels.
    ImFontConfig config;
    config.RasterizerDensity = gFontDensity;
    for (const std::filesystem::path& file : fontCandidates()) {
        const std::string path = pathToUtf8(file);
        if (!std::filesystem::exists(file, ec)) continue;
        if (io.Fonts->AddFontFromFileTTF(path.c_str(), kBodySize, &config) != nullptr) {
            gTitleFont = io.Fonts->AddFontFromFileTTF(path.c_str(), kTitleSize, &config);
            gHeadlineFont = io.Fonts->AddFontFromFileTTF(path.c_str(), kHeadlineSize, &config);
            loaded = true;
            break;
        }
    }
    if (!loaded) io.Fonts->AddFontDefault(&config);
    (void)ImGui::SFML::UpdateFontTexture();
    gFontsLoaded = true;
}

ImVec4 hex(unsigned rgb, float alpha = 1.0f) {
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
}

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 withAlpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

struct Palette {
    ImVec4 background;
    ImVec4 panel;
    ImVec4 raised;
    ImVec4 hover;
    ImVec4 accent;       // fills: selected tabs, sliders, checkmarks
    ImVec4 accentText;   // accent used for text
    ImVec4 text;
    ImVec4 textDisabled;
    ImVec4 glowA;        // background light
    ImVec4 glowB;
};

// In the order of uiThemes().
const std::array<Palette, 14>& palettes() {
    static const std::array<Palette, 14> list = {{
        // Forest: the original green on warm charcoal.
        {hex(0x161512), hex(0x262421), hex(0x373531), hex(0x494641), hex(0x629940), hex(0x99CC66), hex(0xCCCCC7),
         hex(0x80807A), hex(0x6FA34A), hex(0xC8B27A)},
        // Walnut: amber on dark brown.
        {hex(0x18120E), hex(0x29201A), hex(0x3A2E25), hex(0x4B3C30), hex(0xC0843A), hex(0xE8B06A), hex(0xE6DCCF),
         hex(0x928473), hex(0xD08A3C), hex(0x9C5A3A)},
        // Midnight: blue on slate.
        {hex(0x0E1218), hex(0x18202A), hex(0x242E3A), hex(0x303C4B), hex(0x4C8BE0), hex(0x8DB8F2), hex(0xD6DDE6),
         hex(0x7D8896), hex(0x3F7FD8), hex(0x7A5CC8)},
        // Rosewood: rose on plum.
        {hex(0x16101A), hex(0x241B28), hex(0x332637), hex(0x423246), hex(0xC0607D), hex(0xEC9DB4), hex(0xE7DCE5),
         hex(0x8F808C), hex(0xC45A7E), hex(0xD49A5A)},
        // Paper: the original light theme.
        {hex(0xEDEBE7), hex(0xFFFFFF), hex(0xE0DED9), hex(0xCFCDC7), hex(0x70A54C), hex(0x40852A), hex(0x292926),
         hex(0x787570), hex(0x8CC266), hex(0xE6C88C)},
        // Sand: copper on warm paper.
        {hex(0xEFE8DC), hex(0xFCF8F1), hex(0xE3D9C8), hex(0xD6CAB6), hex(0xB8722E), hex(0x8E5418), hex(0x2B241C),
         hex(0x7D7263), hex(0xE0A060), hex(0xC9A0D0)},
        // Crimson: red on dark wine.
        {hex(0x170E0F), hex(0x261819), hex(0x372223), hex(0x482D2F), hex(0xC8414B), hex(0xF08A90), hex(0xE8DCDC),
         hex(0x927F80), hex(0xC83C46), hex(0xD08A3C)},
        // Ember: orange on charred brown.
        {hex(0x17110C), hex(0x271C14), hex(0x38281C), hex(0x493425), hex(0xE0702A), hex(0xF5A468), hex(0xEADFD4),
         hex(0x958372), hex(0xE06A28), hex(0xC83C46)},
        // Sunflower: yellow on dark olive.
        {hex(0x15130B), hex(0x252114), hex(0x352F1D), hex(0x463E27), hex(0xD9AE2B), hex(0xF2D36B), hex(0xE9E4D2),
         hex(0x928B72), hex(0xE0B42E), hex(0x8FB04A)},
        // Ocean: teal on deep sea.
        {hex(0x0B1416), hex(0x142225), hex(0x1D3135), hex(0x274045), hex(0x2AA8A8), hex(0x6FD6D2), hex(0xD6E5E5),
         hex(0x7B9192), hex(0x22A6A6), hex(0x3F7FD8)},
        // Amethyst: purple on dark violet.
        {hex(0x120E1A), hex(0x1E1829), hex(0x2B2239), hex(0x392D4A), hex(0x8E5CD9), hex(0xBF9BF2), hex(0xE1DBEA),
         hex(0x8A8197), hex(0x8A55D8), hex(0xC45A9E)},
        // Lavender: purple on pale lilac.
        {hex(0xEEEAF4), hex(0xFBF9FE), hex(0xE1DAEC), hex(0xD3CAE2), hex(0x8459C8), hex(0x5E37A0), hex(0x2A2433),
         hex(0x7A7187), hex(0xB89AE8), hex(0xE8A8C8)},
        // Sky: blue on pale sky.
        {hex(0xE8EEF4), hex(0xF8FBFE), hex(0xD9E3EE), hex(0xCAD7E5), hex(0x3D7FD0), hex(0x23599E), hex(0x1F2833),
         hex(0x6D7A88), hex(0x8EB8EE), hex(0x8ED8D0)},
        // Blossom: pink on pale rose.
        {hex(0xF5EBEE), hex(0xFEF9FA), hex(0xEDDCE1), hex(0xE2CBD2), hex(0xC94F78), hex(0x9C2E55), hex(0x30232A),
         hex(0x86717A), hex(0xEEA0B8), hex(0xF0C080)},
    }};
    return list;
}

const Palette& palette(int theme) {
    const auto& list = palettes();
    return list[static_cast<std::size_t>(std::clamp(theme, 0, static_cast<int>(list.size()) - 1))];
}

bool isLight(int theme) {
    const auto& themes = uiThemes();
    return themes[static_cast<std::size_t>(std::clamp(theme, 0, static_cast<int>(themes.size()) - 1))].light;
}

// --- Background drawing --------------------------------------------------------------

// A soft disc of light: full colour in the middle, fading out to nothing at `radius`.
void glow(ImDrawList* drawList, ImVec2 center, float radius, ImU32 color) {
    constexpr int kSegments = 48;
    const ImU32 middle = (color & ~IM_COL32_A_MASK) | ((((color >> IM_COL32_A_SHIFT) & 0xFF) * 45 / 100) << IM_COL32_A_SHIFT);
    const ImU32 clear = color & ~IM_COL32_A_MASK;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    drawList->PrimReserve(kSegments * 9, 1 + 2 * kSegments);
    const auto base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
    drawList->PrimWriteVtx(center, uv, color);
    for (int i = 0; i < kSegments; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / kSegments;
        drawList->PrimWriteVtx(ImVec2(center.x + std::cos(angle) * radius * 0.45f, center.y + std::sin(angle) * radius * 0.45f), uv, middle);
    }
    for (int i = 0; i < kSegments; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / kSegments;
        drawList->PrimWriteVtx(ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius), uv, clear);
    }
    for (int i = 0; i < kSegments; ++i) {
        const auto inner = static_cast<ImDrawIdx>(base + 1 + i);
        const auto innerNext = static_cast<ImDrawIdx>(base + 1 + (i + 1) % kSegments);
        const auto outer = static_cast<ImDrawIdx>(base + 1 + kSegments + i);
        const auto outerNext = static_cast<ImDrawIdx>(base + 1 + kSegments + (i + 1) % kSegments);
        drawList->PrimWriteIdx(base);
        drawList->PrimWriteIdx(inner);
        drawList->PrimWriteIdx(innerNext);
        drawList->PrimWriteIdx(inner);
        drawList->PrimWriteIdx(outer);
        drawList->PrimWriteIdx(outerNext);
        drawList->PrimWriteIdx(inner);
        drawList->PrimWriteIdx(outerNext);
        drawList->PrimWriteIdx(innerNext);
    }
}

// A band of light along a slow wave across the whole width, bright in its middle and
// fading towards both edges.
void ribbon(ImDrawList* drawList, ImVec2 size, float baseline, float amplitude, float thickness, float phase, ImU32 color) {
    constexpr int kColumns = 64;
    const ImU32 clear = color & ~IM_COL32_A_MASK;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    drawList->PrimReserve(kColumns * 12, (kColumns + 1) * 3);
    const auto base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
    for (int i = 0; i <= kColumns; ++i) {
        const float u = static_cast<float>(i) / kColumns;
        const float x = -20.0f + (size.x + 40.0f) * u;
        const float y = baseline + amplitude * std::sin(u * 5.0f + phase) + amplitude * 0.35f * std::sin(u * 11.0f - phase * 1.3f);
        // Thinner towards the ends, so the band seems to come and go.
        const float half = thickness * (0.55f + 0.45f * std::sin(u * 3.14159f));
        drawList->PrimWriteVtx(ImVec2(x, y - half), uv, clear);
        drawList->PrimWriteVtx(ImVec2(x, y), uv, color);
        drawList->PrimWriteVtx(ImVec2(x, y + half), uv, clear);
    }
    for (int i = 0; i < kColumns; ++i) {
        const auto a = static_cast<ImDrawIdx>(base + i * 3);
        const auto b = static_cast<ImDrawIdx>(base + (i + 1) * 3);
        for (int row = 0; row < 2; ++row) {
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(a + row));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(b + row));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(b + row + 1));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(a + row));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(b + row + 1));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(a + row + 1));
        }
    }
}

ImU32 u32(ImVec4 color, float alpha) { return ImGui::ColorConvertFloat4ToU32(withAlpha(color, alpha)); }

// A stable pseudo-random number in 0..1 for index `i` and channel `k`.
float hash(int i, int k) {
    const float value = std::sin(static_cast<float>(i) * 12.9898f + static_cast<float>(k) * 78.233f) * 43758.5453f;
    return value - std::floor(value);
}

}  // namespace

void setFontDensity(float pixelsPerPoint) {
    if (pixelsPerPoint <= 0.0f || pixelsPerPoint == gFontDensity) return;
    gFontDensity = pixelsPerPoint;
    if (gFontsLoaded) loadFonts();
}

void setSerifFont(bool serif) {
    if (serif == gSerif && gFontsLoaded) return;
    gSerif = serif;
    if (gFontsLoaded) loadFonts();
}

ImFont* headlineFont() { return gHeadlineFont != nullptr ? gHeadlineFont : ImGui::GetFont(); }
ImFont* titleFont() { return gTitleFont != nullptr ? gTitleFont : ImGui::GetFont(); }

ThemeColors themeColors(int theme) {
    const Palette& p = palette(theme);
    ThemeColors colors;
    colors.light = isLight(theme);
    colors.accentText = p.accentText;
    colors.error = colors.light ? ImVec4(0.78f, 0.22f, 0.18f, 1.0f) : ImVec4(0.90f, 0.42f, 0.38f, 1.0f);
    colors.background = p.background;
    return colors;
}

void applyTheme(int theme) {
    if (!gFontsLoaded) loadFonts();
    const bool light = isLight(theme);
    if (light) {
        ImGui::StyleColorsLight();
    } else {
        ImGui::StyleColorsDark();
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 7.0f;
    style.PopupRounding = 10.0f;
    style.GrabRounding = 7.0f;
    style.TabRounding = 7.0f;
    style.ScrollbarRounding = 7.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBarBorderSize = 1.0f;
    style.WindowPadding = ImVec2(18.0f, 18.0f);
    style.FramePadding = ImVec2(11.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ScrollbarSize = 11.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(14.0f, 4.0f);
    style.CellPadding = ImVec2(10.0f, 7.0f);

    const Palette& p = palette(theme);
    // Panels let a little of the background light through.
    const ImVec4 panel = withAlpha(p.panel, light ? 0.84f : 0.80f);
    const ImVec4 subtle = light ? ImVec4(0.0f, 0.0f, 0.0f, 0.08f) : ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDisabled;
    c[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);  // the background is painted by drawBackground()
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_Border] = subtle;
    c[ImGuiCol_FrameBg] = p.raised;
    c[ImGuiCol_FrameBgHovered] = mix(p.hover, p.accent, 0.10f);
    c[ImGuiCol_FrameBgActive] = mix(p.hover, p.accent, 0.20f);
    c[ImGuiCol_TitleBg] = p.panel;
    c[ImGuiCol_TitleBgActive] = p.panel;
    c[ImGuiCol_Button] = p.raised;
    c[ImGuiCol_ButtonHovered] = mix(p.hover, p.accent, 0.18f);
    c[ImGuiCol_ButtonActive] = p.accent;
    c[ImGuiCol_Header] = mix(p.raised, p.accent, 0.22f);
    c[ImGuiCol_HeaderHovered] = mix(p.hover, p.accent, 0.12f);
    c[ImGuiCol_HeaderActive] = mix(p.hover, p.accent, 0.35f);
    c[ImGuiCol_CheckMark] = p.accentText;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accentText;
    c[ImGuiCol_Separator] = subtle;
    c[ImGuiCol_SeparatorHovered] = p.accent;
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.raised;
    c[ImGuiCol_ScrollbarGrabHovered] = p.hover;
    c[ImGuiCol_ScrollbarGrabActive] = p.hover;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, light ? 0.30f : 0.55f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = light ? ImVec4(0.0f, 0.0f, 0.0f, 0.022f) : ImVec4(1.0f, 1.0f, 1.0f, 0.022f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0, 0, 0, 0);  // headers are quiet text over a rule, see ui::tableHeaders()
    c[ImGuiCol_TableBorderLight] = withAlpha(subtle, subtle.w * 0.7f);
    c[ImGuiCol_TableBorderStrong] = subtle;
    c[ImGuiCol_Tab] = withAlpha(p.raised, 0.85f);
    c[ImGuiCol_TabHovered] = mix(p.hover, p.accent, 0.25f);
    c[ImGuiCol_TabSelected] = p.accent;
    c[ImGuiCol_TabSelectedOverline] = p.accentText;
    c[ImGuiCol_TabDimmed] = withAlpha(p.raised, 0.85f);
    c[ImGuiCol_TabDimmedSelected] = p.accent;
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_TextSelectedBg] = withAlpha(p.accent, 0.35f);
    c[ImGuiCol_NavCursor] = p.accentText;
}

void drawBackground(int theme, double time, bool animate) {
    const Palette& p = palette(theme);
    const bool light = isLight(theme);
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    // Slow enough to notice only when looking for it. A still picture uses a pleasant moment.
    const float t = animate ? static_cast<float>(std::fmod(time, 100000.0)) : 40.0f;

    // Base: a faint diagonal gradient, the top a touch lighter (darker in light themes).
    const ImVec4 lift = light ? mix(p.background, ImVec4(1, 1, 1, 1), 0.35f) : mix(p.background, p.raised, 0.45f);
    const ImVec4 sink = light ? mix(p.background, p.raised, 0.35f) : mix(p.background, ImVec4(0, 0, 0, 1), 0.35f);
    drawList->AddRectFilledMultiColor(ImVec2(0, 0), size, ImGui::ColorConvertFloat4ToU32(lift),
                                      ImGui::ColorConvertFloat4ToU32(p.background),
                                      ImGui::ColorConvertFloat4ToU32(sink),
                                      ImGui::ColorConvertFloat4ToU32(p.background));

    // Large, drifting pools of light.
    const float extent = std::max(size.x, size.y);
    const float glowAlpha = light ? 0.30f : 0.22f;
    struct Pool {
        float x, y, speedX, speedY, radius, phase;
        bool second;
    };
    static constexpr Pool kPools[] = {
        {0.22f, 0.25f, 0.045f, 0.036f, 0.55f, 0.0f, false},
        {0.80f, 0.70f, 0.034f, 0.048f, 0.60f, 2.1f, true},
        {0.55f, 0.05f, 0.026f, 0.040f, 0.45f, 4.0f, false},
    };
    for (const Pool& pool : kPools) {
        const ImVec2 center(size.x * (pool.x + 0.20f * std::sin(t * pool.speedX + pool.phase)),
                            size.y * (pool.y + 0.22f * std::cos(t * pool.speedY + pool.phase * 1.7f)));
        const float breathe = 1.0f + 0.12f * std::sin(t * 0.11f + pool.phase);
        glow(drawList, center, extent * pool.radius * breathe, u32(pool.second ? p.glowB : p.glowA, glowAlpha));
    }

    // Two soft ribbons of light flowing across the lower half.
    const float ribbonAlpha = light ? 0.24f : 0.17f;
    ribbon(drawList, size, size.y * 0.64f, size.y * 0.09f, size.y * 0.12f, t * 0.24f, u32(p.glowA, ribbonAlpha));
    ribbon(drawList, size, size.y * 0.74f, size.y * 0.07f, size.y * 0.08f, t * 0.17f + 2.5f, u32(p.glowB, ribbonAlpha * 0.85f));
    ribbon(drawList, size, size.y * 0.30f, size.y * 0.06f, size.y * 0.06f, -t * 0.13f + 1.0f, u32(p.glowA, ribbonAlpha * 0.5f));

    // A few motes rising very slowly.
    const ImVec4 moteColor = light ? p.accent : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 56; ++i) {
        const float speed = 8.0f + 18.0f * hash(i, 1);
        const float span = size.y + 40.0f;
        const float y = size.y + 20.0f - std::fmod(hash(i, 2) * span + t * speed, span);
        const float x = hash(i, 3) * size.x + 14.0f * std::sin(t * (0.10f + 0.12f * hash(i, 4)) + static_cast<float>(i));
        const float twinkle = 0.5f + 0.5f * std::sin(t * (0.4f + hash(i, 5)) + static_cast<float>(i) * 1.3f);
        const float alpha = (light ? 0.16f : 0.13f) * (0.35f + 0.65f * twinkle);
        drawList->AddCircleFilled(ImVec2(x, y), 1.0f + 1.6f * hash(i, 6), u32(moteColor, alpha), 12);
    }
}

}  // namespace app
