// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/user_settings.h"
#include "driveclub/profile_options.h"
#include "driveclub/launch_check.h"
#include "driveclub/launcher.h"
#include "driveclub/widescreen.h"

#ifndef _WIN32
#include <SDL3/SDL.h>
#include <ImGuiFileDialog.h>
#include <imgui.h>
#include "imgui/big_picture/imgui_impl_sdl3_big_picture.h"
#include "imgui/big_picture/imgui_impl_sdlrenderer3.h"
#include "imgui/renderer/font_stack.h"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h must precede the control and shell headers.
#include <commctrl.h>
#include <shobjidl.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace DriveclubPC::Launcher {

namespace {

namespace fs = std::filesystem;

struct Settings {
    std::string game_dir;
    std::string ps4_username;
    bool discord_rpc = false;
    int target_fps = 60;
    int present_mode = 0; ///< Linux: Immediate, Mailbox, FIFO (in that order)
    bool fullscreen = true;
    int width = 0; ///< 0 = the primary display's resolution
    int height = 0;
    bool fsr = true;
    int sharpening = 1; ///< 0 off, 1 normal, 2 strong
    int readbacks = 2;  ///< driveclub-pc: 0 off, 1 relaxed, 2 precise (default: Relaxed broke
                       ///< shadows and vegetation in the owner's races)
    // driveclub-pc: image quality and hybrid CPU settings.
    int af = 16;
    int aa = 1;
    bool pcore_pin = true;
    // Scene resolution; below 1080p the menu/HUD framebuffer remains native.
    int render_res = 1080;
    bool async_shaders = true; ///< shader translation and pipeline creation off-thread
    bool show_fps = false;
    bool deferred_recording = false;
    bool classic_vblank = false;
    bool show_launcher = true;
};

fs::path IniPath() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "driveclub_pc.ini";
}

Settings Load() {
    Settings s;
    if (const auto* user = UserManagement.GetUserByPlayerIndex(1))
        s.ps4_username = user->user_name;
    s.discord_rpc = EmulatorSettings.IsDiscordRPCEnabled();
    std::ifstream in(IniPath());
    std::map<std::string, std::string> kv;
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) {
            line.erase(0, 3); // UTF-8 byte order mark
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos && line[0] != '#') {
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    const auto num = [&](const char* key, int fallback) {
        const auto it = kv.find(key);
        return it == kv.end() ? fallback : std::atoi(it->second.c_str());
    };
    if (kv.contains("game_dir")) {
        s.game_dir = kv["game_dir"];
    }
    s.target_fps = num("target_fps", s.target_fps);
    s.discord_rpc = num("discord_rpc", s.discord_rpc) != 0;
    s.present_mode = std::clamp(num("present_mode", s.present_mode), 0, 2);
    s.fullscreen = num("fullscreen", s.fullscreen) != 0;
    s.width = num("width", s.width);
    s.height = num("height", s.height);
    s.fsr = num("fsr", s.fsr) != 0;
    s.sharpening = std::clamp(num("sharpening", s.sharpening), 0, 2);
    s.readbacks = std::clamp(num("readbacks", s.readbacks), 0, 2);
    s.af = num("af", s.af);
    if (s.af != 2 && s.af != 4 && s.af != 8 && s.af != 16)
        s.af = 0;
    s.aa = std::clamp(num("aa", s.aa), 0, 2);
    s.pcore_pin = num("pcore_pin", 1) != 0;
    s.render_res = num("render_res", s.render_res);
    if (!IsRenderResolution(s.render_res)) {
        s.render_res = 1080;
    }
    s.async_shaders = num("async_shaders", 1) != 0;
    s.show_fps = num("show_fps", s.show_fps) != 0;
    s.deferred_recording = num("deferred_recording", s.deferred_recording) != 0;
    s.classic_vblank = num("classic_vblank", s.classic_vblank) != 0;
    s.show_launcher = num("show_launcher", s.show_launcher) != 0;
    if (s.target_fps != 30 && s.target_fps != 60 && s.target_fps != 120) {
        s.target_fps = 60;
    }
    return s;
}

