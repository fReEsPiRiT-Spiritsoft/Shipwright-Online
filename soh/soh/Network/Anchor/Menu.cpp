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
    if (UIWidgets::CVarCheckbox("Sync Day/Night Cycle", CVAR_REMOTE_ANCHOR("RoomSettings.SyncDayTime"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(false)
                                    .Color(THEME_COLOR)
                                    .Tooltip("When enabled, the host broadcasts the current time of day to all "
                                             "clients every 3 seconds.\n\n"
                                             "If either player is in an indoor scene or dungeon (where time "
                                             "normally stands still), time is frozen globally for both players."))) {
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
    SohGui::mSohMenu->AddWidget(path, "AnchorInstructionsMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorInstructionsMenu)
        .HideInSearch(true);
}

static RegisterMenuInitFunc menuInitFunc(RegisterAnchorMenu);
#endif
