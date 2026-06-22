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

void Anchor::QueueOrApplyBoulderSpawn(nlohmann::json payload, bool allowDelay) {
    if (!IsSaveLoaded() || !gPlayState) return;
    if (!payload.contains("actorKey") || !payload.contains("actorId")) return;

    s16 sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
    if (sceneNum != gPlayState->sceneNum) return;

    s8 roomNum = payload.value("roomNum", (s8)-1);
    const s8 curRoom = (s8)gPlayState->roomCtx.curRoom.num;
    if (roomNum != -1 && roomNum != curRoom) return;

    s16 actorId = (s16)payload.value("actorId", -1);
    if (!IsRollingBoulderActor(actorId)) return;

    std::string actorKey = payload["actorKey"].get<std::string>();
    if (actorKey.empty()) return;

    // If the actor is already present locally, this is either duplicate or stale.
    for (int cat = 0; cat < ACTORCAT_MAX; ++cat) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != nullptr) {
            if (GetActorKey(actor, sceneNum) == actorKey) {
                return;
            }
            actor = actor->next;
        }
    }

    uint32_t triggerFrame = payload.value("triggerFrame", (uint32_t)0);
    auto lastIt = lastBoulderTriggerFrameByKey.find(actorKey);
    if (lastIt != lastBoulderTriggerFrameByKey.end()) {
        if (triggerFrame != 0 && triggerFrame <= lastIt->second) {
            return;
        }
    }

    if (allowDelay && hasMasterFrameSync && triggerFrame != 0) {
        int32_t targetLocalFrame = (int32_t)triggerFrame + masterFrameToLocalOffset;
        payload["targetLocalFrame"] = targetLocalFrame;
        if ((int32_t)gPlayState->state.frames < targetLocalFrame) {
            pendingBoulderSpawns.push_back(payload);
            SPDLOG_INFO("[Anchor:EnemySync] QUEUE BOULDER_SPAWN | key={} trigger={} targetLocal={} now={}",
                        actorKey, triggerFrame, targetLocalFrame, (uint32_t)gPlayState->state.frames);
            return;
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

    lastBoulderTriggerFrameByKey[actorKey] = triggerFrame;

    SPDLOG_INFO("[Anchor:EnemySync] {}: BOULDER_SPAWN applied | actorId={} room={} key={} trigger={} now={}",
                IsEnemyAuthority() ? "HOST" : "CLIENT", (int)actorId, (int)roomNum, actorKey,
                triggerFrame, (uint32_t)gPlayState->state.frames);
}

void Anchor::ProcessPendingBoulderSpawns() {
    if (!IsSaveLoaded() || !gPlayState || pendingBoulderSpawns.empty()) {
        return;
    }

    const int32_t nowFrame = (int32_t)gPlayState->state.frames;
    std::vector<nlohmann::json> remaining;
    remaining.reserve(pendingBoulderSpawns.size());

    for (auto& payload : pendingBoulderSpawns) {
        int32_t targetLocalFrame = payload.value("targetLocalFrame", nowFrame);
        if (nowFrame < targetLocalFrame) {
            remaining.push_back(payload);
            continue;
        }
        QueueOrApplyBoulderSpawn(payload, false);
    }

    pendingBoulderSpawns.swap(remaining);
}

void Anchor::HandlePacket_BoulderSpawn(nlohmann::json payload) {
    QueueOrApplyBoulderSpawn(payload, true);
}