bool Save(const Settings& s) {
    std::ofstream out(IniPath());
    out << "# Driveclub-PC launcher settings\n"
        << "game_dir=" << s.game_dir << "\n"
        << "discord_rpc=" << int(s.discord_rpc) << "\n"
        << "target_fps=" << s.target_fps << "\n"
        << "present_mode=" << s.present_mode << "\n"
        << "fullscreen=" << int(s.fullscreen) << "\n"
        << "width=" << s.width << "\n"
        << "height=" << s.height << "\n"
        << "fsr=" << int(s.fsr) << "\n"
        << "sharpening=" << s.sharpening << "\n"
        << "readbacks=" << s.readbacks << "\n"
        << "af=" << s.af << "\n"
        << "aa=" << s.aa << "\n"
        << "pcore_pin=" << int(s.pcore_pin) << "\n"
        << "render_res=" << s.render_res << "\n"
        << "async_shaders=" << int(s.async_shaders) << "\n"
        << "show_fps=" << int(s.show_fps) << "\n"
        << "deferred_recording=" << int(s.deferred_recording) << "\n"
        << "classic_vblank=" << int(s.classic_vblank) << "\n"
        << "show_launcher=" << int(s.show_launcher) << "\n";
    out.flush();
    return bool(out);
}

bool SaveProfileOptions(const Settings& s, std::string& error) {
    if (!IsValidLocalUserName(s.ps4_username)) {
        error = "PS4 username must contain 1–16 UTF-8 bytes, no control characters, "
                "and cannot consist only of spaces.";
        return false;
    }
    auto* user = UserManagement.GetUserByPlayerIndex(1);
    if (!user) {
        error = "No local PS4 profile is assigned to player 1. Check users.json.";
        return false;
    }
    if (user->user_name != s.ps4_username) {
        const auto old_name = user->user_name;
        UserManagement.RenameUser(user->user_id, s.ps4_username);
        if (!UserSettings.Save()) {
            user->user_name = old_name;
            error = "Could not save the PS4 username. Check data directory permissions.";
            return false;
        }
    }
    EmulatorSettings.SetDiscordRPCEnabled(s.discord_rpc);
    if (!EmulatorSettings.Save()) {
        error = "Could not save the Discord RPC setting. Check data directory permissions.";
        return false;
    }
    return true;
}

std::pair<int, int> DisplaySize() {
    int width = 1920, height = 1080;
#ifdef _WIN32
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth > 0) {
        width = static_cast<int>(mode.dmPelsWidth);
        height = static_cast<int>(mode.dmPelsHeight);
    }
#else
    const bool init_video = !(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO);
    if (!init_video || SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        if (const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())) {
            width = mode->w;
            height = mode->h;
        }
        if (init_video) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }
#endif
    return {width, height};
}

/// driveclub-pc: the game's output size: the render resolution lines at the display's aspect
/// in fullscreen or the window's aspect in windowed mode (wider than 16:9: widescreen.h).
Widescreen::Output TargetOutput(const Settings& s) {
    auto [w, h] = DisplaySize();
    if (!s.fullscreen && s.width > 0 && s.height > 0) {
        w = s.width;
        h = s.height;
    }
    return Widescreen::ComputeOutput(w, h, PresentationLines(static_cast<u32>(s.render_res)));
}

/// driveclub-pc: extra video memory the game needs for its render targets at this size.
int ExtraVramMb(const Widescreen::Output& out) {
    const u64 pixels = u64{out.width} * out.height;
    return pixels <= 1920u * 1080u ? 0 : pixels <= 2560u * 1440u ? 1024 : 1536;
}

