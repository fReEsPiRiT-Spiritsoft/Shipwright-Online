#include "Anchor.h"
#include <libultraship/libultraship.h>
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/util.h"

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
extern std::shared_ptr<AnchorRoomWindow> mAnchorRoomWindow;
} // namespace SohGui

static const char* pvpModes[3] = { "Off", "On", "On + Friendly Fire" };
static std::vector<const char*> teleportModes = { "None", "Team Only", "All" };
static std::vector<const char*> showLocationsModes = { "None", "Team Only", "All" };

void AnchorMainMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    std::string host = CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
    uint16_t port = CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
    std::string anchorTeamId = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    std::string anchorRoomId = CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
    std::string anchorName = CVarGetString(CVAR_REMOTE_ANCHOR("Name"), "");
    bool isFormValid = !SohUtils::IsStringEmpty(host) && port > 1024 && port < 65535 &&
                       !SohUtils::IsStringEmpty(anchorRoomId) && !SohUtils::IsStringEmpty(anchorName);

    ImGui::SeparatorText("Connection Settings");

    ImGui::BeginDisabled(anchor->isEnabled);
    ImGui::Text("Host & Port");
    if (UIWidgets::InputString("##Host", &host,
                               UIWidgets::InputOptions()
                                   .Size(ImGui::GetContentRegionAvail() -
                                         ImVec2((ImGui::GetFontSize() * 5 + ImGui::GetStyle().ItemSpacing.x), 0))
                                   .Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), host.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::SameLine();
    UIWidgets::PushStyleInput(THEME_COLOR);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    if (ImGui::InputScalar("##Port", ImGuiDataType_U16, &port)) {
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), port);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    UIWidgets::PopStyleInput();

    ImGui::Text("Name & Color");
    static Color_RGBA8 defaultColor = { 100, 255, 100, 255 };
    UIWidgets::CVarColorPicker("##Color", CVAR_REMOTE_ANCHOR("Color"), defaultColor);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##Name", &anchorName, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Name"), anchorName.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Text("Room ID");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##RoomId", &anchorRoomId,
                               UIWidgets::InputOptions().IsSecret(anchor->isEnabled).Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), anchorRoomId.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Text("Team ID (Items & Flags Shared)");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##TeamId", &anchorTeamId, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), anchorTeamId.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Spacing();

    if (UIWidgets::Button("Restore Defaults", UIWidgets::ButtonOptions()
                                                  .Size(ImVec2(ImGui::GetContentRegionAvail().x / 2, 0))
                                                  .Color(UIWidgets::Colors::Red))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
        CVarSetString(CVAR_REMOTE_ANCHOR("Name"), "");
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::SameLine();

    if (UIWidgets::Button("Global Room", UIWidgets::ButtonOptions()
                                             .Color(UIWidgets::Colors::Blue)
                                             .Tooltip("Always-online public room so you don't have to experience "
                                                      "Hyrule alone. PVP and syncing are disabled."))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), "soh-global");
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::EndDisabled();

    ImGui::Spacing();

    ImGui::BeginDisabled(!isFormValid);
    const char* buttonLabel = anchor->isEnabled ? "Disable" : "Enable";
    UIWidgets::PushStyleButton(anchor->isEnabled ? UIWidgets::ColorValues.at(UIWidgets::Colors::Red)
                                                 : UIWidgets::ColorValues.at(UIWidgets::Colors::Green));
    if (ImGui::Button(buttonLabel, ImVec2(-1.0f, 0.0f))) {
        if (anchor->isEnabled) {
            CVarClear(CVAR_REMOTE_ANCHOR("Enabled"));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->Disable();
        } else {
            CVarSetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 1);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->Enable();
        }
    }
    UIWidgets::PopStyleButton();
    ImGui::EndDisabled();
    ImGui::Spacing();

    if (!anchor->isEnabled) {
        return;
    }

    if (!anchor->isConnected) {
        ImGui::Text("Connecting...");
        return;
    }

    ImGui::SeparatorText("Current Room");
    ImGui::Text("%s Connected", ICON_FA_CHECK);

    UIWidgets::PushStyleButton(THEME_COLOR);
    if (ImGui::Button("Request Team State")) {
        anchor->SendPacket_RequestTeamState();
    }
    UIWidgets::Tooltip("Try this if you are missing items or flags that your team members have collected");
    UIWidgets::PopStyleButton();

    ImGui::SameLine();

    UIWidgets::WindowButton("Toggle Anchor Room Window", CVAR_WINDOW("AnchorRoom"), SohGui::mAnchorRoomWindow);

    ImGui::Spacing();

    bool hideLocations = Anchor::Instance->roomState.showLocationsMode == 0;
    ImGui::BeginDisabled(hideLocations);
    UIWidgets::CVarCheckbox(
        "Show Other Players on Minimap", CVAR_REMOTE_ANCHOR("ShowOtherPlayersOnMinimap"),
        UIWidgets::CheckboxOptions()
            .Color(THEME_COLOR)
            .DefaultValue(true)
            .Tooltip(!hideLocations
                         ? "Other players will appear on the minimap in areas where you have the compass. "
                           "Visibility is restricted according to the Show Locations mode for the room."
                         : "Cannot show other players because the room's Show Locations mode is set to None."));
    ImGui::EndDisabled();

    ImGui::Spacing();

    if (!SohGui::mAnchorRoomWindow->IsVisible()) {
        SohGui::mAnchorRoomWindow->DrawElement();
    }
}

void AnchorAdminMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;
    bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));

    if (!anchor->isEnabled || !anchor->isConnected || anchor->roomState.ownerClientId != anchor->ownClientId ||
        isGlobalRoom) {
        return;
    }

    ImGui::SeparatorText("Room Settings (Admin Only)");

    UIWidgets::PushStyleButton(THEME_COLOR);
    if (ImGui::Button("Clear All Team State")) {
        std::set<std::string> teams;
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            teams.insert(client.teamId);
        }
        for (auto& team : teams) {
            anchor->SendPacket_ClearTeamState(team);
        }
    }
    UIWidgets::PopStyleButton();

    if (UIWidgets::CVarCombobox("PvP Mode:", CVAR_REMOTE_ANCHOR("RoomSettings.PvpMode"), pvpModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCombobox("Show Locations For:", CVAR_REMOTE_ANCHOR("RoomSettings.ShowLocationsMode"),
                                showLocationsModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCombobox("Allow Teleporting To:", CVAR_REMOTE_ANCHOR("RoomSettings.TeleportMode"), teleportModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Sync Items & Flags", CVAR_REMOTE_ANCHOR("RoomSettings.SyncItemsAndFlags"),
                                UIWidgets::CheckboxOptions().DefaultValue(true).Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Sync HP & Item Counts", CVAR_REMOTE_ANCHOR("RoomSettings.SyncHPAndCounts"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(true)
                                    .Color(THEME_COLOR)
                                    .Tooltip("When disabled, each player has their own HP and consumable item "
                                             "counts (arrows, bombs, nuts, etc.).\n\n"
                                             "Item unlocks (Hookshot, Bow, Hammer, ...) and story items "
                                             "(medallions, gauntlets, tunics, ...) are always shared regardless "
                                             "of this setting."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Enemy Sync");

    if (UIWidgets::CVarCheckbox("Enable Enemy Sync", CVAR_REMOTE_ANCHOR("RoomSettings.SyncEnemies"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(false)
                                    .Color(THEME_COLOR)
                                    .Tooltip("Master switch for custom enemy synchronization.\n\n"
                                             "OFF: Enemies run in vanilla single-player mode for each client.\n"
                                             "ON:  HP and kills are always synced globally in the same room.\n"
                                             "     Position sync is further controlled by Sync Radius and Tick Rate."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    bool enemySyncOn = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncEnemies"), 0) != 0;
    ImGui::BeginDisabled(!enemySyncOn);

    {
        int radius = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncRadius"), 1500);
        UIWidgets::PushStyleSlider(THEME_COLOR);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderInt("##SyncRadius", &radius, 100, 5000, "Sync Radius: %d units")) {
            CVarSetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncRadius"), radius);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->SendPacket_UpdateRoomState();
        }
        UIWidgets::PopStyleSlider();
        UIWidgets::Tooltip("Enemies within this distance of a client's Link receive full position\n"
                           "and animation sync from the host.\n\n"
                           "Enemies outside this radius run their own local AI on the client;\n"
                           "HP changes and kills are still synced globally regardless of radius.");
    }

    static const char* tickRates[] = { "5 Hz  (every 4th frame)", "10 Hz (every 2nd frame)", "20 Hz (every frame)" };
    if (UIWidgets::CVarCombobox("Tick Rate:", CVAR_REMOTE_ANCHOR("RoomSettings.EnemySyncTickRate"), tickRates,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(2)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR)
                                    .Tooltip("How often position and animation data is broadcast for enemies\n"
                                             "inside the Sync Radius.\n\n"
                                             "20 Hz = every frame (smoothest, most bandwidth)\n"
                                             "10 Hz = every 2nd frame\n"
                                             " 5 Hz = every 4th frame (lowest bandwidth)"))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::SeparatorText("BG Object Sync");

    if (UIWidgets::CVarCheckbox("Enable BG Object Sync", CVAR_REMOTE_ANCHOR("RoomSettings.SyncBGObjects"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(true)
                                    .Color(THEME_COLOR)
                                    .Tooltip("Synchronizes background / environment objects between clients.\n\n"
                                             "Covers:\n"
                                             "  - Moving platforms and elevators (smooth keyframe interpolation)\n"
                                             "  - Rotating objects (Dodongo pillars, water temple gears)\n"
                                             "  - Destroyable objects: bombable walls, heavy blocks, etc.\n\n"
                                             "OFF: Each client simulates BG objects independently (vanilla).\n"
                                             "ON:  Room Master drives all BG objects; clients interpolate."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Physical Item Exchange");

    if (UIWidgets::CVarCheckbox("Physical Item Exchange", CVAR_REMOTE_ANCHOR("RoomSettings.PhysicalItemExchange"),
                                UIWidgets::CheckboxOptions()
                                    .Color(THEME_COLOR)
                                    .Tooltip("When ON: received items and flags are held in a buffer until\n"
                                             "both players are within ~1 metre of each other.\n"
                                             "The receiving player then gets a proper item animation\n"
                                             "and an in-game textbox: 'Du hast von [Name] [Item] erhalten!'\n\n"
                                             "When OFF: items and flags are applied instantly (default)."))) {
        anchor->SendPacket_UpdateRoomState();
    }
}

void AnchorGameModesMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    // ── 1. Guards ────────────────────────────────────────────────────────────
    if (!anchor->isEnabled || !anchor->isConnected) {
        ImGui::TextDisabled("Not connected to an Anchor room.");
        return;
    }

    const bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));
    if (isGlobalRoom) {
        ImGui::TextDisabled("Game Modes are disabled in the Global Room.");
        return;
    }

    const bool isAdmin = (anchor->roomState.ownerClientId == anchor->ownClientId);

    // ── 2. Non-admin: read-only overview ─────────────────────────────────────
    if (!isAdmin) {
        ImGui::SeparatorText("Game Mode Settings (Read-Only)");
        ImGui::TextDisabled("Only the room admin can change these settings.");
        ImGui::Spacing();

        auto& rs = anchor->roomState;
        auto yesno = [](u8 v) -> const char* { return v ? "ON" : "OFF"; };

        ImGui::Text("Day / Night Cycle Sync:  %s", yesno(rs.syncDayTime));
        ImGui::Text("Cutscene Sync:           %s", yesno(rs.syncCutscenes));
        ImGui::Text("Minigame Sync:           %s", yesno(rs.syncMinigames));
        ImGui::Text("Epona / Horse Sync:      %s", yesno(rs.syncEpona));
        ImGui::Text("Battle Royale Mode:      %s", yesno(rs.battleRoyaleMode));
        ImGui::Text("BR Fresh Start:          %s", yesno(rs.brFreshStart));
        return;
    }

    // ── 3. Admin UI ───────────────────────────────────────────────────────────
    ImGui::SeparatorText("World Sync (Admin Only)");

    if (UIWidgets::CVarCheckbox(
            "Sync Day/Night Cycle", CVAR_REMOTE_ANCHOR("RoomSettings.SyncDayTime"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(false)
                .Color(THEME_COLOR)
                .Tooltip("ON:  The room master broadcasts the time-of-day every 3 seconds.\n"
                         "     All clients follow the same sun/moon position.\n\n"
                         "OFF: Each player has their own independent time of day (default).\n\n"
                         "Note: If any player is indoors or in a dungeon, time is frozen\n"
                         "      globally so the transition does not look jarring."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Cutscene Sync (Admin Only)");

    // Cutscene sync borrows the enemy-sync room-authority infrastructure.
    const bool enemySyncOn = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncEnemies"), 0) != 0;
    ImGui::BeginDisabled(!enemySyncOn);
    if (!enemySyncOn) {
        UIWidgets::Tooltip("Requires Enemy Sync to be enabled (in the Anchor Admin tab).");
    }

    if (UIWidgets::CVarCheckbox(
            "Cutscene Sync (Nearest)", CVAR_REMOTE_ANCHOR("RoomSettings.SyncCutscenes"),
            UIWidgets::CheckboxOptions()
                .Color(THEME_COLOR)
                .Tooltip("ON:  In-scene cutscenes are broadcast to players who are in the\n"
                         "     same room and within the Enemy Sync Radius.\n"
                         "     Both players will see the same cutscene play in lockstep.\n\n"
                         "OFF: Each player watches cutscenes independently (default).\n\n"
                         "Requires Enemy Sync to be enabled first."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::SeparatorText("Minigame Sync (Admin Only)");

    if (UIWidgets::CVarCheckbox(
            "Sync Minigame State & Score", CVAR_REMOTE_ANCHOR("RoomSettings.SyncMinigames"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(true)
                .Color(THEME_COLOR)
                .Tooltip("ON:  The room master broadcasts minigame start/end events and\n"
                         "     the running score to all clients in the room.\n"
                         "     Every player's HUD counter and end-of-round NPC dialog\n"
                         "     will show the same consistent value.\n\n"
                         "Covers: Gerudo Horseback Archery (Yabusame), Shooting Gallery,\n"
                         "        Bombchu Bowling, and similar score-based minigames.\n\n"
                         "OFF: Each player accumulates their own independent score (default)."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Epona / Horse Sync (Admin Only)");

    if (UIWidgets::CVarCheckbox(
            "Sync Phantom Horses", CVAR_REMOTE_ANCHOR("RoomSettings.SyncEpona"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(true)
                .Color(THEME_COLOR)
                .Tooltip("ON:  When a remote player is riding a horse, a silent En_Horse_Normal\n"
                         "     actor is spawned below their phantom Link and driven by the\n"
                         "     network position every frame — giving them a visible mount.\n\n"
                         "     The phantom horse has AI disabled (update=nullptr) and lives\n"
                         "     across room boundaries (room=-1), matching the rider's lifetime.\n\n"
                         "OFF: Remote players on horseback appear to float (no horse visible)."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Battle Royale Mode (Admin Only)");

    if (UIWidgets::CVarCheckbox(
            "Battle Royale Mode", CVAR_REMOTE_ANCHOR("RoomSettings.BattleRoyaleMode"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(false)
                .Color(THEME_COLOR)
                .Tooltip(
                    "ON:  Aktiviert den Battle-Royale-Regelsatz fuer diesen Raum.\n\n"
                    "  - PvP-Schaden ist vorausgesetzt (PvP-Mode muss aktiv sein).\n"
                    "  - Jeder Kill erhoet die Kill-Streak des Angreifers.\n"
                    "  - Ab 5 Kills: 'WANTED'-Status — nahe NPCs werden aggressiv.\n"
                    "  - Eliminierte Spieler werden allen anderen gemeldet.\n"
                    "  - Der letzte Ueberlebende gewinnt das Match.\n\n"
                    "OFF: Kooperativer Standardmodus (Default).\n\n"
                    "Empfohlene Einstellungen fuer BR:\n"
                    "  PvP-Mode: An  (sonst kein Schaden)\n"
                    "  Sync Items & Flags: Aus  (Progress isolieren)"))) {
        anchor->SendPacket_UpdateRoomState();
    }

    // BR-Laufzeitanzeige: Match-Status, Wanted-Spieler und Kill-Streaks
    const bool brOn = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.BattleRoyaleMode"), 0) != 0;
    if (brOn) {
        ImGui::Spacing();

        // Match-Status-Indikator
        if (anchor->brMatchActive) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "● Match laeuft");
        } else {
            ImGui::TextDisabled("● Kein aktives Match");
        }
        ImGui::Spacing();

        // Gleichstart-Option
        if (UIWidgets::CVarCheckbox(
                "Gleichstart (Fresh Start)", CVAR_REMOTE_ANCHOR("RoomSettings.BrFreshStart"),
                UIWidgets::CheckboxOptions()
                    .DefaultValue(false)
                    .Color(THEME_COLOR)
                    .Tooltip(
                        "ON:  Beim Match-Start wird das Inventar aller Spieler geleert,\n"
                        "     Herzen auf 3 Container und Rupien auf 0 gesetzt.\n"
                        "     Kisten-Flags werden zurückgesetzt – Items koennen neu geholt werden.\n\n"
                        "OFF: Jeder Spieler startet mit seinem aktuellen Fortschritt (Default)."))) {
            anchor->SendPacket_UpdateRoomState();
        }
        ImGui::Spacing();

        // Admin-Buttons: Match starten / beenden
        if (!anchor->brMatchActive) {
            if (ImGui::Button("Match starten")) {
                // Jedem Client (inkl. Host) wird ein einzigartiger Spawn-Index
                // in Hyrule Field zugewiesen.  8 Spawn-Punkte zyklisch vergeben.
                nlohmann::json spawnAssignments;
                int spawnIdx = 0;
                spawnAssignments[std::to_string(anchor->ownClientId)] = spawnIdx++;
                for (auto& [cid, _] : anchor->clients) {
                    spawnAssignments[std::to_string(cid)] = spawnIdx++ % 8;
                }
                anchor->SendPacket_BattleRoyaleEvent("MATCH_START", {
                    { "startProtectionSecs", 10 },
                    { "spawnAssignments",    spawnAssignments }
                });
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Teleportiert alle Spieler zu verteilten Spawnpunkten\nin Hyrule Field und startet das Match.");
            }
        } else {
            if (ImGui::Button("Match beenden (Abbruch)")) {
                anchor->SendPacket_BattleRoyaleEvent("MATCH_END", { { "winnerClientId", 0u } });
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Beendet das laufende Match ohne Sieger (winnerClientId=0).");
            }
        }
        ImGui::Spacing();
        if (anchor->wantedClients.empty()) {
            ImGui::TextDisabled("Keine Spieler wanted.");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "WANTED:");
            for (uint32_t wantedId : anchor->wantedClients) {
                const std::string wantedName = anchor->clients.count(wantedId)
                                                   ? anchor->clients[wantedId].name
                                                   : std::to_string(wantedId);
                ImGui::Text("  - %s", wantedName.c_str());
            }
        }
        ImGui::Spacing();
        bool anyStreak = false;
        for (auto& [cid, kills] : anchor->brKillStreak) {
            if (kills > 0 && kills != 0xFF) {
                if (!anyStreak) {
                    ImGui::Text("Kill-Streaks:");
                    anyStreak = true;
                }
                const std::string name =
                    anchor->clients.count(cid) ? anchor->clients[cid].name : std::to_string(cid);
                ImGui::Text("  %s: %d", name.c_str(), (int)kills);
            }
        }
        if (!anyStreak) {
            ImGui::TextDisabled("Noch keine Kills in diesem Match.");
        }
    }
}

void AnchorInstructionsMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    ImGui::SeparatorText("Usage Instructions");

    ImGui::TextWrapped("1. All players involved should start at the file select screen");

    ImGui::TextWrapped("2. Come up with a unique Room ID (this is basically your password) and enter it, along with "
                       "your desired player name and team ID and click Enable");

    ImGui::TextWrapped("3. The host should configure the randomizer settings and generate a seed, then share the newly "
                       "generated JSON spoiler file with other players.");

    ImGui::TextWrapped("4. All players should load the same JSON spoiler file (drag it into SoH window), make sure "
                       "seed icons match, then create a new file.");

    ImGui::TextWrapped("5. All players should now load into their game. IMPORTANT! If using an existing save/seed "
                       "ensure the player with the most progress loads the file first.");

    ImGui::TextWrapped("6. After everyone has loaded in, verify on the network tab that it doesn't warn about anyone "
                       "being on a wrong version or seed.");

    ImGui::Spacing();

    ImGui::TextWrapped(
        "Note: Team ID is used to group players together in the same team, sharing items and flags. Make sure all "
        "players who want to share progress use the same Team ID. All players with the same Team ID should be using "
        "the same randomizer seed, while players on different teams can use different seeds.");
}

#ifdef ENABLE_REMOTE_CONTROL
void RegisterAnchorMenu() {
    WidgetPath path = { "Network", "Anchor", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddWidget(path, "AnchorMainMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorMainMenu)
        .HideInSearch(true);
    path.column = SECTION_COLUMN_2;
    SohGui::mSohMenu->AddWidget(path, "AnchorAdminMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorAdminMenu)
        .HideInSearch(true);
    WidgetPath gameModesPath = { "Network", "Game Modes", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddWidget(gameModesPath, "AnchorGameModesMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorGameModesMenu)
        .HideInSearch(true);
    SohGui::mSohMenu->AddWidget(path, "AnchorInstructionsMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorInstructionsMenu)
        .HideInSearch(true);
}

static RegisterMenuInitFunc menuInitFunc(RegisterAnchorMenu);
#endif
