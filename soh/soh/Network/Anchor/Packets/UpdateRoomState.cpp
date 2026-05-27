#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"

extern "C" {
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * UPDATE_ROOM_STATE
 */

nlohmann::json Anchor::PrepRoomState() {
    nlohmann::json payload;
    payload["ownerClientId"] = ownClientId;
    bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));

    if (isGlobalRoom) {
        // Global room uses hardcoded settings
        payload["pvpMode"] = 0;
        payload["showLocationsMode"] = 0;
        payload["teleportMode"] = 0;
        payload["syncItemsAndFlags"] = 0;
        payload["syncHPAndCounts"] = 0;
        payload["syncDayTime"] = 0;
        payload["syncEnemies"] = 0;
        payload["syncBGObjects"] = 0;
        payload["syncRadius"] = 0;
        payload["enemySyncTickRate"] = 2;
        payload["physicalItemExchange"] = 0;
        payload["syncCutscenes"] = 0;
        payload["syncMinigames"]     = 0;
        payload["syncEpona"]         = 0;
        payload["battleRoyaleMode"] = 0;
        payload["brFreshStart"]     = 0;
    } else {
        payload["pvpMode"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.PvpMode"), 1);
        payload["showLocationsMode"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.ShowLocationsMode"), 1);
        payload["teleportMode"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.TeleportMode"), 1);
        payload["syncItemsAndFlags"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncItemsAndFlags"), 1);
        payload["syncHPAndCounts"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncHPAndCounts"), 1);
        payload["syncDayTime"]        = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncDayTime"), 0);
        payload["syncEnemies"]           = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncEnemies"), 0);
        payload["syncBGObjects"]          = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncBGObjects"), 1);
        payload["syncRadius"]            = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncRadius"), 1500);
        payload["enemySyncTickRate"]     = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.EnemySyncTickRate"), 2);
        payload["physicalItemExchange"]  = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.PhysicalItemExchange"), 0);
        payload["syncCutscenes"]         = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncCutscenes"), 0);
        payload["syncMinigames"]         = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncMinigames"),     1);
        payload["syncEpona"]             = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncEpona"),         1);
        payload["battleRoyaleMode"]      = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.BattleRoyaleMode"),   0);
        payload["brFreshStart"]          = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.BrFreshStart"),       0);
    }

    return payload;
}

void Anchor::SendPacket_UpdateRoomState() {
    nlohmann::json payload;
    payload["type"] = UPDATE_ROOM_STATE;
    payload["state"] = PrepRoomState();

    Network::SendJsonToRemote(payload);
}

void Anchor::HandlePacket_UpdateRoomState(nlohmann::json payload) {
    if (!payload.contains("state")) {
        return;
    }

    roomState.ownerClientId = payload["state"]["ownerClientId"].get<uint32_t>();
    roomState.pvpMode = payload["state"]["pvpMode"].get<u8>();
    roomState.showLocationsMode = payload["state"]["showLocationsMode"].get<u8>();
    roomState.teleportMode = payload["state"]["teleportMode"].get<u8>();
    roomState.syncItemsAndFlags = payload["state"]["syncItemsAndFlags"].get<u8>();
    // optional fields for backwards compatibility
    roomState.syncHPAndCounts    = payload["state"].value("syncHPAndCounts",   (u8)1);
    roomState.syncDayTime        = payload["state"].value("syncDayTime",       (u8)0);
    roomState.syncEnemies           = payload["state"].value("syncEnemies",           (u8)0);
    // Default syncBGObjects inherits from syncEnemies for backwards compatibility with older clients.
    roomState.syncBGObjects          = payload["state"].value("syncBGObjects",          roomState.syncEnemies);
    roomState.syncRadius            = payload["state"].value("syncRadius",            (uint16_t)1500);
    roomState.enemySyncTickRate     = payload["state"].value("enemySyncTickRate",     (u8)2);
    roomState.physicalItemExchange  = payload["state"].value("physicalItemExchange",  (u8)0);
    roomState.syncCutscenes         = payload["state"].value("syncCutscenes",         (u8)0);
    roomState.syncMinigames         = payload["state"].value("syncMinigames",         (u8)1);
    roomState.syncEpona             = payload["state"].value("syncEpona",             (u8)1);
    roomState.battleRoyaleMode      = payload["state"].value("battleRoyaleMode",      (u8)0);
    roomState.brFreshStart          = payload["state"].value("brFreshStart",          (u8)0);
}