/// Writes the graphics settings into the per-game emulator config, keeping its other keys.
void WriteGameConfig(const Settings& s) {
    const auto title_id = GameTitleId(s.game_dir);
    if (!title_id) return;
    EnsureDefaultGameConfig(*title_id);
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                      fmt::format("{}.json", *title_id);
    nlohmann::json json = nlohmann::json::object();
    {
        std::ifstream in(path);
        if (in) {
            json = nlohmann::json::parse(in, nullptr, false);
            if (json.is_discarded() || !json.is_object()) {
                json = nlohmann::json::object();
            }
        }
    }
    const auto [dw, dh] = DisplaySize();
    auto& gpu = json["GPU"];
    gpu["full_screen"] = s.fullscreen;
    gpu["full_screen_mode"] = s.fullscreen ? "Fullscreen (Borderless)" : "Windowed";
    gpu["window_width"] = s.width > 0 ? s.width : dw;
    gpu["window_height"] = s.height > 0 ? s.height : dh;
    gpu["fsr_enabled"] = s.fsr;
    gpu["rcas_enabled"] = s.sharpening != 0;
    gpu["rcas_attenuation"] = s.sharpening == 2 ? 100 : 250;
    gpu["readbacks_mode"] = s.readbacks;
#ifdef __linux__
    static constexpr std::array modes{"Immediate", "Mailbox", "Fifo"};
    gpu["present_mode"] = modes[s.present_mode];
#endif
    // driveclub-pc: a higher render resolution needs a bigger video-memory budget in the game
    // (DCPC_VRAM_EXTRA_MB below), backed by extra direct memory.
    json["General"]["extra_dmem_in_mbytes"] = ExtraVramMb(TargetOutput(s));
    std::ofstream out(path);
    out << json.dump(2) << "\n";
}

void SetEnvDefault(const char* name, const std::string& value) {
    // A value already in the environment (test scripts) wins.
    if (std::getenv(name) == nullptr) {
#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        setenv(name, value.c_str(), 0);
#endif
    }
}

void ApplyRuntime(const Settings& s) {
    EmulatorSettings.SetDiscordRPCEnabled(s.discord_rpc);
    EmulatorSettings.SetDiscordRPCEnabled(s.discord_rpc, true);
#ifdef __linux__
    static constexpr std::array modes{"Immediate", "Mailbox", "Fifo"};
    EmulatorSettings.SetPresentMode(modes[s.present_mode]);
    EmulatorSettings.SetPresentMode(modes[s.present_mode], true);
#endif
    SetEnvDefault("DCPC_AF", std::to_string(s.af));
    SetEnvDefault("DCPC_AA", std::to_string(s.aa));
    SetEnvDefault("DCPC_PCORE_PIN", s.pcore_pin ? "1" : "0");
    SetEnvDefault("DCPC_ASYNC_SHADERS", s.async_shaders ? "1" : "0");
    SetEnvDefault("DCPC_ASYNC_PIPELINES", s.async_shaders ? "1" : "0");
    const auto out = TargetOutput(s);
    const auto scene_lines = s.render_res < 1080 ? static_cast<unsigned>(s.render_res) : out.height;
    if (scene_lines != 1080) {
        // Below native, scale the 3D targets only; leave the native UI/output buffers intact.
        // Above native, retain the established matched scene/output path and memory budget.
        SetEnvDefault("DCPC_GAME_ARGS", std::string(RenderScaleArgument(scene_lines)));
    }
    LOG_INFO(Loader, "Render policy: scene target {}p, output {}x{}, native UI preserved below 1080p",
             scene_lines, out.width, out.height);
    if (out.wide || out.height != 1080) {
        // The game's display buffers follow DCPC_RENDER_RES.
        SetEnvDefault("DCPC_VRAM_EXTRA_MB", std::to_string(ExtraVramMb(out)));
        SetEnvDefault("DCPC_RENDER_RES", fmt::format("{}x{}", out.width, out.height));
    }
    if (out.wide) {
        // Aspect, Hor+ FOV and a wide scene (widescreen.h).
        SetEnvDefault("DCPC_WIDESCREEN", fmt::format("{}x{}", out.width, out.height));
    }
    SetEnvDefault("DCPC_TARGET_FPS", std::to_string(s.target_fps));
    SetEnvDefault("DCPC_DEFERRED_RECORDING", s.deferred_recording ? "1" : "0");
    SetEnvDefault("DCPC_CLASSIC_VBLANK", s.classic_vblank ? "1" : "0");
    if (s.show_fps) {
        EmulatorSettings.SetShowFpsCounter(true);
    }
}

std::string FirmwareStatus(bool& ok) {
    std::string error;
    ok = ValidateSysModules(error);
    return ok ? "Firmware modules found." : error;
}

