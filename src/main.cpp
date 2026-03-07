#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>

#include <unordered_map>
#include <string>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

void OpenURL(const char* url) {
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

// Globals for Win32 subclassing
static WNDPROC g_OriginalWndProc = nullptr;
static HWND    gHwnd             = nullptr;

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_NCHITTEST:
        {
            // If ImGui isn't ready yet, fall back to default behavior
            if (ImGui::GetCurrentContext() == nullptr)
                break;

            // Mouse position in screen coords
            POINT p;
            p.x = GET_X_LPARAM(lParam);
            p.y = GET_Y_LPARAM(lParam);

            // Convert to client coords
            ScreenToClient(hwnd, &p);

            // Feed ImGui the mouse position (so hover tests are correct)
            ImGuiIO& io = ImGui::GetIO();
            io.MousePos = ImVec2((float)p.x, (float)p.y);

            // Check if hovering any ImGui widget or window
            bool hoveringUI =
                ImGui::IsAnyItemHovered() ||
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
            bool wantsKeyboard =
                ImGui::GetIO().WantTextInput ||
                ImGui::GetIO().WantCaptureKeyboard;

            if (hoveringUI || wantsKeyboard)
                return HTCLIENT;        // allow mouse + keyboard
            else
                return HTCLIENT;   // pass through mouse, but still allow keyboard (e.g. for global shortcuts)
        }
    }

    return CallWindowProc(g_OriginalWndProc, hwnd, msg, wParam, lParam);
}
static std::unordered_map<ImGuiID, float> g_SwitchAnim;

