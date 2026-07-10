#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * ACTOR_KILLED
 *
 * Sent by the enemy authority (room owner) when an enemy or boss actor is killed.
 *
 * Forces all non-authority clients to kill the matching local actor, ensuring that
 * enemy deaths are consistent across all clients.
 *
 * This is the authoritative kill signal: if the owner killed it, it's dead everywhere.
 * This also covers the case where a non-owner player attacks an enemy — if the owner
 * independently kills that enemy (or the kill propagates through ACTOR_STATE_UPDATE
 * reducing HP to 0 and triggering the enemy's own death logic), all clients will
 * receive this packet and kill their local copy.
 */

void Anchor::SendPacket_ActorKilled(const Actor* actor) {
    if (!IsSaveLoaded()) return;

    std::string actorKey = GetActorKey(actor, gPlayState->sceneNum);
    SPDLOG_INFO("[Anchor:EnemySync] HOST: ACTOR_KILLED send | actorKey={} | scene=0x{:02x}", actorKey, gPlayState->sceneNum);

    nlohmann::json payload;
    payload["type"] = ACTOR_KILLED;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actorKey"] = actorKey;
    payload["actorId"] = actor->id;
    payload["actorCategory"] = actor->category;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_ActorKilled(nlohmann::json payload) {
    if (!IsSaveLoaded() || IsEnemyAuthority()) return;
    if (!payload.contains("actorKey")) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    std::string actorKey = payload["actorKey"].get<std::string>();
    int receivedCategory = payload.value("actorCategory", (int)ACTORCAT_ENEMY);
    SPDLOG_INFO("[Anchor:ActorSync] CLIENT: ACTOR_KILLED recv | actorKey={} | category={} | scene=0x{:02x}",
                actorKey, receivedCategory, sceneNum);

    // Confirmation path for ROOM_KILL_SYNC retries:
    // when authority confirms a kill, drop this key from all pending room sets.
    for (auto it = pendingRoomKills.begin(); it != pendingRoomKills.end();) {
        it->second.erase(actorKey);
        if (it->second.empty()) {
            it = pendingRoomKills.erase(it);
        } else {
            ++it;
        }
    }

    // Determine which actor category lists to scan.
    // Phase 5: extend to BG/PROP in addition to the original ENEMY/BOSS scope.
    bool isBgKill     = (receivedCategory == ACTORCAT_BG || receivedCategory == ACTORCAT_PROP);
    bool isEnemyKill  = (receivedCategory == ACTORCAT_ENEMY || receivedCategory == ACTORCAT_BOSS);

    // Scan the matching category first; fall back to all four if category is unknown.
    std::vector<int> catsToScan;
    if (isEnemyKill)     catsToScan = { ACTORCAT_ENEMY, ACTORCAT_BOSS };
    else if (isBgKill)   catsToScan = { ACTORCAT_BG,    ACTORCAT_PROP };
    else                 catsToScan = { ACTORCAT_ENEMY, ACTORCAT_BOSS, ACTORCAT_BG, ACTORCAT_PROP };

    for (int cat : catsToScan) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != nullptr) {
            Actor* next = actor->next; // cache before potential Actor_Kill invalidates pointers
            if (GetActorKey(actor, sceneNum) == actorKey) {
                if (isEnemyKill) {
                    // Always zero health before kill so the actor leaves the correct
                    // drop state, even if EnemyPositionUpdate already set it to 0.
                    actor->colChkInfo.health = 0;
                }
                Actor_Kill(actor);

                // For boss actors: the game-native death sequence (which runs on the
                // master) normally calls Flags_SetTempClear / Flags_SetClear for the
                // room so that DoorWarp1_AwaitClearFlag (the blue exit portal) can
                // advance.  On non-master clients Actor_Kill is called externally via
                // this packet, so the boss never runs its own clear-flag code.
                // Set both flags here to ensure the blue warp appears for everyone.
                if (isEnemyKill && actor->category == ACTORCAT_BOSS && gPlayState) {
                    const s32 bossRoom = (actor->room >= 0) ? actor->room
                                                            : (s32)gPlayState->roomCtx.curRoom.num;
                    Flags_SetTempClear(gPlayState, bossRoom);
                    Flags_SetClear(gPlayState, bossRoom);
                }
                // Clean up client-side BG tracking entries for dead actors.
                if (isBgKill) {
                    bgActorKeyframeTarget.erase(actorKey);
                }
                SPDLOG_INFO("[Anchor:ActorSync] CLIENT: ACTOR_KILLED applied | actorKey={} | category={}", actorKey, cat);
                return;
            }
            actor = next;
        }
    }

    SPDLOG_WARN("[Anchor:ActorSync] CLIENT: ACTOR_KILLED actor not found | actorKey={} | category={} | scene=0x{:02x}",
                actorKey, receivedCategory, sceneNum);
}
