#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * ROOM_KILL_SYNC
 *
 * Bidirectional packet sent by whichever side had been killing enemies alone
 * in a room the moment the other player enters that same room.
 *
 * Problem it solves:
 *   Because enemies respawn on every room entry, a player that cleared a room
 *   solo will have dead enemies while the arriving player sees them alive.
 *   This packet bridges that gap by immediately killing the relevant actors on
 *   the receiver side right after the shared room is established.
 *
 * Flow (client clears room before host arrives):
 *   1. Client kills enemy while host is elsewhere → actorKey recorded in
 *      pendingRoomKills["sceneNum_roomNum"].
 *   2. OnGameFrameUpdate detects IsOwnerInSameRoom() transition false → true.
 *   3. Client sends ROOM_KILL_SYNC with the pending kill list.
 *   4. Host finds matching living actors and calls Actor_Kill() on each.
 *   5. Host's OnActorKill → SendPacket_ActorKilled propagates back to client
 *      (no-op because the actor is already dead there).
 *
 * Flow (host clears room before client arrives):
 *   Symmetric: authority records kills in pendingRoomKills, detects
 *   IsAnyClientInSameRoom() transition false → true, sends to client.
 */

void Anchor::SendPacket_RoomKillSync() {
    if (!IsSaveLoaded() || !gPlayState) return;

    std::string roomKey = std::to_string(gPlayState->sceneNum) + "_" +
                          std::to_string((s8)gPlayState->roomCtx.curRoom.num);

    auto it = pendingRoomKills.find(roomKey);
    if (it == pendingRoomKills.end() || it->second.empty()) return;

    nlohmann::json payload;
    payload["type"]     = ROOM_KILL_SYNC;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"]  = (s8)gPlayState->roomCtx.curRoom.num;
    payload["kills"]    = nlohmann::json::array();

    for (const auto& key : it->second) {
        payload["kills"].push_back(key);
    }

    SPDLOG_INFO("[Anchor:EnemySync] {}: ROOM_KILL_SYNC send | scene=0x{:02x} room={} kills={}",
                IsEnemyAuthority() ? "HOST" : "CLIENT", gPlayState->sceneNum,
                (s8)gPlayState->roomCtx.curRoom.num, it->second.size());

    // Keep client-side pending entries for reliability retries until authority
    // confirms via ACTOR_KILLED. Host keeps previous behavior.
    if (IsEnemyAuthority()) {
        pendingRoomKills.erase(it);
    }

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_RoomKillSync(nlohmann::json payload) {
    if (!IsSaveLoaded() || !gPlayState) return;
    if (!payload.contains("kills") || !payload.contains("sceneNum")) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    s8 roomNum = payload.value("roomNum", (s8)-1);
    size_t receivedKills = payload["kills"].size();
    size_t appliedKills = 0;

    SPDLOG_INFO("[Anchor:EnemySync] {}: ROOM_KILL_SYNC recv | scene=0x{:02x} room={} kills={}",
                IsEnemyAuthority() ? "HOST" : "CLIENT", sceneNum, roomNum, receivedKills);

    for (const auto& actorKeyJson : payload["kills"]) {
        std::string actorKey = actorKeyJson.get<std::string>();
        bool found = false;

        for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
            Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                Actor* next = actor->next; // cache before potential invalidation
                if (GetActorKey(actor, sceneNum) == actorKey)
                {
                    // Kill regardless of current health. Some enemies can sit at
                    // health==0 for a while before calling Actor_Kill themselves.
                    // ROOM_KILL_SYNC is an authoritative "remove now" signal.
                    actor->colChkInfo.health = 0;
                    Actor_Kill(actor);
                    found = true;
                    appliedKills++;
                    break; // move on to next actorKey in the list
                }
                actor = next;
            }

            if (found) {
                break;
            }
        }

        if (!found) {
            SPDLOG_WARN("[Anchor:EnemySync] {}: ROOM_KILL_SYNC actor not found/already dead | actorKey={} | scene=0x{:02x} room={}",
                        IsEnemyAuthority() ? "HOST" : "CLIENT", actorKey, sceneNum, roomNum);
        }
    }

    SPDLOG_INFO("[Anchor:EnemySync] {}: ROOM_KILL_SYNC applied | scene=0x{:02x} room={} applied={}/{}",
                IsEnemyAuthority() ? "HOST" : "CLIENT", sceneNum, roomNum, appliedKills, receivedKills);
}