inline float LerpFloat(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

bool ToggleSwitch(const char* id, bool* v, ImVec4 onColor, ImVec4 offColor, ImVec4 knobColor)
{
    ImGui::PushID(id);
    ImGuiID switchID = ImGui::GetID("##switch_anim");

    float& anim = g_SwitchAnim[switchID];   // each switch gets its own animation float

    const float width  = 40.0f;
    const float height = 20.0f;
    const float radius = height * 0.5f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##switch", ImVec2(width, height));

    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    if (clicked)
        *v = !*v;

    float target = *v ? 1.0f : 0.0f;
    anim = LerpFloat(anim, target, 0.18f);

    ImVec4 bg = LerpColor(offColor, onColor, anim);
    if (hovered)
        bg = ImVec4(bg.x + 0.05f, bg.y + 0.05f, bg.z + 0.05f, bg.w);

    ImU32 bgCol   = ImGui::GetColorU32(bg);
    ImU32 knobCol = ImGui::GetColorU32(knobColor);

    ImGui::GetWindowDrawList()->AddRectFilled(
        p,
        ImVec2(p.x + width, p.y + height),
        bgCol,
        radius
    );

    float knobX = p.x + radius + anim * (width - height);

    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(knobX, p.y + radius),
        radius - 2.0f,
        knobCol
    );

    ImGui::PopID();
    return clicked;
}
void SettingsRow(const char* id, const char* title, const char* desc,
                 bool* value, ImFont* fontXS,
                 ImVec4 onColor, ImVec4 offColor, ImVec4 knobColor)
{
    ImGui::PushID(id);

    float fullWidth   = ImGui::GetContentRegionAvail().x;
    float switchWidth = 40.0f;
    float rowHeight   = 78.0f;

    // Reserve the row area
    ImVec2 rowStart = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##row_click", ImVec2(fullWidth, rowHeight));
    bool rowClicked = ImGui::IsItemClicked();

    if (rowClicked)
        *value = !*value;

    // Reset cursor to draw content on top of the invisible button
    ImGui::SetCursorScreenPos(rowStart);

    // LEFT — title
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
    ImGui::TextUnformatted(title);

    // LEFT — description
    if (desc && desc[0] != '\0')
    {
        ImGui::PushFont(fontXS);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
        ImGui::TextWrapped(desc);

        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    // RIGHT — switch
    float switchX = rowStart.x + fullWidth - switchWidth - 10.0f;
    float switchY = rowStart.y + 10.0f;

    ImGui::SetCursorScreenPos(ImVec2(switchX, switchY));
    ToggleSwitch("##switch", value, onColor, offColor, knobColor);

    ImGui::PopID();
    
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
}
void OptionRow(const char* id, const char* title, const char* desc,
               int* currentIndex, const char* const* options, int optionCount,
               ImFont* fontXS)

{
    ImGui::PushID(id);

    float fullWidth     = ImGui::GetContentRegionAvail().x;
    float rowHeight     = 76.0f;

    // Full-row click zone
    ImVec2 rowStart = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##row_click", ImVec2(fullWidth, rowHeight));
    bool rowClicked = ImGui::IsItemClicked();

    // Reset cursor to draw content on top
    ImGui::SetCursorScreenPos(rowStart);

    // LEFT — title
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
    ImGui::TextUnformatted(title);

    // LEFT — description
    if (desc && desc[0] != '\0')
    {
        ImGui::PushFont(fontXS);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1);
        ImGui::TextWrapped(desc);

        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::PushFont(fontXS);

    // Measure text width
    ImVec2 textSize = ImGui::CalcTextSize(options[*currentIndex]);

    // Add padding (left + right)
    float dynamicWidth = textSize.x + 20.0f;

    // RIGHT — selector (dynamic width)
    float selectorX = rowStart.x + fullWidth - dynamicWidth - 10.0f;
    float selectorY = rowStart.y - 2.0f;

    ImGui::SetCursorScreenPos(ImVec2(selectorX, selectorY));

    // Draw selector button with dynamic width
    if (ImGui::Button(options[*currentIndex], ImVec2(dynamicWidth, 0)))
        ImGui::OpenPopup("popup");

    ImGui::PopFont();


    // Clicking the row also opens the popup
    if (rowClicked)
        ImGui::OpenPopup("popup");

    // POPUP WINDOW ABOVE — with rounded corners
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);   // roundness
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10)); // nicer padding

    if (ImGui::BeginPopup("popup"))
    {
        for (int i = 0; i < optionCount; i++)
        {
            bool selected = (*currentIndex == i);
            if (ImGui::Selectable(options[i], selected))
            {
                *currentIndex = i;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2); // restore rounding + padding

        ImGui::PopID();
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
    }

int main()
{
    FreeConsole();
    if (!glfwInit())
        return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // No title bar
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    // Transparent framebuffer
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    // --- GET SCREEN SIZE BEFORE CREATING WINDOW ---
    RECT screen;
    GetWindowRect(GetDesktopWindow(), &screen);

    int screenWidth  = screen.right  - screen.left;
    int screenHeight = screen.bottom - screen.top - 1; // taskbar height compensation (optional)

    // --- CREATE FULLSCREEN-SIZED WINDOW ---
    GLFWwindow* window = glfwCreateWindow(
        screenWidth,
        screenHeight,
        "CORSPRITE client",
        NULL,
        NULL
    );

    if (!window)
        return -1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Win32 handle
    HWND hwnd = glfwGetWin32Window(window);
    gHwnd = hwnd;

    // Always on top
    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE
    );

    // Transparent layered overlay window, hidden from taskbar/Alt-Tab
    LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW;   // hide from Alt-Tab + taskbar
    ex &= ~WS_EX_APPWINDOW;   // prevent taskbar button
    ex |= WS_EX_LAYERED;      // transparency
    ex |= WS_EX_TOPMOST;      // always on top
    SetWindowLong(hwnd, GWL_EXSTYLE, ex);
    SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 255, LWA_ALPHA);

    // Subclass the window for custom hit-testing
    g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(
        hwnd,
        GWLP_WNDPROC,
        (LONG_PTR)OverlayWndProc
    );

    // --- INIT IMGUI ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImFont* fontSmallest  = io.Fonts->AddFontFromFileTTF("fonts/Roboto-VariableFont_wdth,wght.ttf", 12.0f);
    ImFont* fontSmall     = io.Fonts->AddFontFromFileTTF("fonts/Roboto-VariableFont_wdth,wght.ttf", 18.0f);
    ImFont* fontMedium    = io.Fonts->AddFontFromFileTTF("fonts/Roboto-VariableFont_wdth,wght.ttf", 22.0f);
    ImFont* fontLarge     = io.Fonts->AddFontFromFileTTF("fonts/Roboto-VariableFont_wdth,wght.ttf", 32.0f);
    io.FontDefault = fontMedium;

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 6.0f;
    s.FrameRounding    = 5.0f;
    s.GrabRounding     = 5.0f;
    s.WindowBorderSize = 0.0f;
    s.Colors[ImGuiCol_WindowBg].w = 0.90f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // UI state
    ImVec4 circleColor = ImVec4(0.1f, 1.0f, 0.1f, 1.0f);
    float radius = 3.0f;
    
    float alertHeight = screen.bottom - screen.top - 360.0f; // taskbar compensation + main panel height

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GLuint myImageLogoTexture = 0;
    int imgWidth = 0, imgHeight = 0;

    {
        int channels;
        unsigned char* data = stbi_load("images/logo.png", &imgWidth, &imgHeight, &channels, 4);

        glGenTextures(1, &myImageLogoTexture);
        glBindTexture(GL_TEXTURE_2D, myImageLogoTexture);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imgWidth, imgHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    
    static bool enableAssistant   = true;
    static bool alwaysOnTop       = true;
    static bool showNotifications = true;
    static bool autoSaveConfig    = true;

    static bool interactiveMode = true;
    static bool followCursor    = false;
    static bool idleAnimations  = true;

    std::ifstream file("assistant_config.json");
    if (file.is_open())
    {
        json config;
        file >> config;
        file.close();

        // -----------------------------
        // ASSISTANT — CORE SETTINGS
        // -----------------------------
        if (config.contains("assistant"))
        {
            auto& a = config["assistant"];

            if (a.contains("enable"))           enableAssistant   = a["enable"];
            if (a.contains("always_on_top"))    alwaysOnTop       = a["always_on_top"];
            if (a.contains("notifications"))    showNotifications = a["notifications"];
            if (a.contains("auto_save"))        autoSaveConfig    = a["auto_save"];

            if (a.contains("interactive_mode")) interactiveMode   = a["interactive_mode"];
            if (a.contains("follow_cursor"))    followCursor      = a["follow_cursor"];
            if (a.contains("idle_animations"))  idleAnimations    = a["idle_animations"];
        }

        // -----------------------------
        // INDICATOR SETTINGS
        // -----------------------------
        if (config.contains("indicator"))
        {
            auto& ind = config["indicator"];

            if (ind.contains("color") && ind["color"].is_array() && ind["color"].size() == 3)
            {
                circleColor.x = ind["color"][0];
                circleColor.y = ind["color"][1];
                circleColor.z = ind["color"][2];
            }

            if (ind.contains("radius")) radius = ind["radius"];
        }
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // --- START IMGUI FRAME ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- LEFT PANEL (INTERACTIVE) ---
        ImGuiStyle& s = ImGui::GetStyle();

        // WINDOW SHAPE
        s.WindowRounding   = 18.0f;
        s.FrameRounding    = 10.0f;
        s.GrabRounding     = 10.0f;
        s.ChildRounding    = 14.0f;

        // PADDING
        s.WindowPadding    = ImVec2(20, 20);
        s.FramePadding     = ImVec2(12, 8);
        s.ItemSpacing      = ImVec2(12, 10);
        s.ItemInnerSpacing = ImVec2(8, 6);

        // BORDERS
        s.WindowBorderSize = 0.0f;
        s.FrameBorderSize  = 0.0f;

        // COLORS

        s.Colors[ImGuiCol_FrameBg]          = ImVec4(0.20f, 0.20f, 0.20f, 0.80f);
        s.Colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.25f, 0.25f, 0.25f, 0.90f);
        s.Colors[ImGuiCol_FrameBgActive]    = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

        s.Colors[ImGuiCol_Button]           = ImVec4(0.20f, 0.20f, 0.20f, 0.80f);
        s.Colors[ImGuiCol_ButtonHovered]    = ImVec4(0.30f, 0.30f, 0.30f, 0.90f);
        s.Colors[ImGuiCol_ButtonActive]     = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);

        s.Colors[ImGuiCol_Header]           = ImVec4(0.25f, 0.25f, 0.25f, 0.80f);
        s.Colors[ImGuiCol_HeaderHovered]    = ImVec4(0.35f, 0.35f, 0.35f, 0.90f);
        s.Colors[ImGuiCol_HeaderActive]     = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

        s.Colors[ImGuiCol_Separator]        = ImVec4(0.35f, 0.35f, 0.35f, 0.50f);
        s.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.50f, 0.50f, 0.50f, 0.70f);
        s.Colors[ImGuiCol_SeparatorActive]  = ImVec4(0.60f, 0.60f, 0.60f, 0.80f);

        s.Colors[ImGuiCol_Text]             = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        s.Colors[ImGuiCol_TextDisabled]     = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(360, io.DisplaySize.y - 40), ImGuiCond_Always);

        ImGui::Begin("MainUI", NULL,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar
        );

        // Logo
        ImGui::Image(
            (void*)(intptr_t)myImageLogoTexture,
            ImVec2(60, 60)
        );

        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 20);

        ImGui::PushFont(fontLarge);
        ImGui::Text("ORSPRITE");
        ImGui::PopFont();

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        
        ImGui::BeginChild("Windowcontaindata", ImVec2(0, alertHeight), true);
        ImGui::Spacing();

        static int languageIndex = 0;

        const char* languageOptions[] =
        {
            "English",
            "French",
            "German",
            "Spanish",
            "Italian",
            "Japanese",
            "Korean"
        };

        OptionRow(
            "assistant_language",
            "Language",
            "Select the language used for CORSPRITE assistant responses.",
            &languageIndex,
            languageOptions,
            IM_ARRAYSIZE(languageOptions),
            fontSmall
        );
        ImGui::Spacing();
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 0.30f, 0.00f, 0.90f));
        ImGui::BeginChild("AlertBox", ImVec2(0, 76), true);

            ImGui::PushFont(fontSmall);
            ImGui::TextWrapped("Welcome to CORSPRITE!");
            ImGui::TextWrapped("Please read the documentation before using the assistant.");

            ImGui::PopFont();


        ImGui::EndChild();
        
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
        ImGui::Spacing();
        if (ImGui::Button("View Documentation", ImVec2(-1, 40))) {
            OpenURL("https://corsprite-docs.vercel.app/");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        ImGui::Spacing();

        // Shared toggle colors
        ImVec4 onColor  = ImVec4(0.10f, 0.45f, 1.00f, 1.0f);
        ImVec4 offColor = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        ImVec4 knob     = ImVec4(1, 1, 1, 1);

        // ASSISTANT — CORE SETTINGS
        ImGui::Separator();
        ImGui::PushFont(fontMedium);
        ImGui::Text("Assistant");
        ImGui::PopFont();

        ImGui::PushFont(fontSmall);
        ImGui::Text("Global behavior and visibility.");
        ImGui::PopFont();
        SettingsRow(
            "assistant_enable",
            "Enable Assistant",
            "Turns the on‑screen AI assistant on or off.",
            &enableAssistant,
            fontSmall, onColor, offColor, knob
        );

        SettingsRow(
            "assistant_topmost",
            "Always On Top",
            "Keeps the assistant visible above all other windows and games.",
            &alwaysOnTop,
            fontSmall, onColor, offColor, knob
        );

        SettingsRow(
            "assistant_notifications",
            "Notifications",
            "Shows alerts, reminders, and assistant responses.",
            &showNotifications,
            fontSmall, onColor, offColor, knob
        );

        SettingsRow(
            "assistant_autosave",
            "Auto‑Save Settings",
            "Automatically saves configuration changes as you make them.",
            &autoSaveConfig,
            fontSmall, onColor, offColor, knob
        );


        // ASSISTANT — INTERACTION & BEHAVIOR
        
        ImGui::Separator();
        ImGui::PushFont(fontMedium);
        ImGui::Text("Interaction & Behavior");
        ImGui::PopFont();

        ImGui::PushFont(fontSmall);
        ImGui::Text("Customize how the assistant reacts.");
        ImGui::PopFont();

        SettingsRow(
            "assistant_interactive",
            "Interactive Mode",
            "Allows clicking and interacting directly with the assistant.",
            &interactiveMode,
            fontSmall, onColor, offColor, knob
        );

        SettingsRow(
            "assistant_follow_cursor",
            "Follow Cursor",
            "Makes the assistant react to or follow your mouse movement.",
            &followCursor,
            fontSmall, onColor, offColor, knob
        );

        SettingsRow(
            "assistant_idle_anim",
            "Idle Animations",
            "Plays subtle animations when the assistant is idle.",
            &idleAnimations,
            fontSmall, onColor, offColor, knob
        );

        ImGui::Separator();

        ImGui::PushFont(fontMedium);
        ImGui::Text("Indicator (TOP RIGHT)");
        ImGui::PopFont();

        ImGui::PushFont(fontSmall);
        ImGui::Text("Enabled when corsprite is on.");
        ImGui::PopFont();

        ImGui::PushFont(fontSmall);
        ImGui::ColorEdit3("Color", (float*)&circleColor);
        ImGui::SliderFloat("Radius", &radius, 1.0f, 100.0f);
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);


        ImVec4 baseColor   = ImVec4(0.10f, 0.45f, 0.90f, 1.00f);
        ImVec4 hoverColor  = ImVec4(0.15f, 0.55f, 1.00f, 1.00f);
        ImVec4 activeColor = ImVec4(0.05f, 0.35f, 0.80f, 1.00f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 10));
        ImGui::PushStyleColor(ImGuiCol_Button,        baseColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  activeColor);

        ImGui::PushFont(fontMedium);
        bool pressed = ImGui::Button("Apply & Start Client", ImVec2(-1, 45));
        ImGui::PopFont();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        if (pressed)
        {
            json config;

            // -----------------------------
            // ASSISTANT — CORE SETTINGS
            // -----------------------------
            config["assistant"]["enable"]          = enableAssistant;
            config["assistant"]["always_on_top"]   = alwaysOnTop;
            config["assistant"]["notifications"]   = showNotifications;
            config["assistant"]["auto_save"]       = autoSaveConfig;

            // -----------------------------
            // INTERACTION & BEHAVIOR
            // -----------------------------
            config["assistant"]["interactive_mode"] = interactiveMode;
            config["assistant"]["follow_cursor"]    = followCursor;
            config["assistant"]["idle_animations"]  = idleAnimations;

            // -----------------------------
            // INDICATOR SETTINGS
            // -----------------------------
            config["indicator"]["color"] = {
                circleColor.x,
                circleColor.y,
                circleColor.z
            };

            config["indicator"]["radius"] = radius;

            // -----------------------------
            // SAVE TO FILE
            // -----------------------------
            std::ofstream file("assistant_config.json");
            if (file.is_open())
            {
                file << config.dump(4); // pretty print with 4‑space indent
                file.close();
            }

            // Close window after saving
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        
        // Quit button (styled to match your UI)
        ImVec4 quitBase   = ImVec4(0.25f, 0.25f, 0.25f, 0.85f);
        ImVec4 quitHover  = ImVec4(0.35f, 0.35f, 0.35f, 0.95f);
        ImVec4 quitActive = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
        ImGui::PushStyleColor(ImGuiCol_Button,        quitBase);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, quitHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  quitActive);

        ImGui::PushFont(fontMedium);
        bool quitPressed = ImGui::Button("Quit CORSPRITE", ImVec2(-1, 40));
        ImGui::PopFont();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        if (quitPressed)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        ImGui::Spacing();
        ImGui::Spacing();

        // Footer text (centered)
        ImGui::PushFont(fontMedium);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("© 2026 CORSPRITE").x) * 0.5f);
        ImGui::Text("© 2026 CORSPRITE");
        ImGui::PopFont();

        ImGui::PushFont(fontSmallest);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("Version Alpha 0.1").x) * 0.5f);
        ImGui::Text("Version Alpha 0.1");
        ImGui::PopFont();

        ImGui::End();

        // --- DRAW CIRCLE (STATUS DOT) ---
        ImVec2 center(
            io.DisplaySize.x - radius - 10.0f,
            radius + 10.0f
        );

        ImGui::GetForegroundDrawList()->AddCircleFilled(
            center,
            radius,
            ImColor(circleColor),
            64
        );

        ImGui::Render();

        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0, 0, 0, 0.6f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
