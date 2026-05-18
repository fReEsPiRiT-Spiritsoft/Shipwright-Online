#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * PLAYER_ATTACK_ACTOR
 *
 * Sent by a non-authority client when the local player damages an enemy.
 * The enemy authority receives this and applies the HP delta to its local copy
 * so that the authoritative health value stays consistent.
 *
 * Flow:
 *   1. Non-authority's OnActorUpdate detects enemy health decrease caused by local gameplay.
 *   2. Non-authority sends PLAYER_ATTACK_ACTOR with the HP delta to the authority (room owner).
 *   3. Authority applies the delta to its local actor.
 *   4. Authority's normal ActorStateUpdate / ActorKilled flow then syncs the result to everyone.
 *
 * The non-authority still sees the damage locally (visual feedback), but the authority's
 * state is the canonical one that all clients converge to.
 */

void Anchor::SendPacket_PlayerAttackActor(const Actor* actor, u8 damage) {
    if (!IsSaveLoaded()) return;

    nlohmann::json payload;
    payload["type"]       = PLAYER_ATTACK_ACTOR;
    payload["quiet"]      = true; // high-frequency, no debug spam
    payload["sceneNum"]   = gPlayState->sceneNum;
    payload["actorKey"]   = GetActorKey(actor, gPlayState->sceneNum);
    payload["damage"]     = damage;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_PlayerAttackActor(nlohmann::json payload) {
    // Only the enemy authority processes incoming attack packets.
    if (!IsSaveLoaded() || !IsEnemyAuthority()) return;
    if (!payload.contains("actorKey") || !payload.contains("damage")) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    std::string actorKey = payload["actorKey"].get<std::string>();
    u8 damage            = payload["damage"].get<u8>();

    // Locate the actor and apply the HP delta.
    // Clamping to 1 so we don't accidentally kill — Actor_Kill comes via the
    // actor's own death logic (which fires OnActorKill → SendPacket_ActorKilled).
    for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != nullptr) {
            if (actor->colChkInfo.health > 0 &&
                GetActorKey(actor, gPlayState->sceneNum) == actorKey)
            {
                u8 currentHp = actor->colChkInfo.health;
                // Apply damage — allow death (0) so the actor's action function can
                // detect health == 0 and trigger its own death sequence.
                actor->colChkInfo.health = (currentHp > damage) ? (currentHp - damage) : 0;
                return;
            }
            actor = actor->next;
        }
    }
}
