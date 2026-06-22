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

struct Ganon2TrackState {
    bool initialized = false;
    float lastHpNorm = 1.0f;
    int lastPhaseId = -1;
    std::deque<nlohmann::json> pendingEvents;
};

std::unordered_map<std::string, Ganon2TrackState> gGanon2Track;

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

int ComputePhaseFromGanon2Health(float healthNorm) {
    if (healthNorm <= 0.0f) {
        return 3;
    }
    if (healthNorm <= 0.33f) {
        return 2;
    }
    if (healthNorm <= 0.66f) {
        return 1;
    }
    return 0;
}

class Ganon2Adapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
        return sceneNum == SCENE_GANON_BOSS && actorId == ACTOR_BOSS_GANON2;
    }

    const char* Name() const override {
        return "Ganon2Adapter";
    }

    nlohmann::json CaptureTransition(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        if (actor->id != ACTOR_BOSS_GANON2) {
            return {};
        }

        const std::string ganon2Key = BuildActorKeyLocal(actor, play->sceneNum);
        Ganon2TrackState& track = gGanon2Track[ganon2Key];

        const char* actorPtr = (const char*)actor;
        const float* healthField = (const float*)(actorPtr + 0x324);
        const float healthNorm = (*healthField > 1.0f) ? 1.0f : (*healthField < 0.0f) ? 0.0f : *healthField;
        const int phaseNow = ComputePhaseFromGanon2Health(healthNorm);

        if (!track.initialized) {
            track.initialized = true;
            track.lastHpNorm = healthNorm;
            track.lastPhaseId = phaseNow;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = ganon2Key;
            event["bossActorKey"] = ganon2Key;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["healthNormalized"] = healthNorm;
            event["healthPercent"] = (int)(healthNorm * 100.0f);
            event["subState"] = "ganon2_intro";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (healthNorm == 0.0f && track.lastHpNorm > 0.0f) {
            track.lastHpNorm = healthNorm;
            track.lastPhaseId = phaseNow;

            nlohmann::json event;
            event["eventType"] = "BOSS_DEATH_COMMIT";
            event["eventKey"] = ganon2Key;
            event["bossActorKey"] = ganon2Key;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = 3;
            event["healthNormalized"] = 0.0f;
            event["healthPercent"] = 0;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        const int newPhase = ComputePhaseFromGanon2Health(healthNorm);
        if (newPhase != track.lastPhaseId) {
            track.lastPhaseId = newPhase;
            track.lastHpNorm = healthNorm;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = ganon2Key;
            event["bossActorKey"] = ganon2Key;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = newPhase;
            event["healthNormalized"] = healthNorm;
            event["healthPercent"] = (int)(healthNorm * 100.0f);
            event["subState"] = "phase_transition";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (healthNorm < track.lastHpNorm) {
            const float oldHpNorm = track.lastHpNorm;
            track.lastHpNorm = healthNorm;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_HIT";
            event["eventKey"] = ganon2Key;
            event["bossActorKey"] = ganon2Key;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["healthNormalized"] = healthNorm;
            event["healthPercent"] = (int)(healthNorm * 100.0f);
            event["damageNormalized"] = (oldHpNorm - healthNorm);
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (healthNorm != track.lastHpNorm) {
            track.lastHpNorm = healthNorm;
        }

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
        const float healthNorm = payload.value("healthNormalized", 1.0f);

        if (eventType == "BOSS_WEAKPOINT_HIT" || eventType == "BOSS_STAGE_ENTER") {
            Actor* ganon2 = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_GANON2) {
                        ganon2 = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (ganon2) break;
            }
            if (ganon2 && ganon2->update != nullptr) {
                char* actorPtr = (char*)ganon2;
                float* healthField = (float*)(actorPtr + 0x324);
                const float current = *healthField;
                // Only lower health, never raise — prevents stale events from healing.
                if (healthNorm < current || healthNorm == 0.0f) {
                    *healthField = (healthNorm > 1.0f) ? 1.0f : (healthNorm < 0.0f) ? 0.0f : healthNorm;
                }
                if (eventType == "BOSS_WEAKPOINT_HIT") {
                    Actor_SetColorFilter(ganon2, 0x4000, 0xFF, 0, 8);
                }
            }
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr || actor->id != ACTOR_BOSS_GANON2) {
            return {};
        }

        const std::string ganon2Key = BuildActorKeyLocal(actor, play->sceneNum);
        const char* actorPtr = (const char*)actor;
        const float* healthField = (const float*)(actorPtr + 0x324);
        const float healthNorm = (*healthField > 1.0f) ? 1.0f : (*healthField < 0.0f) ? 0.0f : *healthField;
        const int phaseNow = ComputePhaseFromGanon2Health(healthNorm);

        nlohmann::json snap;
        snap["bossActorKey"] = ganon2Key;
        snap["bossActorId"] = (int)actor->id;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = phaseNow;
        snap["healthNormalized"] = healthNorm;
        snap["healthPercent"] = (int)(healthNorm * 100.0f);
        snap["lateJoinCanSkipIntro"] = true;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const float healthNorm = snapshot.value("healthNormalized", 1.0f);

        Actor* ganon2 = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_GANON2) {
                    ganon2 = actor;
                    break;
                }
                actor = actor->next;
            }
            if (ganon2) break;
        }
        if (!ganon2 || !ganon2->update) return;

        char* actorPtr = (char*)ganon2;
        float* healthField = (float*)(actorPtr + 0x324);
        *healthField = (healthNorm > 1.0f) ? 1.0f : (healthNorm < 0.0f) ? 0.0f : healthNorm;
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateGanon2Adapter() {
    return std::make_shared<Ganon2Adapter>();
}

} // namespace AnchorBossSync
