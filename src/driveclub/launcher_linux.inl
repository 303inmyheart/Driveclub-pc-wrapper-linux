// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Included inside the launcher's private namespace so both frontends share Settings,
// validation, persistence and runtime options.
bool ShowLinuxWindow(Settings& s) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ShowError(fmt::format("Cannot open the settings window: {}", SDL_GetError()));
        return false;
    }
    auto* window = SDL_CreateWindow("Driveclub-PC", 820, 800,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    auto* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        ShowError(fmt::format("Cannot create the settings renderer: {}", SDL_GetError()));
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_SetWindowMinimumSize(window, 680, 540);
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImFontConfig font;
    io.FontDefault = ImGui::FontStack::AddPrimaryUiFont(
        io.Fonts, 17.0f, EmulatorSettings.GetConsoleLanguage(), font, true);
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(24, 20);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 10);
    style.FrameRounding = 4;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.08f, 0.10f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.10f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.32f, 0.45f, 1.0f);
    const float scale = std::max(1.0f, SDL_GetWindowDisplayScale(window));
    io.FontGlobalScale = scale;
    style.ScaleAllSizes(scale);
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) ||
        !ImGui_ImplSDLRenderer3_Init(renderer)) {
        ShowError("Cannot initialize the settings interface.");
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    std::array<char, 4096> game{};
    std::array<char, 4096> dlc{};
    std::array<char, 4096> saves{};
    std::snprintf(saves.data(), saves.size(), "%s", s.save_dir.c_str());
    std::array<char, 128> username{};
    std::snprintf(username.data(), username.size(), "%s", s.ps4_username.c_str());
    std::string profile_status;
    std::string firmware_details;
    std::snprintf(game.data(), game.size(), "%s", s.game_dir.c_str());
    std::snprintf(dlc.data(), dlc.size(), "%s", EmulatorSettings.GetAddonInstallDir().c_str());
    std::string game_status, firmware_status, import_status;
    std::string dlc_status;
    bool game_ok = false, firmware_ok = false;
    const auto refresh_dlc = [&] {
        const auto folder = dlc[0] ? fs::path(dlc.data()) :
            Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "addcont";
        dlc_status = DescribeAddonFolder(folder, GameTitleId(s.game_dir).value_or(std::string(ReferenceTitleId)));
    };
    const auto refresh = [&] {
        s.game_dir = game.data();
        game_status.clear();
        const auto resolved = s.game_dir.empty() ? std::nullopt :
                              ResolveAndValidateGame(s.game_dir, game_status);
        game_ok = resolved.has_value();
        if (s.game_dir.empty()) {
            game_status = "Choose Driveclub or a containing folder. Subfolders are searched automatically.";
        } else if (game_ok) {
            s.game_dir = resolved->parent_path().string();
            game_status = GameCompatibilityText(s.game_dir) + "\n" + s.game_dir;
            bool update_found = false;
            for (const auto suffix : {"-UPDATE", "-patch"}) {
                const auto update = fs::path(s.game_dir + suffix);
                std::error_code ec;
                if (fs::is_directory(update, ec)) {
                    game_status += fmt::format("\nUpdate found: {}", update.filename().string());
                    update_found = true;
                    break;
                }
            }
            if (!update_found) game_status += "\nNo update folder found next to the base game.";
        }
        const auto report = InspectSysModules(GameTitleId(s.game_dir).value_or(std::string(ReferenceTitleId)));
        firmware_ok = report.ready;
        firmware_status = report.summary;
        firmware_details = report.details;
        if (!firmware_ok) {
            firmware_status = "Import your PS4 sys_modules folder to continue.";
        }
        refresh_dlc();
    };
    refresh();
    const auto persist = [&] {
        refresh();
        std::error_code ec;
        const auto addon_dir = dlc[0] ? fs::absolute(fs::path(dlc.data()), ec) : fs::path{};
        if (ec) {
            dlc_status = "Could not resolve the selected DLC folder.";
            return false;
        }
        EmulatorSettings.SetAddonInstallDir(addon_dir);
        if (!PrepareSaveRoot(s, profile_status) || !SaveProfileOptions(s, profile_status)) return false;
        if (!Save(s)) {
            profile_status = "Could not save launcher settings. Check data directory permissions.";
            return false;
        }
        return true;
    };
    const auto folder_dialog = [&](const char* key, const char* title) {
        ImGuiFileDialog::Instance()->OpenDialog(
            key, title, nullptr, ".", 1, nullptr,
            ImGuiFileDialogFlags_DisableCreateDirectoryButton | ImGuiFileDialogFlags_Modal);
    };
    const auto combo_value = [](const char* label, int& value, const auto& values,
                                const char* labels) {
        const auto found = std::find(values.begin(), values.end(), value);
        int index = found == values.end() ? 0 : static_cast<int>(found - values.begin());
        if (ImGui::Combo(label, &index, labels)) {
            value = values[index];
        }
    };

    bool running = true, play = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                running = false;
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::Begin("Driveclub-PC settings", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("DRIVECLUB-PC");
        ImGui::TextDisabled("Linux / Vulkan");
        ImGui::Separator();
        ImGui::BeginChild("Settings", ImVec2(0, -104 * scale));
        ImGui::TextUnformatted("Local PS4 profile");
        if (ImGui::InputText("PS4 username", username.data(), username.size())) {
            s.ps4_username = username.data();
            profile_status.clear();
        }
        ImGui::TextDisabled("Changes the player-1 profile name; keeps its ID and saves.");
#ifndef ENABLE_DISCORD_RPC
        ImGui::BeginDisabled();
#endif
        ImGui::Checkbox("Discord Rich Presence", &s.discord_rpc);
#ifndef ENABLE_DISCORD_RPC
        ImGui::EndDisabled();
        ImGui::TextDisabled("Discord RPC is not included in this build.");
#else
        ImGui::TextDisabled("Show your current game in Discord (requires running Discord).");
#endif
        if (!profile_status.empty()) ImGui::TextWrapped("%s", profile_status.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("Save data");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##save-root", "Default save root (leave empty)", saves.data(), saves.size()))
            s.save_dir = saves.data();
        if (ImGui::Button("Choose save folder...")) folder_dialog("dcpc-saves", "Choose save data root");
        ImGui::SameLine();
        if (ImGui::Button("Use default save root")) { saves[0] = 0; s.save_dir.clear(); }
        ImGui::TextWrapped("Active root: %s", PathText(SaveRoot(s)).c_str());
        ImGui::TextDisabled("Existing saves are not moved automatically.");
        ImGui::Separator();
        ImGui::TextUnformatted("Game and firmware");
        ImGui::SetNextItemWidth(-110 * scale);
        const bool game_enter = ImGui::InputText("##game", game.data(), game.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
        if (game_enter || ImGui::IsItemDeactivatedAfterEdit()) {
            refresh();
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            folder_dialog("dcpc-game", "Choose Driveclub or a containing folder");
        }
        if (game_status.starts_with("EXPERIMENTAL"))
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.25f, 1.0f));
        ImGui::TextWrapped("%s", game_status.c_str());
        if (game_status.starts_with("EXPERIMENTAL")) ImGui::PopStyleColor();
        ImGui::TextUnformatted("DLC folder (optional)");
        ImGui::SetNextItemWidth(-110 * scale);
        const bool dlc_enter = ImGui::InputText("##dlc", dlc.data(), dlc.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        if (dlc_enter || ImGui::IsItemDeactivatedAfterEdit()) refresh_dlc();
        ImGui::SameLine();
        if (ImGui::Button("Browse...##dlc")) {
            folder_dialog("dcpc-dlc", "Choose the folder containing your unpacked DLCs");
        }
        ImGui::TextWrapped("%s", dlc_status.c_str());
        ImGui::TextWrapped("%s", firmware_status.c_str());
        if (ImGui::Button("Import firmware...")) {
            folder_dialog("dcpc-firmware", "Choose your PS4 sys_modules folder");
        }
        if (ImGui::CollapsingHeader("Firmware details"))
            ImGui::TextWrapped("%s", firmware_details.c_str());
        if (!import_status.empty()) {
            ImGui::TextWrapped("%s", import_status.c_str());
        }
        ImGui::Separator();
        ImGui::PushItemWidth(230 * scale);
        int frame_mode = s.target_fps == 120 ? 3 : s.target_fps == 30 ? 0 : s.frame_generation ? 2 : 1;
        if (ImGui::Combo("Frame rate", &frame_mode,
                         "30 FPS\00060 FPS\00060 FPS + FG (experimental, ~120 output)\000120 FPS (experimental)\000")) {
            s.target_fps = frame_mode == 0 ? 30 : frame_mode == 3 ? 120 : 60;
            s.frame_generation = frame_mode == 2;
        }
        if (s.frame_generation)
            ImGui::TextWrapped("Frame generation is experimental; it adds display frames, not simulation FPS.");
        ImGui::Combo("Presentation", &s.present_mode,
                     "Immediate (may tear)\000Mailbox (VSync)\000FIFO (VSync)\000");
        combo_value("Render resolution", s.render_res, std::array{240, 360, 480, 576, 720, 1080, 1440, 2160},
                    "240p scene (experimental, native UI)\000360p scene (experimental, native UI)\000"
                    "480p scene (experimental, native UI)\000576p scene (experimental, native UI)\000"
                    "720p scene (native UI)\0001080p\0001440p\0002160p\000");
        ImGui::Checkbox("Borderless fullscreen", &s.fullscreen);
        ImGui::BeginDisabled(s.fullscreen);
        static constexpr std::array sizes{
            std::pair{0, 0}, std::pair{426, 240}, std::pair{640, 360}, std::pair{854, 480}, std::pair{1024, 576},
            std::pair{1280, 720}, std::pair{1920, 1080},
            std::pair{2560, 1440}, std::pair{3840, 2160}, std::pair{2560, 1080},
            std::pair{3440, 1440}, std::pair{5120, 1440}};
        const auto selected = std::find(sizes.begin(), sizes.end(), std::pair{s.width, s.height});
        int size_index = selected == sizes.end() ? 0 : static_cast<int>(selected - sizes.begin());
        if (ImGui::Combo("Window size", &size_index,
                         "Desktop\000426 x 240\000640 x 360\000854 x 480\0001024 x 576\0001280 x 720\0001920 x 1080\0002560 x 1440\0003840 x 2160\000"
                         "2560 x 1080\0003440 x 1440\0005120 x 1440\000")) {
            s.width = sizes[size_index].first;
            s.height = sizes[size_index].second;
        }
        ImGui::EndDisabled();
        int upscaling = s.fsr ? 0 : 1;
        if (ImGui::Combo("Upscaling", &upscaling, "AMD FSR 1\000Bilinear\000")) {
            s.fsr = upscaling == 0;
        }
        ImGui::Combo("Sharpening", &s.sharpening, "Off\000Normal\000Strong\000");
        ImGui::Combo("Anti-aliasing", &s.aa, "Off\000FXAA\000SMAA 1x High\000");
        combo_value("Anisotropic filtering", s.af, std::array{0, 2, 4, 8, 16},
                    "Game default\0002x\0004x\0008x\00016x\000");
        combo_value("GPU readbacks", s.readbacks, std::array{2, 1, 0},
                    "Precise (recommended)\000Relaxed (visual glitches)\000Off (debug only)\000");
        ImGui::PopItemWidth();
        ImGui::Checkbox("Compile shaders in the background", &s.async_shaders);
        ImGui::Checkbox("Show FPS", &s.show_fps);
        ImGui::Checkbox("Classic PS4 frame pacing", &s.classic_vblank);
        ImGui::Checkbox("Show settings at startup", &s.show_launcher);
        ImGui::TextDisabled("Reopen settings with --launcher.");
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::BeginDisabled(!game_ok || !firmware_ok);
        if (ImGui::Button("Play", ImVec2(160 * scale, 0))) {
            refresh();
            if (game_ok && firmware_ok && persist()) {
                play = true;
                running = false;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Save and close")) {
            if (persist()) running = false;
        }
        ImGui::TextWrapped("Settings: %s",
                           Common::FS::GetUserPath(Common::FS::PathType::UserDir).c_str());
        ImGui::TextWrapped("Save root: %s", PathText(SaveRoot(s)).c_str());

        for (const char* key : {"dcpc-game", "dcpc-firmware", "dcpc-dlc", "dcpc-saves"}) {
            if (ImGuiFileDialog::Instance()->Display(key, ImGuiWindowFlags_NoCollapse,
                                                     ImVec2(560 * scale, 340 * scale))) {
                if (ImGuiFileDialog::Instance()->IsOk()) {
                    const fs::path folder = ImGuiFileDialog::Instance()->GetCurrentPath();
                    if (std::string_view(key) == "dcpc-game") {
                        std::snprintf(game.data(), game.size(), "%s", folder.c_str());
                    } else if (std::string_view(key) == "dcpc-saves") {
                        s.save_dir = PathText(folder);
                        std::snprintf(saves.data(), saves.size(), "%s", s.save_dir.c_str());
                    } else if (std::string_view(key) == "dcpc-dlc") {
                        std::snprintf(dlc.data(), dlc.size(), "%s", folder.c_str());
                        refresh_dlc();
                    } else {
                        const auto dest = EmulatorSettings.GetSysModulesDir();
                        std::error_code ec;
                        fs::create_directories(dest, ec);
                        int copied = 0;
                        if (!ec) {
                            for (const auto& entry : fs::directory_iterator(folder, ec)) {
                                if (entry.is_regular_file(ec) && entry.path().extension() == ".sprx") {
                                    if (fs::copy_file(entry.path(), dest / entry.path().filename(),
                                                       fs::copy_options::skip_existing, ec)) {
                                        ++copied;
                                    }
                                    if (ec) break;
                                }
                            }
                        }
                        import_status = ec ? fmt::format("Import failed: {}", ec.message())
                                           : fmt::format("Imported {} module(s). Existing files kept.", copied);
                    }
                    refresh();
                }
                ImGuiFileDialog::Instance()->Close();
            }
        }
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 16, 20, 26, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return play;
}
