#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

namespace {
bool IsRollingBoulderActor(s16 actorId) {
    return actorId == ACTOR_EN_BW || actorId == ACTOR_EN_GOROIWA;
}
} // namespace

void Anchor::SendPacket_BoulderSpawn(const Actor* actor) {
    if (!IsSaveLoaded() || !gPlayState || actor == nullptr) return;
    if (!roomState.syncEnemies) return;
    if (!IsRollingBoulderActor(actor->id)) return;

    nlohmann::json payload;
    payload["type"] = BOULDER_SPAWN;
    payload["quiet"] = true;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"] = (s8)gPlayState->roomCtx.curRoom.num;
    payload["actorKey"] = GetActorKey(actor, gPlayState->sceneNum);
    payload["actorId"] = (int)actor->id;
    payload["params"] = (int)actor->params;
    payload["posX"] = actor->world.pos.x;
    payload["posY"] = actor->world.pos.y;
    payload["posZ"] = actor->world.pos.z;
    payload["rotX"] = (int)actor->world.rot.x;
    payload["rotY"] = (int)actor->world.rot.y;
    payload["rotZ"] = (int)actor->world.rot.z;
    payload["triggerFrame"] = (u32)gPlayState->state.frames;

    SPDLOG_INFO("[Anchor:EnemySync] {}: BOULDER_SPAWN send | actorId={} room={} key={}",
                IsEnemyAuthority() ? "HOST" : "CLIENT", (int)actor->id,
                (int)(s8)gPlayState->roomCtx.curRoom.num,
                payload["actorKey"].get<std::string>());

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_BoulderSpawn(nlohmann::json payload) {
    if (!IsSaveLoaded() || !gPlayState) return;
    if (!payload.contains("actorKey") || !payload.contains("actorId")) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    s8 roomNum = payload.value("roomNum", (s8)-1);
    if (roomNum != (s8)gPlayState->roomCtx.curRoom.num) return;

    s16 actorId = (s16)payload.value("actorId", -1);
    if (!IsRollingBoulderActor(actorId)) return;

    std::string actorKey = payload["actorKey"].get<std::string>();

    for (int cat = 0; cat < ACTORCAT_MAX; ++cat) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != nullptr) {
            if (GetActorKey(actor, sceneNum) == actorKey) {
                return;
            }
            actor = actor->next;
        }
    }

    f32 posX = payload.value("posX", 0.0f);
    f32 posY = payload.value("posY", 0.0f);
    f32 posZ = payload.value("posZ", 0.0f);
    s16 rotX = (s16)payload.value("rotX", 0);
    s16 rotY = (s16)payload.value("rotY", 0);
    s16 rotZ = (s16)payload.value("rotZ", 0);
    s16 params = (s16)payload.value("params", 0);

    isSpawningRemoteBoulder = true;
    Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, posX, posY, posZ, rotX, rotY, rotZ, params);
    isSpawningRemoteBoulder = false;

    if (spawned == nullptr) {
        SPDLOG_WARN("[Anchor:EnemySync] {}: BOULDER_SPAWN spawn failed | actorId={} room={} key={}",
                    IsEnemyAuthority() ? "HOST" : "CLIENT", (int)actorId, (int)roomNum, actorKey);
        return;
    }

    SPDLOG_INFO("[Anchor:EnemySync] {}: BOULDER_SPAWN applied | actorId={} room={} key={}",
                IsEnemyAuthority() ? "HOST" : "CLIENT", (int)actorId, (int)roomNum, actorKey);
}
