#include "app/Theme.h"

#include <string>
#include <vector>

#include "app/Paths.h"
#include "platform/Platform.h"

#include <filesystem>
#include <system_error>

#include <imgui-SFML.h>
#include <imgui.h>

namespace app {

namespace {

ImFont* gHeadlineFont = nullptr;
bool gFontsLoaded = false;
float gFontDensity = 1.0f;

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    gHeadlineFont = nullptr;
    // The platform layer knows where this system keeps its interface fonts.
    const std::vector<std::filesystem::path> candidates = platform::uiFontCandidates();
    bool loaded = false;
    std::error_code ec;
    // Sizes stay in points; on a high-DPI display the glyphs are rasterized with more pixels.
    ImFontConfig config;
    config.RasterizerDensity = gFontDensity;
    for (const std::filesystem::path& file : candidates) {
        const std::string path = pathToUtf8(file);
        if (!std::filesystem::exists(file, ec)) continue;
        if (io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f, &config) != nullptr) {
            gHeadlineFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 32.0f, &config);
            loaded = true;
            break;
        }
    }
    if (!loaded) io.Fonts->AddFontDefault(&config);
    (void)ImGui::SFML::UpdateFontTexture();
    gFontsLoaded = true;
}

struct Palette {
    ImVec4 background;
    ImVec4 panel;
    ImVec4 raised;
    ImVec4 hover;
    ImVec4 accent;
    ImVec4 accentHover;
    ImVec4 text;
    ImVec4 textDisabled;
    ImVec4 rowAlt;
};

constexpr Palette kDark = {
    ImVec4(0.086f, 0.082f, 0.071f, 1.0f),  // #161512
    ImVec4(0.149f, 0.141f, 0.129f, 1.0f),  // #262421
    ImVec4(0.216f, 0.208f, 0.192f, 1.0f),
    ImVec4(0.286f, 0.275f, 0.255f, 1.0f),
    ImVec4(0.384f, 0.600f, 0.251f, 1.0f),  // green
    ImVec4(0.447f, 0.682f, 0.298f, 1.0f),
    ImVec4(0.80f, 0.80f, 0.78f, 1.0f),
    ImVec4(0.50f, 0.50f, 0.48f, 1.0f),
    ImVec4(1.0f, 1.0f, 1.0f, 0.03f),
};

constexpr Palette kLight = {
    ImVec4(0.929f, 0.922f, 0.906f, 1.0f),  // #edebe7
    ImVec4(1.000f, 1.000f, 1.000f, 1.0f),
    ImVec4(0.878f, 0.871f, 0.851f, 1.0f),
    ImVec4(0.812f, 0.804f, 0.780f, 1.0f),
    ImVec4(0.439f, 0.647f, 0.298f, 1.0f),  // green
    ImVec4(0.345f, 0.553f, 0.216f, 1.0f),
    ImVec4(0.16f, 0.16f, 0.15f, 1.0f),
    ImVec4(0.47f, 0.46f, 0.44f, 1.0f),
    ImVec4(0.0f, 0.0f, 0.0f, 0.035f),
};

}  // namespace

void setFontDensity(float pixelsPerPoint) {
    if (pixelsPerPoint <= 0.0f || pixelsPerPoint == gFontDensity) return;
    gFontDensity = pixelsPerPoint;
    if (gFontsLoaded) loadFonts();
}

ImFont* headlineFont() { return gHeadlineFont != nullptr ? gHeadlineFont : ImGui::GetFont(); }

void applyTheme(bool light) {
    if (!gFontsLoaded) loadFonts();

    if (light) {
        ImGui::StyleColorsLight();
    } else {
        ImGui::StyleColorsDark();
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ScrollbarSize = 12.0f;

    const Palette& p = light ? kLight : kDark;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDisabled;
    c[ImGuiCol_WindowBg] = p.background;
    c[ImGuiCol_ChildBg] = p.panel;
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_Border] = p.raised;
    c[ImGuiCol_FrameBg] = p.raised;
    c[ImGuiCol_FrameBgHovered] = p.hover;
    c[ImGuiCol_FrameBgActive] = p.hover;
    c[ImGuiCol_TitleBg] = p.panel;
    c[ImGuiCol_TitleBgActive] = p.panel;
    c[ImGuiCol_Button] = p.raised;
    c[ImGuiCol_ButtonHovered] = p.hover;
    c[ImGuiCol_ButtonActive] = p.accent;
    c[ImGuiCol_Header] = p.raised;
    c[ImGuiCol_HeaderHovered] = p.hover;
    c[ImGuiCol_HeaderActive] = p.accent;
    c[ImGuiCol_CheckMark] = p.accentHover;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accentHover;
    c[ImGuiCol_Separator] = p.raised;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.raised;
    c[ImGuiCol_ScrollbarGrabHovered] = p.hover;
    c[ImGuiCol_ScrollbarGrabActive] = p.hover;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, light ? 0.35f : 0.6f);
    c[ImGuiCol_TableRowBgAlt] = p.rowAlt;
    c[ImGuiCol_TableHeaderBg] = p.raised;
    c[ImGuiCol_TableBorderLight] = p.raised;
    c[ImGuiCol_TableBorderStrong] = p.hover;
    c[ImGuiCol_Tab] = p.raised;
    c[ImGuiCol_TabHovered] = p.hover;
    c[ImGuiCol_TabSelected] = p.accent;
    c[ImGuiCol_PlotHistogram] = p.accent;
}

}  // namespace app