#ifdef _WIN32

std::wstring Widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

enum Id : int {
    IdGameEdit = 100,
    IdUsername,
    IdDiscordRpc,
    IdBrowse,
    IdGameStatus,
    IdFirmwareStatus,
    IdImport,
    IdFps,
    IdDisplay,
    IdResolution,
    IdRenderRes,
    IdUpscaling,
    IdSharpening,
    IdReadbacks,
    IdAf,
    IdAa,
    IdPcore,
    IdAsyncShaders,
    IdShowFps,
    IdDeferred,
    IdClassic,
    IdShowLauncher,
    IdPlay,
    IdQuit,
};

struct Ui {
    Settings settings;
    HWND window{};
    HFONT font{}, title_font{};
    std::map<int, HWND> controls;
    bool game_ok = false;
    bool firmware_ok = false;
    bool play = false;
    float scale = 1.0f;
    std::vector<std::pair<int, int>> resolutions;
};

Ui* g_ui = nullptr;

int Px(float dip) {
    return static_cast<int>(dip * g_ui->scale + 0.5f);
}

std::optional<std::wstring> PickFolder(HWND owner, const wchar_t* title) {
    std::optional<std::wstring> result;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return result;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title);
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::string GetText(int id) {
    const HWND h = g_ui->controls[id];
    const int n = GetWindowTextLengthW(h);
    std::wstring w(static_cast<size_t>(n), L'\0');
    GetWindowTextW(h, w.data(), n + 1);
    return Narrow(w);
}

void SetText(int id, const std::string& text) {
    SetWindowTextW(g_ui->controls[id], Widen(text).c_str());
}

void RefreshStatus() {
    auto& ui = *g_ui;
    const std::string dir = GetText(IdGameEdit);
    std::string message;
    if (dir.empty()) {
        ui.game_ok = false;
        message = "Choose the Driveclub base game folder. Other title IDs are experimental.";
    } else {
        std::string error;
        const auto eboot = ResolveAndValidateGame(dir, error);
        ui.game_ok = eboot.has_value();
        message = ui.game_ok ? GameCompatibilityText(eboot->parent_path()) : error;
    }
    std::replace(message.begin(), message.end(), '\n', ' ');
    SetText(IdGameStatus, message);
    bool fw = false;
    std::string fw_text = FirmwareStatus(fw);
    std::replace(fw_text.begin(), fw_text.end(), '\n', ' ');
    ui.firmware_ok = fw;
    SetText(IdFirmwareStatus, fw_text);
    EnableWindow(ui.controls[IdPlay], ui.game_ok && ui.firmware_ok);
    InvalidateRect(ui.window, nullptr, TRUE);
}

void ImportFirmware() {
    const auto source = PickFolder(g_ui->window, L"Folder with your PS4 firmware modules (*.sprx)");
    if (!source) {
        return;
    }
    const fs::path dest = EmulatorSettings.GetSysModulesDir();
    std::error_code ec;
    fs::create_directories(dest, ec);
    int copied = 0;
    for (const auto& entry : fs::directory_iterator(*source, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".sprx") {
            fs::copy_file(entry.path(), dest / entry.path().filename(),
                          fs::copy_options::skip_existing, ec);
            copied += ec ? 0 : 1;
        }
    }
    MessageBoxW(g_ui->window,
                Widen(fmt::format("Copied {} module(s) into\n{}", copied, dest.string())).c_str(),
                L"Driveclub-PC", MB_OK | MB_ICONINFORMATION);
    RefreshStatus();
}

int ComboIndex(int id) {
    return static_cast<int>(SendMessageW(g_ui->controls[id], CB_GETCURSEL, 0, 0));
}

