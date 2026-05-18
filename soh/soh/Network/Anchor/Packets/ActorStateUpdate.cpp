#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * ACTOR_STATE_UPDATE
 *
 * Sent by the enemy authority (room owner) to sync enemy HP to all other clients.
 *
 * Fires when an enemy or boss actor's health changes on the authority client.
 * Non-authority clients update the matching local actor's HP to maintain consistency.
 *
 * Actor identification uses a composite key:
 *   sceneNum + category + actorId + room + params + home.pos (spawn position)
 * This uniquely identifies any actor instance across all clients since all clients
 * start with the same actor spawn data from the ROM.
 */

std::string Anchor::GetActorKey(const Actor* actor, s16 sceneNum) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%d_%d_%d_%d_%d_%d_%d_%d",
        (int)sceneNum,
        (int)actor->category,
        (int)actor->id,
        (int)actor->room,
        (int)actor->params,
        (int)actor->home.pos.x,
        (int)actor->home.pos.y,
        (int)actor->home.pos.z);
    return std::string(buf);
}

bool Anchor::IsEnemyAuthority() {
    return isConnected && ownClientId != 0 && ownClientId == roomState.ownerClientId;
}

void Anchor::SendPacket_ActorStateUpdate(const Actor* actor) {
    if (!IsSaveLoaded()) return;

    nlohmann::json payload;
    payload["type"] = ACTOR_STATE_UPDATE;
    payload["quiet"] = true; // High-frequency packet, don't spam debug log
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actorKey"] = GetActorKey(actor, gPlayState->sceneNum);
    payload["health"] = actor->colChkInfo.health;
    // Position and rotation so non-authority clients track enemy location
    payload["posX"] = actor->world.pos.x;
    payload["posY"] = actor->world.pos.y;
    payload["posZ"] = actor->world.pos.z;
    payload["rotY"] = (int)actor->world.rot.y;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_ActorStateUpdate(nlohmann::json payload) {
    if (!IsSaveLoaded() || IsEnemyAuthority()) return;
    if (!payload.contains("actorKey") || !payload.contains("health")) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    std::string actorKey = payload["actorKey"].get<std::string>();
    u8 health = payload["health"].get<u8>();
    float posX = payload.value("posX", 0.0f);
    float posY = payload.value("posY", 0.0f);
    float posZ = payload.value("posZ", 0.0f);
    s16   rotY = (s16)payload.value("rotY", 0);
    bool hasPos = payload.contains("posX");

    // Find matching actor in the local scene and update its HP
    for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != nullptr) {
            if (GetActorKey(actor, sceneNum) == actorKey) {
                actor->colChkInfo.health = health;
                // Override position so both clients see the enemy in the same place
                if (hasPos) {
                    actor->world.pos.x = posX;
                    actor->world.pos.y = posY;
                    actor->world.pos.z = posZ;
                    actor->world.rot.y = rotY;
                }
                // Mark this as a remote update so OnActorUpdate on the non-authority
                // won't forward the change back to the owner as a local hit.
                pendingRemoteHealthOverride[actorKey] = health;
                return;
            }
            actor = actor->next;
        }
    }
}
