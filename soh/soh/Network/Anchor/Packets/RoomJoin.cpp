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
    payload["clientId"] = ownClientId;  // needed for local handler

    // The Anchor server does not echo packets back to the sender, and non-hosts
    // skip ROOM_JOIN entirely (they check !IsHostAuthority()).  So when the host
    // itself joins a room, no one would ever process its ROOM_JOIN.  Handle it
    // locally so ROOM_MASTER_ASSIGN and the snapshot delivery flow are triggered.
    if (IsHostAuthority()) {
        HandlePacket_RoomJoin(payload);
    }

    SendJsonToRemote(payload);
}

// ─── Receive: ROOM_JOIN ───────────────────────────────────────────────────────

void Anchor::HandlePacket_RoomJoin(nlohmann::json payload) {
    // Only the room owner decides who becomes room master.
    if (!IsHostAuthority()) return;

    if (!payload.contains("roomKey") || !payload.contains("clientId")) return;

    const std::string roomKey      = payload["roomKey"].get<std::string>();
    const uint32_t    joiningClient = payload["clientId"].get<uint32_t>();

    const s16 joinSceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    const s8  joinRoomNum  = (s8)payload.value("roomNum", -1);

    // Check whether an active room master is currently present in this room.
    // "Online but in a different room" counts as absent — this ensures a
    // re-entering master triggers the snapshot flow rather than silently
    // keeping stale authority over a room they left.
    bool masterInRoom = false;
    auto it = roomAuthority.find(roomKey);
    if (it != roomAuthority.end() && it->second != 0) {
        const uint32_t masterId = it->second;
        if (masterId == ownClientId) {
            // Host is the master — check against the host's current room.
            masterInRoom = IsSaveLoaded() && gPlayState &&
                           gPlayState->sceneNum == joinSceneNum &&
                           (s8)gPlayState->roomCtx.curRoom.num == joinRoomNum;
        } else {
            masterInRoom = clients.contains(masterId) && clients[masterId].online &&
                           clients[masterId].sceneNum == joinSceneNum &&
                           clients[masterId].curRoomNum == joinRoomNum;
        }
    }

    if (!masterInRoom) {
        roomAuthority[roomKey] = joiningClient;
        SPDLOG_INFO("[Anchor] Room master assigned: room={} master={}", roomKey, joiningClient);
    }

    // If the joining client IS the current master (they left and re-entered),
    // transfer mastership to a staying client so the re-entrant receives a
    // fresh snapshot with the actual room state (killed enemies, BG positions).
    if (joiningClient == roomAuthority[roomKey]) {
        for (const auto& [cid, client] : clients) {
            if (cid == joiningClient) continue;  // skip the re-entering client
            if (!client.online || !client.isSaveLoaded) continue;
            if (client.sceneNum == joinSceneNum && client.curRoomNum == joinRoomNum) {
                roomAuthority[roomKey] = cid;
                SPDLOG_INFO("[Anchor] Re-entering master: transferred to staying client: room={} newMaster={}", roomKey, cid);
                break;
            }
        }
    }

    nlohmann::json assign;
    assign["type"]            = ROOM_MASTER_ASSIGN;
    assign["roomKey"]         = roomKey;
    assign["masterClientId"]  = roomAuthority[roomKey];
    assign["joiningClientId"] = joiningClient;
    // The server does not echo ROOM_MASTER_ASSIGN back to the sender.
    // Handle it locally so the host's roomAuthority and snapshot trigger work.
    HandlePacket_RoomMasterAssign(assign);
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
