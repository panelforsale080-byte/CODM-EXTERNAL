if (isToggled)
{
   
ImVec2 disp = ImGui::GetIO().DisplaySize;
float mw = disp.x > 40.f ? disp.x * 0.92f : 830.f;
float mh = disp.y > 40.f ? disp.y * 0.72f : 500.f;
if (mw > 830.f) mw = 830.f;
if (mh > 500.f) mh = 500.f;
if (mw < 280.f) mw = 280.f;
if (mh < 220.f) mh = 220.f;
ImVec2 center2 = ImGui::GetMainViewport()->GetCenter();
ImGui::SetNextWindowPos(center2, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
ImGui::SetNextWindowSize(ImVec2(mw, mh), ImGuiCond_FirstUseEver); 
if (ImFont* __menuTitle = jnmaini_title_font()) ImGui::PushFont(__menuTitle);
if (ImGui::Begin(" ", nullptr, ImGuiWindowFlags_NoTitleBar))
{
    DrawMacOSHeaderButtons();
    ImGui::Dummy(ImVec2(0, 10));
    
    ImGui::BeginChild("##ScrollablePage", ImVec2(-1, -1), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    
    static int tab = 1;
    
    ImGui::Columns(2);
    ImGui::SetColumnOffset(1, 200); 
    
    {
        float buttonWidth = 170.0f;
        float buttonHeight = 66.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 5.0f));
        
        struct TabButton {
            const char* icon;
            int id;
            float anim;
        };
        
        static TabButton buttons[] = {
            { ICON_FA_EYE,        1, 0.0f },
            { ICON_FA_CROSSHAIRS, 2, 0.0f },
            { ICON_FA_MICROCHIP,  3, 0.0f },
            { ICON_FA_BULLSEYE,   7, 0.0f },
            { ICON_FA_COGS,       5, 0.0f },
            { ICON_FA_CLIPBOARD_LIST, 6, 0.0f },
        };
        
        ImGuiIO& io = ImGui::GetIO();
        float speed = 8.0f * io.DeltaTime; 
        
        auto Lerp = [](float a, float b, float t) {
            return a + (b - a) * t;
        };
        
        for (auto& b : buttons) {
            b.anim = Lerp(b.anim, (tab == b.id) ? 1.0f : 0.0f, speed);
            
            ImVec4 iconColor = ImVec4(
                0.42f + 0.20f * b.anim,
                0.44f + 0.20f * b.anim,
                0.65f + 0.15f * b.anim,
                1.0f
            );
            
            ImVec4 btnColor = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
            ImVec4 btnHover = ImVec4(0.22f, 0.22f, 0.26f, 1.0f);
            ImVec4 btnActive = ImVec4(0.26f, 0.26f, 0.30f, 1.0f);
            
            if (tab == b.id) {
                btnColor = ImVec4(0.42f, 0.44f, 0.65f, 0.15f);
                btnHover = ImVec4(0.42f, 0.44f, 0.65f, 0.25f);
                btnActive = ImVec4(0.42f, 0.44f, 0.65f, 0.35f);
            }
            
            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnActive);
            ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
            
            if (ImGui::Button(b.icon, ImVec2(buttonWidth, buttonHeight))) tab = b.id;
            
            ImGui::PopStyleColor(4);
            ImGui::Spacing();
        }
        
        ImGui::PopStyleVar();
        
        ImGui::NextColumn();
        ImGui::BeginChild("right_content", ImVec2(0, 0), true);
        
        if (tab == 1) {
            if (checkboxStates["ESP ENABLE"]) {
                ImGui::PushStyleColor(ImGuiCol_Button, g_widgets.m_accent);
                if (ImGui::Button("ESP ENABLED", ImVec2(-1, 80))) {
                    checkboxStates["ESP ENABLE"] = false;
                }
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
                if (ImGui::Button("ESP DISABLED", ImVec2(-1, 80))) {
                    checkboxStates["ESP ENABLE"] = true;
                }
                ImGui::PopStyleColor();
            }
            
            ImGui::Columns(2);
            g_widgets.checkbox("Line", &checkboxStates["ESP LINE"]);
            g_widgets.checkbox("2D Box", &checkboxStates["ESP BOX"]);
            g_widgets.checkbox("Name", &checkboxStates["ESP NAME"]);
            g_widgets.checkbox("Vehicle", &checkboxStates["ESP::VEHICLE"]);
            g_widgets.checkbox("3D Box", &checkboxStates["3D::BOX"]);
            g_widgets.checkbox("Armor", &checkboxStates["ESP ARMOR"]);
            
            ImGui::NextColumn();
            g_widgets.checkbox("Alert", &checkboxStates["ESP ALERT 360"]);
            g_widgets.checkbox("Health", &checkboxStates["ESP HEALTH"]);
            g_widgets.checkbox("Distance", &checkboxStates["ESP DISTANCE"]);
            g_widgets.checkbox("Circle", &checkboxStates["ESP CIRCLE"]);
            g_widgets.checkbox("Skeleton", &checkboxStates["SKELETON"]);
            g_widgets.checkbox("Enemy Counter", &checkboxStates["ENEMY::COUNTER"]);
            ImGui::Columns(1);
            
            ImGui::Spacing();
            ImGui::Columns(2, nullptr, false);
            
            ImGui::BeginGroupPanel("Line Position", ImVec2(-1, 0));
            static const char* linePositions[] = { "Bottom", "Up" };
            for (int i = 0; i < IM_ARRAYSIZE(linePositions); i++) {
                ImGui::RadioButton(linePositions[i], &dropdownStates["#LINE_POS"], i);
                if (i < IM_ARRAYSIZE(linePositions) - 1)
                    ImGui::SameLine();
            }
            ImGui::EndGroupPanel();
            
            ImGui::NextColumn();
            ImGui::BeginGroupPanel("2D Box Style", ImVec2(-1, 0));
            static const char* boxStyles[] = { "Outline", "Filled" };
            for (int i = 0; i < IM_ARRAYSIZE(boxStyles); i++) {
                ImGui::RadioButton(boxStyles[i], &dropdownStates["#BOX_STYLE"], i);
                if (i < IM_ARRAYSIZE(boxStyles) - 1)
                    ImGui::SameLine();
            }
            ImGui::EndGroupPanel();
        }
        
        if (tab == 2)
        {
            ImGui::Text(ICON_FA_EXCLAMATION_CIRCLE "  Aim");
            ImGui::Separator();
            
            ImGui::BeginGroupPanel("", ImVec2(-1, 0));
            g_widgets.checkbox("SILENT AIM", &checkboxStates["AIMBOT 360"]);
            g_widgets.checkbox("Ignore Wall", &checkboxStates["DISABLE::VIS"]);
            g_widgets.checkbox("Ignore Knocked Enemy", &checkboxStates["IGNORE KNOCKED"]);
            ImGui::EndGroupPanel();
            
            ImGui::Spacing();
            
            ImGui::BeginGroupPanel("TUNING", ImVec2(-1, 0));
            SLIDER("AIM SMOOTHNESS", &seekbarValues["AIM SMOOTHNESS"], 1.0f, 100.0f);
            SLIDER("AIM SPEED", &seekbarValues["AIM SPEED"], 1.0f, 100.0f);
            SLIDER("AIM FOV", &seekbarValues["FOV500"], 1.0f, 500.0f);
            SLIDER("[Change Recoil To Current Gun]", &seekbarValues["NO::RECOIL"], 0.0f, 100.0f);
            SLIDER("[change ADS To Current Gun]", &seekbarValues["FAST::SCOPE"], 0.0f, 100.0f);
            SLIDER("[change Reload To Current Gun]", &seekbarValues["NO::RELOAD"], 0.0f, 100.0f);
            SLIDER("[change Spread To Current Gun]", &seekbarValues["NO::SPREAD"], 0.0f, 100.0f);
            SLIDER("Aim Range", &seekbarValues["MAXDIST"], 0.0f, 200.0f);
            ImGui::EndGroupPanel();
            
            ImGui::Spacing();
            
            ImGui::BeginGroupPanel("Aiming Location", ImVec2(-1, 0));
            static const char* targets[] = { "BODY", "HEAD", "CHEST", "FEET" };
            g_widgets.dropdown("##target", &dropdownStates["#TARG"], targets, 4);
            ImGui::EndGroupPanel();
            
            ImGui::Spacing();
            
            ImGui::BeginGroupPanel("Aim Priority", ImVec2(-1, 0));
            static const char* distances[] = { "NEAREST", "FOV" };
            g_widgets.dropdown("##priority", &dropdownStates["#DIST"], distances, 2);
            ImGui::EndGroupPanel();
            
            ImGui::Spacing();
            
            ImGui::BeginGroupPanel("Trigger Condition", ImVec2(-1, 0));
            static const char* triggers[] = { "NONE", "SHOOTING", "SCOPING", "HYBRID" };
            g_widgets.dropdown("##trigger", &dropdownStates["#TRIG"], triggers, 4);
            ImGui::EndGroupPanel();
            
            ImGui::Spacing();
        }
        
        if (tab == 3)
        {
            ImGui::Text(ICON_FA_MICROCHIP "  Memory");
            ImGui::Separator();
            
            ImGui::BeginGroupPanel("", ImVec2(-1, 0));
            ImGui::Columns(2, nullptr, false);
            g_widgets.checkbox("Wallhack", &checkboxStates["WALLHACK"]);
            ImGui::NextColumn();
            g_widgets.checkbox("Maphack", &checkboxStates["ADVANCE::UAV"]);
            ImGui::NextColumn();
            g_widgets.checkbox("No Parachute", &checkboxStates["NO::PARACHUTE"]);
            ImGui::NextColumn();
            g_widgets.checkbox("Fast Swim", &checkboxStates["FAST::SWIM"]);
            ImGui::NextColumn();
            g_widgets.checkbox("Extend Hitbox", &checkboxStates["HITBOX"]);
            ImGui::NextColumn();
            g_widgets.checkbox("No Shake", &checkboxStates["NO::SHAKE"]);
            ImGui::NextColumn();
            g_widgets.checkbox("Disable Crouch", &checkboxStates["NO CROUCH"]);
            ImGui::NextColumn();
            g_widgets.checkbox("Misdirection Effect", &checkboxStates["AUTO::MISDIRECTION"]);
            ImGui::Columns(1);
            ImGui::EndGroupPanel();
            
            ImGui::Spacing();
            
            ImGui::BeginGroupPanel("Movement Adjustments", ImVec2(-1, 0));
            ImGui::Spacing();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            SLIDER("Aim Assist", &seekbarValues["AIM::ASSIST"], 0.0f, 100.0f);
            SLIDER("Fov BR", &seekbarValues["FOV"], 0.0f, 200.0f);
            SLIDER("Fov MP", &seekbarValues["FOV1"], 0.0f, 200.0f);
            SLIDER("Snowb Speed", &seekbarValues["SizeSnow"], 0.0f, 70.0f); 
            SLIDER("Loot Speed", &seekbarValues["FAST::LOOT"], 0.0f, 100.0f);
            SLIDER("Game Speed", &seekbarValues["SPEED_HACK"], 0.0f, 70.0f); 
            ImGui::PopStyleVar();
            ImGui::EndGroupPanel();
        }
        
        if (tab == 5)
        {
            ImGui::Spacing();
            
            ImGui::BeginChild("##SettingsContent", ImVec2(0, 0), true);
            {
                ImGui::BeginGroupPanel("Performance", ImVec2(-1, 0));
                ImGui::Columns(2, nullptr, false);
                g_widgets.checkbox("Force 120 / 60 Fps", &checkboxStates["FRAME120"]);
                ImGui::NextColumn();
                g_widgets.checkbox("Force 40 Fps", &checkboxStates["FRAME40"]);
                ImGui::NextColumn();
                g_widgets.checkbox("Hand Cam Mode", &checkboxStates["HAND::CAM"]);
                ImGui::NextColumn();
                g_widgets.checkbox("Disable VisCheck", &checkboxStates["DISABLE_VISCHECK"]);
                
                ImGui::Columns(1);
                ImGui::EndGroupPanel();
                
                ImGui::Spacing();
                
                ImGui::BeginGroupPanel("Esp Colors", ImVec2(-1, 0));
                static ImVec4 enemyVisibleColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); 
                static ImVec4 botVisibleColor   = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);  
                static ImVec4 notVisibleColor   = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  
                static ImVec4 knockedColor      = ImVec4(1.0f, 0.65f, 0.0f, 1.0f);
                static ImVec4 defaultEspColor   = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                ImGui::ColorEdit4("Enemy",     (float*)&enemyVisibleColor, ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine(); 
                ImGui::ColorEdit4("Bot",       (float*)&botVisibleColor,   ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine();
                ImGui::ColorEdit4("Hidden",    (float*)&notVisibleColor,   ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine();
                ImGui::ColorEdit4("Knocked",   (float*)&knockedColor,      ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Default",   (float*)&defaultEspColor,   ImGuiColorEditFlags_NoInputs);

                for (auto& color : {&enemyVisibleColor, &botVisibleColor, &notVisibleColor, &knockedColor, &defaultEspColor})
                    color->w = 1.0f;

                colorStates = {
                    {"ENEMY", enemyVisibleColor}, {"BOT", botVisibleColor}, {"HIDDEN", notVisibleColor},
                    {"KNOCKED", knockedColor}, {"DEFAULT", defaultEspColor}
                };
                ImGui::EndGroupPanel();

                ImGui::Spacing();
                ImGui::BeginGroupPanel("External overlay", ImVec2(-1, 0));
                jnmaini_draw_external_panel();
                ImGui::EndGroupPanel();
            }
            ImGui::EndChild();
        }
        if (tab == 6)
        {
            ImGui::Text(ICON_FA_CLIPBOARD_LIST "  Log / hooks.h");
            ImGui::Separator();
            const int bypassN = codm_anogs_last_bypass_count();
            if (codm_anogs_hooks_patched())
                ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.35f, 1.f),
                                   ICON_FA_CHECK_CIRCLE " anogs hooks.h BYPASS OK (%d/4)", bypassN);
            else if (bypassN > 0)
                ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f),
                                   "anogs hooks.h partial %d/4 — tap Re-patch", bypassN);
            else
                ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f),
                                   "anogs hooks.h not applied — start ESP or Re-patch");
            if (ImGui::Button("Re-patch anogs (hooks.h)")) codm_auto_patch_anogs();
            ImGui::Text("unity features on %d (menu toggles only)", codm_unity_hook_count());
            ImGui::SameLine();
            if (ImGui::Button("Copy logs all")) ui_log_copy_to_clipboard();
            ImGui::SameLine();
            if (ImGui::Button("Clear log")) ui_log_clear();
            ImGui::Spacing();
            ui_log_imgui_panel_ex(-1.f);
        }

        if (tab == 7)
        {
            ImGui::Spacing();
            WeaponCfg::DrawTab();
        }
        ImGui::EndChild();
        ImGui::Columns(1);
    }
    
    ImGui::EndChild();
}
if (jnmaini_title_font()) ImGui::PopFont();
ImGui::End();
}
