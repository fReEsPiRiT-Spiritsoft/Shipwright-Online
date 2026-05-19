#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * ENEMY_DROP_ITEM
 *
 * Sent by the enemy authority whenever an En_Item00 collectible spawns as the
 * direct result of an enemy death action (via Item_DropCollectible).
 *
 * The receiving non-authority client spawns a matching En_Item00 at the same
 * position so both players can compete for the loot.  The existing ITEM_PICKUP
 * packet then ensures first-wins: whichever player picks up the item sends
 * ITEM_PICKUP and the other side removes their local copy via Actor_Kill.
 *
 * Flow (authority side):
 *   1. Enemy death action calls Item_DropCollectible → Actor_Spawn(EN_ITEM00)
 *   2. OnActorSpawn(EN_ITEM00) hook records entry in recentCollectibleSpawns.
 *   3. OnActorKill(ENEMY) hook (same frame) sends ENEMY_DROP_ITEM for every
 *      recentCollectibleSpawn within kDropRadiusSq of the dead enemy.
 *   4. OnGameFrameUpdate clears recentCollectibleSpawns.
 *
 * Flow (client side):
 *   HandlePacket_EnemyDropItem spawns EN_ITEM00 at the received position.
 *   isSpawningRemoteCollectible suppresses the OnActorSpawn echo.
 */

void Anchor::SendPacket_EnemyDropItem(s16 params, float x, float y, float z) {
    if (!IsSaveLoaded() || !gPlayState) return;

    nlohmann::json payload;
    payload["type"]     = ENEMY_DROP_ITEM;
    payload["quiet"]    = true;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"]  = (s8)gPlayState->roomCtx.curRoom.num;
    payload["params"]   = params;
    payload["posX"]     = x;
    payload["posY"]     = y;
    payload["posZ"]     = z;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_EnemyDropItem(nlohmann::json payload) {
    if (!IsSaveLoaded() || IsEnemyAuthority() || !gPlayState) return;
    if (!payload.contains("params")) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    s16   params = payload["params"].get<s16>();
    float x      = payload.value("posX", 0.0f);
    float y      = payload.value("posY", 0.0f);
    float z      = payload.value("posZ", 0.0f);

    // Guard: mark the spawn as remote so OnActorSpawn does not echo it back.
    isSpawningRemoteCollectible = true;
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_ITEM00,
                x, y, z, 0, 0, 0, params);
    isSpawningRemoteCollectible = false;
}
