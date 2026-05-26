#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"

extern "C" {
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * ROOM_JOIN / ROOM_MASTER_ASSIGN
 *
 * These two packets implement the per-room gameplay-authority handshake.
 *
 * Flow:
 *   1. Any client entering a new scene/room sends ROOM_JOIN to all peers.
 *   2. The room owner (IsHostAuthority()) is the sole actor that processes
 *      ROOM_JOIN.  It checks whether a live room master exists for that room.
 *        - If none / master went offline  → assigns the joining client.
 *        - If owner themselves join        → owner always takes master.
 *        - If an active master exists      → confirms the existing master.
 *   3. The owner then broadcasts ROOM_MASTER_ASSIGN to all clients.
 *   4. All clients (including the owner) update their roomAuthority map.
 *
 * Backwards compatibility:
 *   Old clients neither send ROOM_JOIN nor register a handler for
 *   ROOM_MASTER_ASSIGN, so they silently ignore both packets.
 *   IsRoomMaster() falls back to IsHostAuthority() when roomAuthority holds
 *   no assignment for the current room, preserving old behaviour.
 */

// ─── Send ─────────────────────────────────────────────────────────────────────

void Anchor::SendPacket_RoomJoin() {
    if (!IsSaveLoaded() || !isConnected) return;

    nlohmann::json payload;
    payload["type"]     = ROOM_JOIN;
    payload["roomKey"]  = GetCurrentRoomKey();
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"]  = (int)gPlayState->roomCtx.curRoom.num;

    SendJsonToRemote(payload);
}

// ─── Receive: ROOM_JOIN ───────────────────────────────────────────────────────

void Anchor::HandlePacket_RoomJoin(nlohmann::json payload) {
    // Only the room owner decides who becomes room master.
    if (!IsHostAuthority()) return;

    if (!payload.contains("roomKey") || !payload.contains("clientId")) return;

    const std::string roomKey      = payload["roomKey"].get<std::string>();
    const uint32_t    joiningClient = payload["clientId"].get<uint32_t>();

    // Check whether an active room master already exists for this room.
    bool masterOnline = false;
    auto it = roomAuthority.find(roomKey);
    if (it != roomAuthority.end() && it->second != 0) {
        masterOnline = clients.contains(it->second) && clients[it->second].online;
    }

    // The owner always reclaims master when joining a room themselves so that
    // the admin is never locked out of their own authority.
    const bool ownerJoining = (joiningClient == ownClientId);

    if (ownerJoining || !masterOnline) {
        roomAuthority[roomKey] = joiningClient;
        if (ownerJoining) {
            SPDLOG_INFO("[Anchor] Room owner reclaimed master: room={}", roomKey);
        } else {
            SPDLOG_INFO("[Anchor] Room master assigned: room={} master={}", roomKey, joiningClient);
        }
    }
    // If an active master already exists (and it is not the owner joining),
    // we still broadcast a confirmation so late joiners learn who the master is.

    nlohmann::json assign;
    assign["type"]           = ROOM_MASTER_ASSIGN;
    assign["roomKey"]        = roomKey;
    assign["masterClientId"] = roomAuthority[roomKey];
    assign["joiningClientId"] = joiningClient;  // lets the master know who just arrived
    SendJsonToRemote(assign);
}

// ─── Receive: ROOM_MASTER_ASSIGN ─────────────────────────────────────────────

void Anchor::HandlePacket_RoomMasterAssign(nlohmann::json payload) {
    if (!payload.contains("roomKey") || !payload.contains("masterClientId")) return;

    const std::string roomKey        = payload["roomKey"].get<std::string>();
    const uint32_t    masterClientId = payload["masterClientId"].get<uint32_t>();
    const uint32_t    joiningClientId = payload.value("joiningClientId", 0u);

    roomAuthority[roomKey] = masterClientId;

    const bool isSelf = (masterClientId == ownClientId);
    SPDLOG_INFO("[Anchor] Room master updated: room={} master={} self={}",
                roomKey, masterClientId, isSelf);

    // If we ARE the room master and a different client just joined our room,
    // send them a state snapshot so they start with accurate HP/positions.
    if (isSelf && joiningClientId != 0 && joiningClientId != ownClientId) {
        // Only send a snapshot for the room we are currently in.
        if (roomKey == GetCurrentRoomKey()) {
            SPDLOG_INFO("[Anchor] RoomMasterAssign: I am master, triggering snapshot for client {}", joiningClientId);
            SendPacket_RoomSnapshot(joiningClientId);
        }
    }
}