bool Checked(int id) {
    return SendMessageW(g_ui->controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void CollectSettings() {
    auto& s = g_ui->settings;
    s.game_dir = GetText(IdGameEdit);
    s.ps4_username = GetText(IdUsername);
    s.discord_rpc = Checked(IdDiscordRpc);
    static constexpr int fps_values[] = {30, 60, 120};
    s.target_fps = fps_values[std::clamp(ComboIndex(IdFps), 0, 2)];
    s.fullscreen = ComboIndex(IdDisplay) == 0;
    const int res =
        std::clamp(ComboIndex(IdResolution), 0, static_cast<int>(g_ui->resolutions.size()) - 1);
    s.width = g_ui->resolutions[res].first;
    s.height = g_ui->resolutions[res].second;
    s.fsr = ComboIndex(IdUpscaling) == 0;
    s.sharpening = std::clamp(ComboIndex(IdSharpening), 0, 2);
    s.readbacks = 2 - std::clamp(ComboIndex(IdReadbacks), 0, 2);
    static constexpr int af_values[] = {0, 2, 4, 8, 16};
    s.af = af_values[std::clamp(ComboIndex(IdAf), 0, 4)];
    s.aa = std::clamp(ComboIndex(IdAa), 0, 2);
    s.pcore_pin = Checked(IdPcore);
    static constexpr int render_values[] = {480, 576, 720, 1080, 1440, 2160};
    s.render_res = render_values[std::clamp(ComboIndex(IdRenderRes), 0, 5)];
    s.async_shaders = Checked(IdAsyncShaders);
    s.show_fps = Checked(IdShowFps);
    s.deferred_recording = Checked(IdDeferred);
    s.classic_vblank = Checked(IdClassic);
    s.show_launcher = Checked(IdShowLauncher);
}

/// driveclub-pc: the window size only applies in windowed mode.
void UpdateWindowSizeEnabled() {
    const auto it = g_ui->controls.find(IdResolution);
    if (it != g_ui->controls.end()) {
        EnableWindow(it->second, ComboIndex(IdDisplay) == 1);
    }
}

HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, float x, float y, float w, float h,
          int id, DWORD ex_style = 0) {
    HWND hwnd =
        CreateWindowExW(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style, Px(x), Px(y), Px(w),
                        Px(h), g_ui->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui->font), TRUE);
    if (id > 0) {
        g_ui->controls[id] = hwnd;
    }
    return hwnd;
}

HWND Combo(int id, float x, float y, float w, const std::vector<std::wstring>& items, int sel) {
    HWND h = Make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, x, y, w, 200, id);
    for (const auto& item : items) {
        SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    SendMessageW(h, CB_SETCURSEL, sel, 0);
    return h;
}

void Label(const wchar_t* text, float x, float y, float w, float h = 20) {
    Make(L"STATIC", text, SS_LEFT, x, y + 3, w, h, 0);
}

