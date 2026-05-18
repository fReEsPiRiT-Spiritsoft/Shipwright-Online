#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * ITEM_PICKUP
 *
 * Sent by either player when they pick up a dropped collectible (En_Item00).
 * The receiver removes their local copy of the item so only one player gets it.
 *
 * Both sides spawn dropped items independently when an enemy dies (each side runs
 * the death animation + Item_DropCollectible calls locally).  When one player walks
 * over an item, En_Item00 calls Actor_Kill → OnActorKill fires → ITEM_PICKUP sent.
 * The remote side matches the item by scene, room, params, and closest world position,
 * then silently removes it via Actor_Kill (guarded by isRemovingRemoteItem so the
 * resulting OnActorKill does not echo another packet back).
 */

void Anchor::SendPacket_ItemPickup(const Actor* actor) {
    if (!IsSaveLoaded() || !gPlayState) return;

    nlohmann::json payload;
    payload["type"]     = ITEM_PICKUP;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"]  = (s8)gPlayState->roomCtx.curRoom.num;
    payload["params"]   = actor->params;
    payload["posX"]     = actor->world.pos.x;
    payload["posY"]     = actor->world.pos.y;
    payload["posZ"]     = actor->world.pos.z;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_ItemPickup(nlohmann::json payload) {
    if (!IsSaveLoaded() || !gPlayState) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    s16 params = payload.value("params", (s16)0);
    float px   = payload.value("posX", 0.0f);
    float py   = payload.value("posY", 0.0f);
    float pz   = payload.value("posZ", 0.0f);

    // Find the closest En_Item00 actor with matching params within tolerance.
    // Position-based matching handles the case where multiple items of the same
    // type exist in the room simultaneously.
    constexpr f32 kMaxDistSq = 100.0f * 100.0f; // 100-unit tolerance
    Actor* best = nullptr;
    f32    bestDistSq = kMaxDistSq;

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_MISC].head;
    while (actor != nullptr) {
        Actor* next = actor->next;
        if (actor->id == ACTOR_EN_ITEM00 && actor->params == params) {
            Vec3f remotePos = { px, py, pz };
            f32 distSq = Math3D_Vec3fDistSq(&actor->world.pos, &remotePos);
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                best = actor;
            }
        }
        actor = next;
    }

    if (best != nullptr) {
        // Guard so OnActorKill does not echo this back to the sender.
        isRemovingRemoteItem = true;
        Actor_Kill(best);
        isRemovingRemoteItem = false;
    }
}
