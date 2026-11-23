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

void loadFont() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    // The platform layer knows where this system keeps its interface fonts.
    const std::vector<std::filesystem::path> candidates = platform::uiFontCandidates();
    bool loaded = false;
    std::error_code ec;
    for (const std::filesystem::path& file : candidates) {
        const std::string path = pathToUtf8(file);
        if (!std::filesystem::exists(file, ec)) continue;
        if (io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f) != nullptr) {
            loaded = true;
            break;
        }
    }
    if (!loaded) io.Fonts->AddFontDefault();
    (void)ImGui::SFML::UpdateFontTexture();
}

}  // namespace

void applyTheme() {
    loadFont();

    ImGui::StyleColorsDark();
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

    const ImVec4 background(0.086f, 0.082f, 0.071f, 1.0f);  // #161512
    const ImVec4 panel(0.149f, 0.141f, 0.129f, 1.0f);       // #262421
    const ImVec4 raised(0.216f, 0.208f, 0.192f, 1.0f);
    const ImVec4 hover(0.286f, 0.275f, 0.255f, 1.0f);
    const ImVec4 accent(0.384f, 0.600f, 0.251f, 1.0f);  // green
    const ImVec4 accentHover(0.447f, 0.682f, 0.298f, 1.0f);
    const ImVec4 text(0.80f, 0.80f, 0.78f, 1.0f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.48f, 1.0f);
    c[ImGuiCol_WindowBg] = background;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_Border] = raised;
    c[ImGuiCol_FrameBg] = raised;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive] = hover;
    c[ImGuiCol_TitleBg] = panel;
    c[ImGuiCol_TitleBgActive] = panel;
    c[ImGuiCol_Button] = raised;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = raised;
    c[ImGuiCol_HeaderHovered] = hover;
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_CheckMark] = accentHover;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Separator] = raised;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = raised;
    c[ImGuiCol_ScrollbarGrabHovered] = hover;
    c[ImGuiCol_ScrollbarGrabActive] = hover;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
}

}  // namespace app