void BuildControls() {
    auto& ui = *g_ui;
    const auto& s = ui.settings;
    constexpr float L = 24, C = 170, W = 400, Row = 36;
    float y = 72;

    Label(L"PS4 username", L, y, 140);
    Make(L"EDIT", Widen(s.ps4_username).c_str(), ES_AUTOHSCROLL | WS_TABSTOP,
         C, y, W, 24, IdUsername, WS_EX_CLIENTEDGE);
    y += 32;
    Make(L"BUTTON", L"Discord Rich Presence", BS_AUTOCHECKBOX | WS_TABSTOP,
         C, y, W, 22, IdDiscordRpc);
    SendMessageW(ui.controls[IdDiscordRpc], BM_SETCHECK,
                 s.discord_rpc ? BST_CHECKED : BST_UNCHECKED, 0);
#ifndef ENABLE_DISCORD_RPC
    EnableWindow(ui.controls[IdDiscordRpc], FALSE);
#endif
    y += 32;

    Label(L"Game folder", L, y, 140);
    Make(L"EDIT", Widen(s.game_dir).c_str(), ES_AUTOHSCROLL | WS_TABSTOP, C, y, W - 96, 24,
         IdGameEdit, WS_EX_CLIENTEDGE);
    Make(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, C + W - 88, y - 1, 88, 26, IdBrowse);
    y += 28;
    Make(L"STATIC", L"", SS_LEFT, C, y, W, 34, IdGameStatus);
    y += 40;

    Label(L"Firmware", L, y, 140);
    Make(L"STATIC", L"", SS_LEFT, C, y + 3, W - 96, 34, IdFirmwareStatus);
    Make(L"BUTTON", L"Import...", BS_PUSHBUTTON | WS_TABSTOP, C + W - 88, y - 1, 88, 26, IdImport);
    y += 48;

    Label(L"Frame rate", L, y, 140);
    Combo(IdFps, C, y, W,
          {L"30 FPS (as on PS4)", L"60 FPS (recommended)", L"120 FPS (experimental)"},
          s.target_fps == 30    ? 0
          : s.target_fps == 120 ? 2
                                : 1);
    y += Row;

    Label(L"Display", L, y, 140);
    Combo(IdDisplay, C, y, W, {L"Fullscreen (borderless)", L"Window"}, s.fullscreen ? 0 : 1);
    y += Row;

    const auto [dw, dh] = DisplaySize();
    // driveclub-pc: the three ultrawide sizes get a matching wide output (widescreen.h).
    ui.resolutions = {{0, 0},       {5120, 1440}, {3840, 2160}, {3440, 1440},
                      {2560, 1600}, {2560, 1440}, {2560, 1080}, {1920, 1200},
                      {1920, 1080}, {1600, 900},  {1280, 720}};
    std::vector<std::wstring> res_items;
    int res_sel = 0;
    for (size_t i = 0; i < ui.resolutions.size(); ++i) {
        const auto [w, h] = ui.resolutions[i];
        res_items.push_back(i == 0 ? Widen(fmt::format("Same as the display ({}x{})", dw, dh))
                                   : Widen(fmt::format("{}x{}", w, h)));
        if (w == s.width && h == s.height) {
            res_sel = static_cast<int>(i);
        }
    }
    // driveclub-pc: borderless fullscreen always covers the whole display; this is only the size
    // of the window in windowed mode.
    Label(L"Window size", L, y, 140);
    Combo(IdResolution, C, y, W, res_items, res_sel);
    UpdateWindowSizeEnabled();
    y += Row;

    // driveclub-pc: lines; the width follows the display (or window) aspect, see widescreen.h.
    Label(L"Render resolution", L, y, 140);
    Combo(IdRenderRes, C, y, W,
          {L"480p scene (experimental, native UI)", L"576p scene (experimental, native UI)",
           L"720p scene (native UI)", L"1080p (native)", L"1440p (sharper, about 10-15% slower)",
           L"2160p (very demanding)"},
          s.render_res == 2160   ? 5
          : s.render_res == 1440 ? 4
          : s.render_res == 720  ? 2
          : s.render_res == 576  ? 1
          : s.render_res == 480  ? 0
                                 : 3);
    y += Row;

    Label(L"Upscaling", L, y, 140);
    Combo(IdUpscaling, C, y, W, {L"AMD FSR 1 (game image to window size)", L"Bilinear"},
          s.fsr ? 0 : 1);
    y += Row;

    Label(L"Sharpening", L, y, 140);
    Combo(IdSharpening, C, y, W, {L"Off", L"Normal (RCAS)", L"Strong (RCAS)"}, s.sharpening);
    y += Row;

    Label(L"Anisotropic filter", L, y, 140);
    Combo(IdAf, C, y, W, {L"Off (game setting)", L"2x", L"4x", L"8x", L"16x"},
          s.af == 16  ? 4
          : s.af == 8 ? 3
          : s.af == 4 ? 2
          : s.af == 2 ? 1
                      : 0);
    y += Row;
    Label(L"Anti-aliasing", L, y, 140);
    Combo(IdAa, C, y, W, {L"Off", L"FXAA (default)", L"SMAA 1x (High)"}, s.aa);
    y += Row;

    Label(L"GPU readbacks", L, y, 140);
    Combo(IdReadbacks, C, y, W,
          {L"Precise (default)", L"Relaxed (faster, shadow/vegetation glitches)",
           L"Off (fastest, effects may break)"},
          2 - s.readbacks);
    y += Row + 6;

    Make(L"BUTTON", L"Prefer performance cores (hybrid CPUs)", BS_AUTOCHECKBOX | WS_TABSTOP, C, y,
         W, 22, IdPcore);
    SendMessageW(ui.controls[IdPcore], BM_SETCHECK, s.pcore_pin ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Compile shaders in the background (less stutter)",
         BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdAsyncShaders);
    SendMessageW(ui.controls[IdAsyncShaders], BM_SETCHECK,
                 s.async_shaders ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Show FPS counter", BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdShowFps);
    SendMessageW(ui.controls[IdShowFps], BM_SETCHECK, s.show_fps ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Experimental: record Vulkan commands on a second thread",
         BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdDeferred);
    SendMessageW(ui.controls[IdDeferred], BM_SETCHECK,
                 s.deferred_recording ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Classic PS4 frame pacing (fallback if timing looks wrong)",
         BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdClassic);
    SendMessageW(ui.controls[IdClassic], BM_SETCHECK,
                 s.classic_vblank ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 26;
    Make(L"BUTTON", L"Show this window at start (hold Shift to open it anyway)",
         BS_AUTOCHECKBOX | WS_TABSTOP, C, y, W, 22, IdShowLauncher);
    SendMessageW(ui.controls[IdShowLauncher], BM_SETCHECK,
                 s.show_launcher ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 44;

    Make(L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, C + W - 196, y, 96, 32, IdPlay);
    Make(L"BUTTON", L"Quit", BS_PUSHBUTTON | WS_TABSTOP, C + W - 92, y, 92, 32, IdQuit);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
        SetBkMode(dc, TRANSPARENT);
        if (id == IdGameStatus) {
            SetTextColor(dc, g_ui->game_ok ? RGB(20, 120, 40) : RGB(170, 30, 30));
        } else if (id == IdFirmwareStatus) {
            SetTextColor(dc, g_ui->firmware_ok ? RGB(20, 120, 40) : RGB(170, 30, 30));
        }
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_PRINTCLIENT:
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wp);
        if (msg == WM_PRINTCLIENT) {
            RECT client;
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
        }
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, g_ui->title_font);
        RECT r{Px(24), Px(14), Px(600), Px(44)};
        DrawTextW(dc, L"Driveclub-PC", -1, &r, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, g_ui->font);
        SetTextColor(dc, RGB(90, 90, 90));
        RECT r2{Px(24), Px(44), Px(600), Px(64)};
        DrawTextW(dc,
                  L"PS4 Driveclub. Other versions are experimental. Settings and saves: the 'user' folder next to the "
                  L"program.",
                  -1, &r2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, old);
        if (msg == WM_PAINT) {
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IdBrowse:
            if (const auto dir = PickFolder(hwnd, L"Driveclub base game folder (other versions experimental)")) {
                SetWindowTextW(g_ui->controls[IdGameEdit], dir->c_str());
            }
            return 0;
        case IdGameEdit:
            if (HIWORD(wp) == EN_CHANGE) {
                RefreshStatus();
            }
            return 0;
        case IdImport:
            ImportFirmware();
            return 0;
        case IdDisplay:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                UpdateWindowSizeEnabled();
            }
            return 0;
        case IdPlay:
            CollectSettings();
            {
                std::string error;
                if (!SaveProfileOptions(g_ui->settings, error)) { ShowError(error); return 0; }
            }
            g_ui->play = true;
            DestroyWindow(hwnd);
            return 0;
        case IdQuit:
            CollectSettings();
            {
                std::string error;
                if (!SaveProfileOptions(g_ui->settings, error)) { ShowError(error); return 0; }
            }
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        CollectSettings();
        {
            std::string error;
            if (!SaveProfileOptions(g_ui->settings, error)) { ShowError(error); return 0; }
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/// Returns true when the user pressed Play; `s` holds the chosen settings either way.
bool ShowWindow(Settings& s) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    Ui ui;
    ui.settings = s;
    g_ui = &ui;
    ui.scale = static_cast<float>(GetDpiForSystem()) / 96.0f;

    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = -Px(14);
    ui.font = CreateFontIndirectW(&lf);
    lf.lfHeight = -Px(24);
    lf.lfWeight = FW_SEMIBOLD;
    ui.title_font = CreateFontIndirectW(&lf);

    const HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"DriveclubPCLauncher";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    RECT rect{0, 0, Px(600), Px(820)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, GetDpiForSystem());
    const int w = rect.right - rect.left, h = rect.bottom - rect.top;
    ui.window = CreateWindowExW(
        0, wc.lpszClassName, L"Driveclub-PC", style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    BuildControls();
    RefreshStatus();
    ::ShowWindow(ui.window, SW_SHOW);
    SetForegroundWindow(ui.window);
    if (const char* snapshot = std::getenv("DCPC_LAUNCHER_SNAPSHOT")) {
        // Test hook: save an image of this window only (24-bit BMP) and close it.
        for (int i = 0; i < 40; ++i) {
            MSG pending;
            while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&pending);
                DispatchMessageW(&pending);
            }
            Sleep(10);
        }
        RECT rc;
        GetWindowRect(ui.window, &rc);
        const int sw = rc.right - rc.left, sh = rc.bottom - rc.top;
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), sw, -sh, 1, 24, BI_RGB};
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mem, bmp);
        SendMessageW(ui.window, WM_PRINT, reinterpret_cast<WPARAM>(mem),
                     PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        const DWORD stride = static_cast<DWORD>((sw * 3 + 3) & ~3);
        BITMAPFILEHEADER fh{
            0x4D42,
            static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + stride * sh),
            0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
        std::ofstream f(snapshot, std::ios::binary);
        f.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
        f.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
        f.write(static_cast<const char*>(bits), static_cast<std::streamsize>(stride) * sh);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        DestroyWindow(ui.window);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(ui.window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    s = ui.settings;
    DeleteObject(ui.font);
    DeleteObject(ui.title_font);
    UnregisterClassW(wc.lpszClassName, inst);
    g_ui = nullptr;
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return ui.play;
}

#else
#include "driveclub/launcher_linux.inl"
#endif

} // namespace

