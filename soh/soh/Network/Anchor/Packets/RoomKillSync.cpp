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

    pendingRoomKills.erase(it); // consumed — don't re-send on next frame
    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_RoomKillSync(nlohmann::json payload) {
    if (!IsSaveLoaded() || !gPlayState) return;
    if (!payload.contains("kills") || !payload.contains("sceneNum")) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    for (const auto& actorKeyJson : payload["kills"]) {
        std::string actorKey = actorKeyJson.get<std::string>();

        for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
            Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                Actor* next = actor->next; // cache before potential invalidation
                if (actor->colChkInfo.health > 0 &&
                    GetActorKey(actor, sceneNum) == actorKey)
                {
                    actor->colChkInfo.health = 0;
                    Actor_Kill(actor);
                    break; // move on to next actorKey in the list
                }
                actor = next;
            }
        }
    }
}
