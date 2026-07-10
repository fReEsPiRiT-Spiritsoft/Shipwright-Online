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

constexpr s16 kMorphaCoreParam = -1;
constexpr s16 kMorphaTentacleParam = 100;

enum MorphaCoreStateLocal {
    kCoreMove = 0,
    kCoreMakeTent = 1,
    kCoreUnderwater = 2,
    kCoreStunned = 5,
    kCoreAttack = 10,
    kCoreRetreat = 11,
    kCoreIntroWait = 20,
    kCoreIntroReveal = 21,
};

struct MorphaTrackState {
    bool initialized = false;
    uint8_t lastHp = 1;
    int lastPhaseId = -1;
    int lastTentacleCount = 0;
    std::unordered_set<std::string> tentacleKeys;
    std::deque<nlohmann::json> pendingEvents;
};

std::unordered_map<std::string, MorphaTrackState> gMorphaTrack;

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

int ComputePhaseFromCoreState(int coreState, int tentacleCount) {
    if (coreState == kCoreUnderwater) {
        return 0;
    }
    if (coreState == kCoreStunned) {
        return 1;
    }
    if (coreState == kCoreAttack || coreState == kCoreMakeTent || coreState == kCoreMove) {
        return (tentacleCount > 0) ? 0 : 2;
    }
    if (coreState == kCoreRetreat) {
        return 2;
    }
    return 0;
}

void QueueTentacleLossEvents(MorphaTrackState& track,
                             const std::unordered_set<std::string>& currentTentacles,
                             const std::string& coreKey,
                             Actor* coreActor,
                             PlayState* play) {
    for (const auto& key : track.tentacleKeys) {
        if (!currentTentacles.count(key)) {
            nlohmann::json event;
            event["eventType"] = "BOSS_SUBACTOR_KILL";
            event["eventKey"] = coreKey;
            event["targetActorKey"] = key;
            event["bossActorKey"] = coreKey;
            event["bossActorId"] = (int)coreActor->id;
            event["subState"] = "tentacleSevered";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            track.pendingEvents.push_back(event);
        }
    }
}

class MorphaBossAdapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
        return sceneNum == SCENE_WATER_TEMPLE_BOSS && actorId == ACTOR_BOSS_MO;
    }

    const char* Name() const override {
        return "MorphaBossAdapter";
    }

    void Reset() override {
        gMorphaTrack.clear();
    }

    nlohmann::json CaptureTransition(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        if (actor->params != kMorphaCoreParam) {
            return {};
        }

        const std::string coreKey = BuildActorKeyLocal(actor, play->sceneNum);
        MorphaTrackState& track = gMorphaTrack[coreKey];

        std::unordered_set<std::string> currentTentacles;
        int tentacleCount = 0;

        Actor* node = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
        while (node != nullptr) {
            if (node->id == ACTOR_BOSS_MO && node->params == kMorphaTentacleParam &&
                (node->room == actor->room || node->room == -1 || actor->room == -1)) {
                const std::string key = BuildActorKeyLocal(node, play->sceneNum);
                currentTentacles.insert(key);
                tentacleCount++;
            }
            node = node->next;
        }

        QueueTentacleLossEvents(track, currentTentacles, coreKey, actor, play);

        const uint8_t hp = actor->colChkInfo.health;
        const int phaseNow = ComputePhaseFromCoreState((int)actor->params, tentacleCount);

        if (!track.initialized) {
            track.initialized = true;
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastTentacleCount = tentacleCount;
            track.tentacleKeys = currentTentacles;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = coreKey;
            event["bossActorKey"] = coreKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["tentaclesAlive"] = tentacleCount;
            event["subState"] = "morphaCore";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp == 0 && track.lastHp != 0) {
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastTentacleCount = tentacleCount;
            track.tentacleKeys = currentTentacles;

            nlohmann::json event;
            event["eventType"] = "BOSS_DEATH_COMMIT";
            event["eventKey"] = coreKey;
            event["bossActorKey"] = coreKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = 0;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (phaseNow != track.lastPhaseId) {
            track.lastPhaseId = phaseNow;
            track.lastHp = hp;
            track.lastTentacleCount = tentacleCount;
            track.tentacleKeys = currentTentacles;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = coreKey;
            event["bossActorKey"] = coreKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["tentaclesAlive"] = tentacleCount;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (tentacleCount != track.lastTentacleCount) {
            track.lastTentacleCount = tentacleCount;
            track.tentacleKeys = currentTentacles;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_DESTROY";
            event["eventKey"] = coreKey;
            event["bossActorKey"] = coreKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["tentaclesAlive"] = tentacleCount;
            event["subState"] = "tentacleDestroyed";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp < track.lastHp) {
            const uint8_t oldHp = track.lastHp;
            track.lastHp = hp;
            track.tentacleKeys = currentTentacles;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_HIT";
            event["eventKey"] = coreKey;
            event["bossActorKey"] = coreKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["damage"] = (int)(oldHp - hp);
            event["tentaclesAlive"] = tentacleCount;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp != track.lastHp) {
            track.lastHp = hp;
        }

        track.tentacleKeys = currentTentacles;

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
        const std::string targetActorKey = payload.value("targetActorKey", std::string(""));
        const uint8_t hp = (uint8_t)payload.value("hp", 1);

        if (eventType == "BOSS_WEAKPOINT_HIT" || eventType == "BOSS_STAGE_ENTER") {
            Actor* morpha = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_MO) {
                        morpha = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (morpha) break;
            }
            if (morpha && morpha->update != nullptr) {
                if (hp < morpha->colChkInfo.health || hp == 0) {
                    morpha->colChkInfo.health = hp;
                }
                if (eventType == "BOSS_WEAKPOINT_HIT") {
                    Actor_SetColorFilter(morpha, 0x4000, 0xFF, 0, 8);
                }
            }
        } else if (eventType == "BOSS_SUBACTOR_KILL") {
            Actor* tentacle = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == targetActorKey && actor->id == ACTOR_BOSS_MO) {
                        tentacle = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (tentacle) break;
            }
            if (tentacle && tentacle->update != nullptr) {
                tentacle->colChkInfo.health = 0;
            }
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr || actor->params != kMorphaCoreParam) {
            return {};
        }

        const std::string coreKey = BuildActorKeyLocal(actor, play->sceneNum);

        int tentacleCount = 0;
        Actor* node = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
        while (node != nullptr) {
            if (node->id == ACTOR_BOSS_MO && node->params == kMorphaTentacleParam &&
                (node->room == actor->room || node->room == -1 || actor->room == -1)) {
                tentacleCount++;
            }
            node = node->next;
        }

        const uint8_t hp = actor->colChkInfo.health;

        nlohmann::json snap;
        snap["bossActorKey"] = coreKey;
        snap["bossActorId"] = (int)actor->id;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = ComputePhaseFromCoreState((int)actor->params, tentacleCount);
        snap["hp"] = (int)hp;
        snap["tentaclesAlive"] = tentacleCount;
        snap["lateJoinCanSkipIntro"] = true;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const uint8_t hp = (uint8_t)snapshot.value("hp", 1);

        Actor* morpha = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_MO) {
                    morpha = actor;
                    break;
                }
                actor = actor->next;
            }
            if (morpha) break;
        }
        if (!morpha || !morpha->update) return;

        morpha->colChkInfo.health = hp;
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateMorphaBossAdapter() {
    return std::make_shared<MorphaBossAdapter>();
}

} // namespace AnchorBossSync