std::optional<fs::path> Run(bool force) {
    Settings s = Load();
    std::string error;
    const bool game_ok = !s.game_dir.empty() && ResolveAndValidateGame(s.game_dir, error);
    bool firmware_ok = false;
    FirmwareStatus(firmware_ok);
#ifdef _WIN32
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (s.show_launcher || force || shift || !game_ok || !firmware_ok) {
        const bool play = ShowWindow(s);
        if (!Save(s)) {
            ShowError("Could not save launcher settings. Check data directory permissions.");
            return std::nullopt;
        }
        if (!play) {
            return std::nullopt;
        }
    }
#else
    // The Linux launcher also provides a way to re-open settings after automatic launch.
    if (s.show_launcher || force || !game_ok || !firmware_ok) {
        if (!ShowLinuxWindow(s)) {
            return std::nullopt;
        }
    }
#endif
    // Persist and start the discovered base, rather than handing a containing folder to the core.
    const auto resolved = ResolveAndValidateGame(s.game_dir, error);
    if (!resolved) {
        ShowError(error);
        return std::nullopt;
    }
    s.game_dir = resolved->parent_path().string();
    if (!Save(s)) {
        ShowError("Could not save launcher settings. Check data directory permissions.");
        return std::nullopt;
    }
    WriteGameConfig(s);
    ApplyRuntime(s);
    const auto out = TargetOutput(s);
    LOG_INFO(Loader, "Launcher: {} FPS, {} {}x{}, {}, sharpening {}, output {}x{}{}",
             s.target_fps, s.fullscreen ? "fullscreen" : "window", s.width, s.height,
             s.fsr ? "FSR 1" : "bilinear", s.sharpening, out.width, out.height,
             out.wide ? " (widescreen)" : "");
    return fs::path(s.game_dir);
}

void ApplySavedRuntimeOptions() {
    std::error_code ec;
    if (fs::exists(IniPath(), ec)) {
        ApplyRuntime(Load());
    }
}

void ApplySavedProfileOptions() {
    std::error_code ec;
    if (fs::exists(IniPath(), ec)) {
        const auto s = Load();
        EmulatorSettings.SetDiscordRPCEnabled(s.discord_rpc);
        EmulatorSettings.SetDiscordRPCEnabled(s.discord_rpc, true);
    }
}

} // namespace DriveclubPC::Launcher
