#include "soh/Network/Anchor/BossSync/BossSyncRegistry.h"

#include <algorithm>
#include <unordered_map>

extern "C" {
#include "variables.h"
#include "functions.h"
}

namespace AnchorBossSync {

namespace {

struct BigOctoTrackState {
    bool initialized = false;
    uint8_t initialHp = 1;
    uint8_t lastHp = 1;
    int lastPhaseId = -1;
};

std::unordered_map<std::string, BigOctoTrackState> gBigOctoTrack;

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

int ComputePhaseByHp(uint8_t hp, uint8_t initialHp) {
    if (initialHp <= 1) {
        return hp == 0 ? 2 : 0;
    }

    float ratio = (float)hp / (float)std::max<uint8_t>(initialHp, 1);
    if (hp == 0) return 2;
    if (ratio <= 0.4f) return 1;
    return 0;
}

class BigOctoMinibossAdapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
        return sceneNum == SCENE_JABU_JABU && actorId == ACTOR_EN_BIGOKUTA;
    }

    const char* Name() const override {
        return "BigOctoMinibossAdapter";
    }

    void Reset() override {
        gBigOctoTrack.clear();
    }
(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        const std::string actorKey = BuildActorKeyLocal(actor, play->sceneNum);
        BigOctoTrackState& track = gBigOctoTrack[actorKey];

        const uint8_t hp = actor->colChkInfo.health;
        if (!track.initialized) {
            track.initialized = true;
            track.initialHp = std::max<uint8_t>(hp, 1);
            track.lastHp = hp;
            track.lastPhaseId = ComputePhaseByHp(hp, track.initialHp);

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = actorKey;
            event["bossActorKey"] = actorKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = track.lastPhaseId;
            event["hp"] = (int)hp;
            event["isMiniBoss"] = true;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = false;
            return event;
        }

        const int phaseNow = ComputePhaseByHp(hp, track.initialHp);

        if (hp == 0 && track.lastHp != 0) {
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;

            nlohmann::json event;
            event["eventType"] = "BOSS_SUBACTOR_KILL";
            event["eventKey"] = actorKey;
            event["targetActorKey"] = actorKey;
            event["bossActorKey"] = actorKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = 0;
            event["isMiniBoss"] = true;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = false;
            return event;
        }

        if (phaseNow != track.lastPhaseId) {
            track.lastPhaseId = phaseNow;
            track.lastHp = hp;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = actorKey;
            event["bossActorKey"] = actorKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["isMiniBoss"] = true;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = false;
            return event;
        }

        if (hp < track.lastHp) {
            const uint8_t oldHp = track.lastHp;
            track.lastHp = hp;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_HIT";
            event["eventKey"] = actorKey;
            event["bossActorKey"] = actorKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["damage"] = (int)(oldHp - hp);
            event["isMiniBoss"] = true;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = false;
            return event;
        }

        if (hp != track.lastHp) {
            track.lastHp = hp;
        }

        return {};
    }

    void ApplyEvent(PlayState* play, const nlohmann::json& payload) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string eventType = payload.value("eventType", std::string(""));
        const std::string bossActorKey = payload.value("bossActorKey", std::string(""));
        const std::string targetActorKey = payload.value("targetActorKey", std::string(""));

        Actor* bigocto = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_EN_BIGOKUTA) {
                    bigocto = actor;
                    break;
                }
                actor = actor->next;
            }
            if (bigocto) break;
        }
        if (!bigocto) return;

        if (!bigocto->update) return;
        if (eventType == "BOSS_WEAKPOINT_HIT") {
            const uint8_t newHp = (uint8_t)payload.value("hp", 0);
            if (newHp < bigocto->colChkInfo.health || newHp == 0) {
                bigocto->colChkInfo.health = newHp;
                Actor_SetColorFilter(bigocto, 0x4000, 0xFF, 0, 8);
            }
        } else if (eventType == "BOSS_STAGE_ENTER") {
            const uint8_t newHp = (uint8_t)payload.value("hp", 0);
            if (newHp < bigocto->colChkInfo.health || newHp == 0) {
                bigocto->colChkInfo.health = newHp;
            }
        } else if (eventType == "BOSS_SUBACTOR_KILL") {
            bigocto->colChkInfo.health = 0;
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        const std::string actorKey = BuildActorKeyLocal(actor, play->sceneNum);
        const uint8_t hp = actor->colChkInfo.health;

        nlohmann::json snap;
        snap["bossActorKey"] = actorKey;
        snap["bossActorId"] = (int)actor->id;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = ComputePhaseByHp(hp, gBigOctoTrack.count(actorKey) ? gBigOctoTrack[actorKey].initialHp : std::max<uint8_t>(hp, 1));
        snap["hp"] = (int)hp;
        snap["isMiniBoss"] = true;
        snap["lateJoinCanSkipIntro"] = false;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const uint8_t hp = (uint8_t)snapshot.value("hp", 1);

        Actor* bigocto = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_EN_BIGOKUTA) {
                    bigocto = actor;
                    break;
                }
                actor = actor->next;
            }
            if (bigocto) break;
        }
        if (!bigocto || !bigocto->update) return;

        bigocto->colChkInfo.health = hp;
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateBigOctoMinibossAdapter() {
    return std::make_shared<BigOctoMinibossAdapter>();
}

} // namespace AnchorBossSync
