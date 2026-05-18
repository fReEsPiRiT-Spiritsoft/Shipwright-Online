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

    nlohmann::json payload;
    payload["type"] = ACTOR_KILLED;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actorKey"] = GetActorKey(actor, gPlayState->sceneNum);
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

    for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != nullptr) {
            Actor* next = actor->next; // cache next before potential Actor_Kill invalidates pointers
            if (GetActorKey(actor, sceneNum) == actorKey) {
                if (actor->colChkInfo.health > 0) {
                    // Force HP to 0 so the enemy's own update logic triggers the death sequence
                    // (plays death animation, drops items, sets clear flags, etc.)
                    actor->colChkInfo.health = 0;
                    Actor_Kill(actor);
                }
                return;
            }
            actor = next;
        }
    }
}
