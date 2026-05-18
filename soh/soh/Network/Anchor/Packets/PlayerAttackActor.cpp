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
 *
 * Timing note (important for correctness):
 *   ProcessIncomingPacketQueue() is called from OnGameFrameUpdate in game.c,
 *   which fires AFTER Play_Update() (CollisionCheck + Actor_UpdateAll) for
 *   the current frame.  This means the handler runs AFTER the actor loop.
 *
 *   We therefore set actor->colChkInfo.damage and let the NEXT frame's
 *   Actor_UpdateAll call the enemy's own update() → Actor_ApplyDamage().
 *   This gives the host the full natural hit response: hit flash, stagger,
 *   damage sounds, and the normal death sequence (Actor_Kill → OnActorKill
 *   → SendPacket_ActorKilled).
 *
 * Flow:
 *   Client:  OnBeforeActorUpdate detects colChkInfo.damage > 0
 *            → zeroes local damage (no local HP change)
 *            → sends PLAYER_ATTACK_ACTOR(actorKey, damage)
 *   Host:    HandlePacket_PlayerAttackActor sets colChkInfo.damage
 *            → next frame: actor->update() calls Actor_ApplyDamage
 *            → death / OnActorKill / SendPacket_ActorKilled as normal
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

    // IMPORTANT: We cannot rely on queuing colChkInfo.damage and waiting for the
    // enemy's own update() to call Actor_ApplyDamage().  Most enemies gate damage
    // processing on the acHit pointer (set by CollisionCheck_ApplyDamage when an
    // actual AT/OC collision occurs).  Since no real collision happens on the host
    // machine for a client-side hit, acHit is never set, so queued damage is silently
    // ignored.  We therefore apply damage directly here.
    for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != nullptr) {
            if (actor->colChkInfo.health > 0 &&
                GetActorKey(actor, gPlayState->sceneNum) == actorKey)
            {
                // Set damage so Actor_ApplyDamage() reads the correct value,
                // then immediately apply it and clear the field so the actor's
                // own update() does not double-apply it next frame.
                actor->colChkInfo.damage = damage;
                Actor_ApplyDamage(actor);
                actor->colChkInfo.damage = 0;

                // If the hit was lethal, kill immediately.  Actor_Kill fires
                // OnActorKill → SendPacket_ActorKilled so all clients sync.
                // We don't wait for the enemy's update() because it may never
                // reach the health==0 death check without going through acHit.
                if (actor->colChkInfo.health == 0) {
                    Actor_Kill(actor);
                }
                return;
            }
            actor = actor->next;
        }
    }
}
