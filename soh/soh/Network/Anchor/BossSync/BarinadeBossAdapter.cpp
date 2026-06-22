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

constexpr s16 kBarinadeBodyParam = -1;
constexpr s16 kSupportParamMin = 0;
constexpr s16 kSupportParamMax = 2;
constexpr s16 kZapperParamMin = 3;
constexpr s16 kZapperParamMax = 5;

struct BarinadeTrackState {
    bool initialized = false;
    uint8_t lastHp = 1;
    int lastPhaseId = -1;
    std::unordered_set<std::string> supportKeys;
    std::unordered_set<std::string> zapperKeys;
    std::deque<nlohmann::json> pendingEvents;
};

std::unordered_map<std::string, BarinadeTrackState> gBarinadeTrack;

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

bool IsSupportParam(s16 params) {
    return params >= kSupportParamMin && params <= kSupportParamMax;
}

bool IsZapperParam(s16 params) {
    return params >= kZapperParamMin && params <= kZapperParamMax;
}

int ComputePhase(uint8_t hp, size_t supportCount, size_t zapperCount) {
    if (hp == 0) {
        return 3;
    }
    if (supportCount > 0) {
        return 0;
    }
    if (zapperCount > 0) {
        return 1;
    }
    return 2;
}

void QueueSubactorKillEvents(BarinadeTrackState& track,
                             const std::unordered_set<std::string>& currentSupports,
                             const std::unordered_set<std::string>& currentZappers,
                             const std::string& bodyKey,
                             Actor* bodyActor,
                             PlayState* play) {
    for (const auto& key : track.supportKeys) {
        if (!currentSupports.count(key)) {
            nlohmann::json event;
            event["eventType"] = "BOSS_SUBACTOR_KILL";
            event["eventKey"] = bodyKey;
            event["targetActorKey"] = key;
            event["bossActorKey"] = bodyKey;
            event["bossActorId"] = (int)bodyActor->id;
            event["subState"] = "supportDetached";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            track.pendingEvents.push_back(event);
        }
    }

    for (const auto& key : track.zapperKeys) {
        if (!currentZappers.count(key)) {
            nlohmann::json event;
            event["eventType"] = "BOSS_SUBACTOR_KILL";
            event["eventKey"] = bodyKey;
            event["targetActorKey"] = key;
            event["bossActorKey"] = bodyKey;
            event["bossActorId"] = (int)bodyActor->id;
            event["subState"] = "zapperDetached";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            track.pendingEvents.push_back(event);
        }
    }
}

class BarinadeBossAdapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
        return sceneNum == SCENE_JABU_JABU_BOSS && actorId == ACTOR_BOSS_VA;
    }

    const char* Name() const override {
        return "BarinadeBossAdapter";
    }

    nlohmann::json CaptureTransition(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        // We drive synchronization from the main body only. Subactors are
        // represented as derived events from body-side scans.
        if (actor->params != kBarinadeBodyParam) {
            return {};
        }

        const std::string bodyKey = BuildActorKeyLocal(actor, play->sceneNum);
        BarinadeTrackState& track = gBarinadeTrack[bodyKey];

        std::unordered_set<std::string> currentSupports;
        std::unordered_set<std::string> currentZappers;

        Actor* node = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
        while (node != nullptr) {
            if (node->id == ACTOR_BOSS_VA && (node->room == actor->room || node->room == -1 || actor->room == -1)) {
                const std::string key = BuildActorKeyLocal(node, play->sceneNum);
                if (IsSupportParam(node->params)) {
                    currentSupports.insert(key);
                } else if (IsZapperParam(node->params)) {
                    currentZappers.insert(key);
                }
            }
            node = node->next;
        }

        QueueSubactorKillEvents(track, currentSupports, currentZappers, bodyKey, actor, play);

        const uint8_t hp = actor->colChkInfo.health;
        const int phaseNow = ComputePhase(hp, currentSupports.size(), currentZappers.size());

        if (!track.initialized) {
            track.initialized = true;
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.supportKeys = currentSupports;
            track.zapperKeys = currentZappers;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = bodyKey;
            event["bossActorKey"] = bodyKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["subState"] = "barinadeBody";
            event["supportAlive"] = (int)currentSupports.size();
            event["zapperAlive"] = (int)currentZappers.size();
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp == 0 && track.lastHp != 0) {
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.supportKeys = currentSupports;
            track.zapperKeys = currentZappers;

            nlohmann::json event;
            event["eventType"] = "BOSS_DEATH_COMMIT";
            event["eventKey"] = bodyKey;
            event["bossActorKey"] = bodyKey;
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
            track.supportKeys = currentSupports;
            track.zapperKeys = currentZappers;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = bodyKey;
            event["bossActorKey"] = bodyKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["supportAlive"] = (int)currentSupports.size();
            event["zapperAlive"] = (int)currentZappers.size();
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp < track.lastHp) {
            const uint8_t oldHp = track.lastHp;
            track.lastHp = hp;
            track.supportKeys = currentSupports;
            track.zapperKeys = currentZappers;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_HIT";
            event["eventKey"] = bodyKey;
            event["bossActorKey"] = bodyKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["damage"] = (int)(oldHp - hp);
            event["supportAlive"] = (int)currentSupports.size();
            event["zapperAlive"] = (int)currentZappers.size();
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp != track.lastHp) {
            track.lastHp = hp;
        }

        track.supportKeys = currentSupports;
        track.zapperKeys = currentZappers;

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
            Actor* barinade = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_VA) {
                        barinade = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (barinade) break;
            }
            if (barinade && barinade->update != nullptr) {
                // Only lower HP, never raise — prevents stale events from healing boss.
                if (hp < barinade->colChkInfo.health || hp == 0) {
                    barinade->colChkInfo.health = hp;
                }
                if (eventType == "BOSS_WEAKPOINT_HIT") {
                    // Visual hit feedback so non-master players see boss react to damage.
                    Actor_SetColorFilter(barinade, 0x4000, 0xFF, 0, 8);
                }
            }
        } else if (eventType == "BOSS_SUBACTOR_KILL") {
            Actor* subactor = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == targetActorKey && actor->id == ACTOR_BOSS_VA) {
                        subactor = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (subactor) break;
            }
            if (subactor && subactor->update != nullptr) {
                subactor->colChkInfo.health = 0;
            }
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr || actor->params != kBarinadeBodyParam) {
            return {};
        }

        const std::string bodyKey = BuildActorKeyLocal(actor, play->sceneNum);

        int supportCount = 0;
        int zapperCount = 0;
        Actor* node = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
        while (node != nullptr) {
            if (node->id == ACTOR_BOSS_VA && (node->room == actor->room || node->room == -1 || actor->room == -1)) {
                if (IsSupportParam(node->params)) {
                    supportCount++;
                } else if (IsZapperParam(node->params)) {
                    zapperCount++;
                }
            }
            node = node->next;
        }

        const uint8_t hp = actor->colChkInfo.health;

        nlohmann::json snap;
        snap["bossActorKey"] = bodyKey;
        snap["bossActorId"] = (int)actor->id;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = ComputePhase(hp, (size_t)supportCount, (size_t)zapperCount);
        snap["hp"] = (int)hp;
        snap["supportAlive"] = supportCount;
        snap["zapperAlive"] = zapperCount;
        snap["lateJoinCanSkipIntro"] = true;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const uint8_t hp = (uint8_t)snapshot.value("hp", 1);

        Actor* barinade = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_VA) {
                    barinade = actor;
                    break;
                }
                actor = actor->next;
            }
            if (barinade) break;
        }
        if (!barinade || !barinade->update) return;

        // Snapshot is authoritative initial state for late-joiners: set absolute HP.
        barinade->colChkInfo.health = hp;
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateBarinadeBossAdapter() {
    return std::make_shared<BarinadeBossAdapter>();
}

} // namespace AnchorBossSync
