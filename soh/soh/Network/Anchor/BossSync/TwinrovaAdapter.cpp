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

enum TwinrovaFormLocal {
    kKotake = 0,
    kKoume = 1,
    kTwinrova = 2,
};

struct TwinrovaTrackState {
    bool initialized = false;
    uint8_t lastHp = 1;
    int lastPhaseId = -1;
    int lastFormId = -1;
    bool lastStunned = false;
    std::deque<nlohmann::json> pendingEvents;
};

std::unordered_map<std::string, TwinrovaTrackState> gTwinrovaTrack;

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

int ComputePhaseFromTwinrovaForm(int formId) {
    if (formId == kKoume) {
        return 0;
    }
    if (formId == kKotake) {
        return 1;
    }
    if (formId == kTwinrova) {
        return 2;
    }
    return 0;
}

class TwinrovaAdapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
        return sceneNum == SCENE_SPIRIT_TEMPLE_BOSS && actorId == ACTOR_BOSS_TW;
    }

    const char* Name() const override {
        return "TwinrovaAdapter";
    }

    void Reset() override {
        gTwinrovaTrack.clear();
    }

    nlohmann::json CaptureTransition(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        if (actor->id != ACTOR_BOSS_TW) {
            return {};
        }

        const std::string twinrovaKey = BuildActorKeyLocal(actor, play->sceneNum);
        TwinrovaTrackState& track = gTwinrovaTrack[twinrovaKey];

        const uint8_t hp = actor->colChkInfo.health;
        const int formId = (int)actor->params;
        const int phaseNow = ComputePhaseFromTwinrovaForm(formId);

        bool stunned = false;
        if (actor->params <= kTwinrova) {
            const char* actorPtr = (const char*)actor;
            const uint8_t* stunField = (const uint8_t*)(actorPtr + 0x04F4);
            stunned = (*stunField != 0);
        }

        if (!track.initialized) {
            track.initialized = true;
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastFormId = formId;
            track.lastStunned = stunned;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = twinrovaKey;
            event["bossActorKey"] = twinrovaKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["formId"] = formId;
            event["formName"] = (formId == kKoume) ? "koume" : (formId == kKotake) ? "kotake" : "twinrova";
            event["subState"] = "twinrova_intro";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp == 0 && track.lastHp != 0) {
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastFormId = formId;
            track.lastStunned = stunned;

            nlohmann::json event;
            event["eventType"] = "BOSS_DEATH_COMMIT";
            event["eventKey"] = twinrovaKey;
            event["bossActorKey"] = twinrovaKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = 3;
            event["hp"] = 0;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (formId != track.lastFormId) {
            track.lastFormId = formId;
            track.lastPhaseId = phaseNow;
            track.lastHp = hp;
            track.lastStunned = stunned;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = twinrovaKey;
            event["bossActorKey"] = twinrovaKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["formId"] = formId;
            event["formName"] = (formId == kKoume) ? "koume" : (formId == kKotake) ? "kotake" : "twinrova";
            event["subState"] = "form_transition";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (stunned && !track.lastStunned) {
            track.lastStunned = stunned;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_DESTROY";
            event["eventKey"] = twinrovaKey;
            event["bossActorKey"] = twinrovaKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["formId"] = formId;
            event["subState"] = "stunned";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (!stunned && track.lastStunned) {
            track.lastStunned = stunned;
        }

        if (hp < track.lastHp) {
            const uint8_t oldHp = track.lastHp;
            track.lastHp = hp;
            track.lastStunned = stunned;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_HIT";
            event["eventKey"] = twinrovaKey;
            event["bossActorKey"] = twinrovaKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["damage"] = (int)(oldHp - hp);
            event["formId"] = formId;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp != track.lastHp) {
            track.lastHp = hp;
        }

        track.lastStunned = stunned;

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
            Actor* twinrova = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_TW) {
                        twinrova = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (twinrova) break;
            }
            if (twinrova && twinrova->update != nullptr) {
                if (hp < twinrova->colChkInfo.health || hp == 0) {
                    twinrova->colChkInfo.health = hp;
                }
                if (eventType == "BOSS_WEAKPOINT_HIT") {
                    Actor_SetColorFilter(twinrova, 0x4000, 0xFF, 0, 8);
                }
            }
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr || actor->id != ACTOR_BOSS_TW) {
            return {};
        }

        const std::string twinrovaKey = BuildActorKeyLocal(actor, play->sceneNum);
        const uint8_t hp = actor->colChkInfo.health;
        const int formId = (int)actor->params;
        const int phaseNow = ComputePhaseFromTwinrovaForm(formId);

        bool stunned = false;
        if (formId <= kTwinrova) {
            const char* actorPtr = (const char*)actor;
            const uint8_t* stunField = (const uint8_t*)(actorPtr + 0x04F4);
            stunned = (*stunField != 0);
        }

        nlohmann::json snap;
        snap["bossActorKey"] = twinrovaKey;
        snap["bossActorId"] = (int)actor->id;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = phaseNow;
        snap["hp"] = (int)hp;
        snap["formId"] = formId;
        snap["formName"] = (formId == kKoume) ? "koume" : (formId == kKotake) ? "kotake" : "twinrova";
        snap["stunned"] = stunned;
        snap["lateJoinCanSkipIntro"] = true;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const uint8_t hp = (uint8_t)snapshot.value("hp", 1);

        Actor* twinrova = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_TW) {
                    twinrova = actor;
                    break;
                }
                actor = actor->next;
            }
            if (twinrova) break;
        }
        if (!twinrova || !twinrova->update) return;

        twinrova->colChkInfo.health = hp;
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateTwinrovaAdapter() {
    return std::make_shared<TwinrovaAdapter>();
}

} // namespace AnchorBossSync
