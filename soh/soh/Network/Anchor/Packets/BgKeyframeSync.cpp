#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * BG_KEYFRAME_SYNC
 *
 * Authoritative keyframe packet for background / environment actors
 * (ACTORCAT_BG and ACTORCAT_PROP) — moving blades, rotating platforms,
 * elevators, etc.
 *
 * Unlike enemy sync, BG actors are NOT frozen on the client; their own
 * Update() logic provides dead-reckoning between keyframes.  A keyframe is
 * sent by the authority:
 *   a) Every BG_KEYFRAME_INTERVAL_MS milliseconds (heartbeat, FPS-unabhaengig), or
 *   b) Immediately when the actor's velocity reverses direction (platform
 *      turns around), so the client corrects without waiting for the heartbeat.
 *
 * On the client the received target position is blended in smoothly every
 * frame using Math_ApproachF() to avoid visible position pops.
 */

// ─── Send ─────────────────────────────────────────────────────────────────────

void Anchor::SendPacket_BgKeyframeSync(const Actor* actor) {
    if (!IsSaveLoaded() || !gPlayState) return;

    nlohmann::json payload;
    payload["type"]     = BG_KEYFRAME_SYNC;
    payload["quiet"]    = true;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actorId"]  = (int)actor->id;  // Used on receive side to filter EN_HORSE
    payload["actorKey"] = GetActorKey(actor, gPlayState->sceneNum);
    payload["posX"]     = actor->world.pos.x;
    payload["posY"]     = actor->world.pos.y;
    payload["posZ"]     = actor->world.pos.z;
    payload["velX"]     = actor->velocity.x;
    payload["velY"]     = actor->velocity.y;
    payload["velZ"]     = actor->velocity.z;
    payload["rotY"]     = (int)actor->world.rot.y;

    SendJsonToRemote(payload);
}

// ─── Handle ───────────────────────────────────────────────────────────────────

void Anchor::HandlePacket_BgKeyframeSync(nlohmann::json payload) {
    // Only non-authority clients apply incoming keyframes.
    if (!IsSaveLoaded() || IsEnemyAuthority() || !gPlayState) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    // Epona (ACTOR_EN_HORSE) runs independently on each client — her position
    // must not be driven by the Master's keyframes, otherwise local Epona gets
    // pulled to underground positions whenever terrain heights differ between
    // clients (the source of the "Epona comes from underground" and
    // "Client can't see/mount their own Epona" bugs).
    const s16 actorId = (s16)payload.value("actorId", (int)-1);
    if (actorId == ACTOR_EN_HORSE) return;

    if (!payload.contains("actorKey")) return;

    std::string actorKey = payload["actorKey"].get<std::string>();

    // Skip if the LOCAL player is currently carrying this actor.
    // The local carry physics must win over the remote keyframe to keep the
    // item in the player's hands and allow a clean throw afterwards.
    if (gPlayState) {
        Player* player = (Player*)gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head;
        if (player && (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) &&
            player->heldActor != nullptr) {
            if (GetActorKey(player->heldActor, sceneNum) == actorKey) {
                return;
            }
        }
    }

    BgKeyframeTarget target;
    target.pos.x = payload.value("posX", 0.0f);
    target.pos.y = payload.value("posY", 0.0f);
    target.pos.z = payload.value("posZ", 0.0f);
    target.rotY  = (s16)payload.value("rotY", 0);
    target.lastReceivedAt = std::chrono::steady_clock::now();

    // Store the target — the per-frame blend hook in HookHandlers.cpp will
    // smoothly approach the position using Math_ApproachF().
    bgActorKeyframeTarget[actorKey] = target;
}
