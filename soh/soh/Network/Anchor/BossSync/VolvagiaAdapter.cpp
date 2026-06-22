#include "soh/Network/Anchor/BossSync/BossSyncRegistry.h"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

extern "C" {
#include "variables.h"
#include "functions.h"
}

namespace AnchorBossSync {

namespace {

constexpr s16 kVolvagiaPrimaryParam = 0;

enum VolvagiaStateLocal {
    kWaitIntro = -1,
    kFlyMain = 0,
    kFlyHole = 1,
    kBurrow = 2,
    kEmerge = 3,
    kFlyCeiling = 50,
    kDropRocks = 51,
    kFlyChase = 100,
    kDeathStart = 200,
    kSkinBurn = 201,
    kBonesFall = 202,
    kSkullPause = 203,
    kSkullFall = 204,
    kSkullBurn = 205,
};

struct VolvagiaTrackState {
    bool initialized = false;
    uint8_t lastHp = 1;
    int lastPhaseId = -1;
    int lastStateId = -1;
    bool lastArenaCollapsed = false;
    bool lastIsHole = false;
    std::deque<nlohmann::json> pendingEvents;
};

std::unordered_map<std::string, VolvagiaTrackState> gVolvagiaTrack;

std::string BuildActorKeyLocal(const Actor* actor, s16 sceneNum) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%d_%d_%d_%d_%d_%d_%d_%d_%d_%d_%d",
             (int)sceneNum,
             (int)actor->category,
             (int)actor->id,
             (int)actor->room,
             (int)actor->params,
             (int)actor->home.pos.x,
             (int)actor->home.pos.y,
             (int)actor->home.pos.z,
             (int)actor->home.rot.x,
             (int)actor->home.rot.y,
             (int)actor->home.rot.z);
    return std::string(buf);
}

int ComputePhaseFromVolvagiaState(int stateId, bool isHole) {
    if (stateId == kWaitIntro) {
        return 0;
    }
    if (stateId == kFlyMain || stateId == kFlyChase || stateId == kFlyCeiling) {
        return 0;
    }
    if (stateId == kFlyHole || stateId == kDropRocks) {
        return 1;
    }
    if (stateId == kBurrow || stateId == kEmerge) {
        return (isHole) ? 1 : 2;
    }
    if (stateId >= kDeathStart) {
        return 3;
    }
    return 0;
}

class VolvagiaAdapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
        return sceneNum == SCENE_FIRE_TEMPLE_BOSS &&
               (actorId == ACTOR_BOSS_FD || actorId == ACTOR_BOSS_FD2);
    }

    const char* Name() const override {
        return "VolvagiaAdapter";
    }

    nlohmann::json CaptureTransition(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        if (!(actor->id == ACTOR_BOSS_FD || actor->id == ACTOR_BOSS_FD2)) {
            return {};
        }

        const std::string primaryKey =
            (actor->id == ACTOR_BOSS_FD) ? BuildActorKeyLocal(actor, play->sceneNum) : "volvagia_primary";

        VolvagiaTrackState& track = gVolvagiaTrack[primaryKey];

        bool isHole = (actor->id == ACTOR_BOSS_FD2);
        const uint8_t hp = actor->colChkInfo.health;
        const int stateId = (int)actor->params;
        const int phaseNow = ComputePhaseFromVolvagiaState(stateId, isHole);

        bool arenaCollapsed = false;
        Actor* node = play->actorCtx.actorLists[ACTORCAT_BG].head;
        while (node != nullptr) {
            if (node->id == ACTOR_BG_VB_SIMA && (node->room == actor->room || node->room == -1 || actor->room == -1)) {
                if (node->colChkInfo.health == 0) {
                    arenaCollapsed = true;
                }
            }
            node = node->next;
        }

        if (!track.initialized) {
            track.initialized = true;
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastStateId = stateId;
            track.lastArenaCollapsed = arenaCollapsed;
            track.lastIsHole = isHole;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = primaryKey;
            event["bossActorKey"] = primaryKey;
            event["bossActorId"] = (int)ACTOR_BOSS_FD;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["arenaCollapsed"] = arenaCollapsed;
            event["isHole"] = isHole;
            event["stateId"] = stateId;
            event["subState"] = "volvagia_intro";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp == 0 && track.lastHp != 0) {
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastStateId = stateId;
            track.lastArenaCollapsed = arenaCollapsed;
            track.lastIsHole = isHole;

            nlohmann::json event;
            event["eventType"] = "BOSS_DEATH_COMMIT";
            event["eventKey"] = primaryKey;
            event["bossActorKey"] = primaryKey;
            event["bossActorId"] = (int)ACTOR_BOSS_FD;
            event["phaseId"] = phaseNow;
            event["hp"] = 0;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (arenaCollapsed && !track.lastArenaCollapsed) {
            track.lastArenaCollapsed = arenaCollapsed;
            track.lastPhaseId = phaseNow;
            track.lastStateId = stateId;
            track.lastIsHole = isHole;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = primaryKey;
            event["bossActorKey"] = primaryKey;
            event["bossActorId"] = (int)ACTOR_BOSS_FD;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["arenaCollapsed"] = true;
            event["subState"] = "arena_collapse";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (phaseNow != track.lastPhaseId) {
            track.lastPhaseId = phaseNow;
            track.lastStateId = stateId;
            track.lastHp = hp;
            track.lastArenaCollapsed = arenaCollapsed;
            track.lastIsHole = isHole;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = primaryKey;
            event["bossActorKey"] = primaryKey;
            event["bossActorId"] = (int)ACTOR_BOSS_FD;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["arenaCollapsed"] = arenaCollapsed;
            event["isHole"] = isHole;
            event["stateId"] = stateId;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp < track.lastHp) {
            const uint8_t oldHp = track.lastHp;
            track.lastHp = hp;
            track.lastArenaCollapsed = arenaCollapsed;
            track.lastIsHole = isHole;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_HIT";
            event["eventKey"] = primaryKey;
            event["bossActorKey"] = primaryKey;
            event["bossActorId"] = (int)ACTOR_BOSS_FD;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["damage"] = (int)(oldHp - hp);
            event["isHole"] = isHole;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp != track.lastHp) {
            track.lastHp = hp;
        }

        track.lastArenaCollapsed = arenaCollapsed;
        track.lastIsHole = isHole;

        if (!track.pendingEvents.empty()) {
            nlohmann::json next = track.pendingEvents.front();
            track.pendingEvents.pop_front();
            return next;
        }

        return {};
    }

    void ApplyEvent(PlayState* play, const nlohmann::json& payload) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string eventType = payload.value("eventType", std::string(""));
        const std::string bossActorKey = payload.value("bossActorKey", std::string(""));
        const uint8_t hp = (uint8_t)payload.value("hp", 1);

        if (eventType == "BOSS_WEAKPOINT_HIT" || eventType == "BOSS_STAGE_ENTER") {
            Actor* volvagia = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && 
                        (actor->id == ACTOR_BOSS_FD || actor->id == ACTOR_BOSS_FD2)) {
                        volvagia = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (volvagia) break;
            }
            if (volvagia && volvagia->update != nullptr) {
                if (hp < volvagia->colChkInfo.health || hp == 0) {
                    volvagia->colChkInfo.health = hp;
                }
                if (eventType == "BOSS_WEAKPOINT_HIT") {
                    Actor_SetColorFilter(volvagia, 0x4000, 0xFF, 0, 8);
                }
            }
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        if (!(actor->id == ACTOR_BOSS_FD || actor->id == ACTOR_BOSS_FD2)) {
            return {};
        }

        const std::string primaryKey =
            (actor->id == ACTOR_BOSS_FD) ? BuildActorKeyLocal(actor, play->sceneNum) : "volvagia_primary";

        bool isHole = (actor->id == ACTOR_BOSS_FD2);

        bool arenaCollapsed = false;
        Actor* node = play->actorCtx.actorLists[ACTORCAT_BG].head;
        while (node != nullptr) {
            if (node->id == ACTOR_BG_VB_SIMA && (node->room == actor->room || node->room == -1 || actor->room == -1)) {
                if (node->colChkInfo.health == 0) {
                    arenaCollapsed = true;
                }
            }
            node = node->next;
        }

        const uint8_t hp = actor->colChkInfo.health;
        const int stateId = (int)actor->params;
        const int phaseNow = ComputePhaseFromVolvagiaState(stateId, isHole);

        nlohmann::json snap;
        snap["bossActorKey"] = primaryKey;
        snap["bossActorId"] = (int)ACTOR_BOSS_FD;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = phaseNow;
        snap["hp"] = (int)hp;
        snap["arenaCollapsed"] = arenaCollapsed;
        snap["isHole"] = isHole;
        snap["stateId"] = stateId;
        snap["lateJoinCanSkipIntro"] = true;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const uint8_t hp = (uint8_t)snapshot.value("hp", 1);

        Actor* volvagia = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && 
                    (actor->id == ACTOR_BOSS_FD || actor->id == ACTOR_BOSS_FD2)) {
                    volvagia = actor;
                    break;
                }
                actor = actor->next;
            }
            if (volvagia) break;
        }
        if (!volvagia || !volvagia->update) return;

        volvagia->colChkInfo.health = hp;
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateVolvagiaAdapter() {
    return std::make_shared<VolvagiaAdapter>();
}

} // namespace AnchorBossSync
