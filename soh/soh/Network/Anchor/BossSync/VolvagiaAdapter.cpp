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

// BossFd's real action-state (BOSSFD_FLY_MAIN/FLY_HOLE/BURROW/EMERGE/...) lives
// in work[BFD_ACTION_STATE] (s16 array, index 0), not actor->params — params is
// just a spawn/identity value. holeIndex and targetPosition decide which hole
// the dragon flies to; without forcing these too, each client's local RNG
// picks a different hole. Offsets from z_boss_fd.h (BossFd struct).
constexpr size_t kBfdActionStateOffset = 0x0222; // work[BFD_ACTION_STATE]
constexpr size_t kBfdTargetPosOffset   = 0x02BC; // Vec3f targetPosition
constexpr size_t kBfdHoleIndexOffset   = 0x02D4; // u8 holeIndex

int16_t ReadBfdActionState(const Actor* actor) {
    return *(const int16_t*)((const char*)actor + kBfdActionStateOffset);
}

void WriteBfdActionState(Actor* actor, int16_t value) {
    *(int16_t*)((char*)actor + kBfdActionStateOffset) = value;
}

u8 ReadBfdHoleIndex(const Actor* actor) {
    return *(const u8*)((const char*)actor + kBfdHoleIndexOffset);
}

void WriteBfdHoleIndex(Actor* actor, u8 value) {
    *(u8*)((char*)actor + kBfdHoleIndexOffset) = value;
}

Vec3f ReadBfdTargetPosition(const Actor* actor) {
    return *(const Vec3f*)((const char*)actor + kBfdTargetPosOffset);
}

void WriteBfdTargetPosition(Actor* actor, const Vec3f& value) {
    *(Vec3f*)((char*)actor + kBfdTargetPosOffset) = value;
}

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

    void Reset() override {
        gVolvagiaTrack.clear();
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
        const uint8_t hp = ReadClampedBossHealth(actor);
        const int stateId = (actor->id == ACTOR_BOSS_FD) ? (int)ReadBfdActionState(actor) : (int)actor->params;
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

        // Attack/hole-selection sync: captured BEFORE the early-return branches
        // below (which already advance lastStateId for their own bookkeeping),
        // so a hole change that lands on the same frame as an HP/phase/death
        // transition is queued instead of silently dropped.
        if (track.initialized && actor->id == ACTOR_BOSS_FD && stateId != track.lastStateId) {
            const u8 holeIndex = ReadBfdHoleIndex(actor);
            const Vec3f targetPos = ReadBfdTargetPosition(actor);

            nlohmann::json actionEvent;
            actionEvent["eventType"] = "BOSS_ACTION_STATE";
            actionEvent["eventKey"] = primaryKey;
            actionEvent["bossActorKey"] = primaryKey;
            actionEvent["bossActorId"] = (int)ACTOR_BOSS_FD;
            actionEvent["actionState"] = stateId;
            actionEvent["holeIndex"] = (int)holeIndex;
            actionEvent["targetX"] = targetPos.x;
            actionEvent["targetY"] = targetPos.y;
            actionEvent["targetZ"] = targetPos.z;
            actionEvent["seq"] = (uint32_t)play->state.frames;
            actionEvent["masterFrame"] = (uint32_t)play->state.frames;
            actionEvent["lateJoinCanSkipIntro"] = true;
            track.pendingEvents.push_back(actionEvent);
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
        // Always advance, even when no other branch fired this frame — otherwise
        // the action-state detector above would re-queue duplicate events forever.
        track.lastStateId = stateId;

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
        } else if (eventType == "BOSS_ACTION_STATE") {
            Actor* volvagia = nullptr;
            Actor* actor = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
            while (actor != nullptr) {
                if (actor->id == ACTOR_BOSS_FD && BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey) {
                    volvagia = actor;
                    break;
                }
                actor = actor->next;
            }
            if (!volvagia || volvagia->update == nullptr) return;

            const int16_t actionState = (int16_t)payload.value("actionState", (int)0);
            const u8 holeIndex = (u8)payload.value("holeIndex", (int)0);
            const Vec3f targetPos = {
                payload.value("targetX", volvagia->world.pos.x),
                payload.value("targetY", volvagia->world.pos.y),
                payload.value("targetZ", volvagia->world.pos.z),
            };

            // Force the SAME decision the master already made — the dragon's own
            // actionFunc keeps running locally and will act on these corrected
            // fields on its very next tick, no actionFunc swap required.
            WriteBfdActionState(volvagia, actionState);
            WriteBfdHoleIndex(volvagia, holeIndex);
            WriteBfdTargetPosition(volvagia, targetPos);
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

        const uint8_t hp = ReadClampedBossHealth(actor);
        const int stateId = (actor->id == ACTOR_BOSS_FD) ? (int)ReadBfdActionState(actor) : (int)actor->params;
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

        // Restore the exact action-state/hole/target so a late joiner sees the
        // dragon already committed to the same attack, not a fresh ROM default.
        if (volvagia->id == ACTOR_BOSS_FD) {
            const int16_t actionState = (int16_t)snapshot.value("stateId", (int)0);
            WriteBfdActionState(volvagia, actionState);
        }
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateVolvagiaAdapter() {
    return std::make_shared<VolvagiaAdapter>();
}

} // namespace AnchorBossSync
