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

constexpr s16 kBongoHeadParam = -1;
constexpr s16 kBongoLeftHandParam = 0;
constexpr s16 kBongoRightHandParam = 1;

struct BongoTrackState {
    bool initialized = false;
    uint8_t lastHp = 1;
    int lastPhaseId = -1;
    int lastHandsAlive = 2;
    bool lastLeftHandAlive = true;
    bool lastRightHandAlive = true;
    std::unordered_set<std::string> handKeys;
    std::deque<nlohmann::json> pendingEvents;
};

std::unordered_map<std::string, BongoTrackState> gBongoTrack;

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

int ComputePhaseFromHandState(int handsAlive, int hp) {
    if (hp == 0) {
        return 3;
    }
    if (handsAlive == 0) {
        return 2;
    }
    if (handsAlive == 1) {
        return 1;
    }
    return 0;
}

void QueueHandLossEvents(BongoTrackState& track,
                         const std::unordered_set<std::string>& currentHands,
                         const std::string& headKey,
                         Actor* headActor,
                         PlayState* play) {
    for (const auto& key : track.handKeys) {
        if (!currentHands.count(key)) {
            nlohmann::json event;
            event["eventType"] = "BOSS_SUBACTOR_KILL";
            event["eventKey"] = headKey;
            event["targetActorKey"] = key;
            event["bossActorKey"] = headKey;
            event["bossActorId"] = (int)headActor->id;
            event["subState"] = "handDestroyed";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            track.pendingEvents.push_back(event);
        }
    }
}

class BongoBongoAdapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
        return sceneNum == SCENE_SHADOW_TEMPLE_BOSS && actorId == ACTOR_BOSS_SST;
    }

    const char* Name() const override {
        return "BongoBongoAdapter";
    }

    void Reset() override {
        gBongoTrack.clear();
    }

    nlohmann::json CaptureTransition(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        if (actor->params != kBongoHeadParam) {
            return {};
        }

        const std::string headKey = BuildActorKeyLocal(actor, play->sceneNum);
        BongoTrackState& track = gBongoTrack[headKey];

        std::unordered_set<std::string> currentHands;
        int handsAlive = 0;
        bool leftHandAlive = false;
        bool rightHandAlive = false;

        Actor* node = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
        while (node != nullptr) {
            if (node->id == ACTOR_BOSS_SST && (node->room == actor->room || node->room == -1 || actor->room == -1)) {
                if (node->params == kBongoLeftHandParam) {
                    if (node->colChkInfo.health > 0) {
                        leftHandAlive = true;
                        handsAlive++;
                        const std::string key = BuildActorKeyLocal(node, play->sceneNum);
                        currentHands.insert(key);
                    }
                } else if (node->params == kBongoRightHandParam) {
                    if (node->colChkInfo.health > 0) {
                        rightHandAlive = true;
                        handsAlive++;
                        const std::string key = BuildActorKeyLocal(node, play->sceneNum);
                        currentHands.insert(key);
                    }
                }
            }
            node = node->next;
        }

        QueueHandLossEvents(track, currentHands, headKey, actor, play);

        const uint8_t hp = ReadClampedBossHealth(actor);
        const int phaseNow = ComputePhaseFromHandState(handsAlive, (int)hp);

        if (!track.initialized) {
            track.initialized = true;
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastHandsAlive = handsAlive;
            track.lastLeftHandAlive = leftHandAlive;
            track.lastRightHandAlive = rightHandAlive;
            track.handKeys = currentHands;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = headKey;
            event["bossActorKey"] = headKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["handsAlive"] = handsAlive;
            event["subState"] = "bongoHead";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp == 0 && track.lastHp != 0) {
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastHandsAlive = handsAlive;
            track.lastLeftHandAlive = leftHandAlive;
            track.lastRightHandAlive = rightHandAlive;
            track.handKeys = currentHands;

            nlohmann::json event;
            event["eventType"] = "BOSS_DEATH_COMMIT";
            event["eventKey"] = headKey;
            event["bossActorKey"] = headKey;
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
            track.lastHandsAlive = handsAlive;
            track.lastLeftHandAlive = leftHandAlive;
            track.lastRightHandAlive = rightHandAlive;
            track.handKeys = currentHands;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = headKey;
            event["bossActorKey"] = headKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["handsAlive"] = handsAlive;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (handsAlive != track.lastHandsAlive) {
            track.lastHandsAlive = handsAlive;
            track.lastLeftHandAlive = leftHandAlive;
            track.lastRightHandAlive = rightHandAlive;
            track.handKeys = currentHands;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_DESTROY";
            event["eventKey"] = headKey;
            event["bossActorKey"] = headKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["handsAlive"] = handsAlive;
            event["subState"] = "handDestroyed";
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp < track.lastHp) {
            const uint8_t oldHp = track.lastHp;
            track.lastHp = hp;
            track.handKeys = currentHands;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_HIT";
            event["eventKey"] = headKey;
            event["bossActorKey"] = headKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["damage"] = (int)(oldHp - hp);
            event["handsAlive"] = handsAlive;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp != track.lastHp) {
            track.lastHp = hp;
        }

        track.handKeys = currentHands;

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
            Actor* bongo = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_SST) {
                        bongo = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (bongo) break;
            }
            if (bongo && bongo->update != nullptr) {
                if (hp < bongo->colChkInfo.health || hp == 0) {
                    bongo->colChkInfo.health = hp;
                }
                if (eventType == "BOSS_WEAKPOINT_HIT") {
                    Actor_SetColorFilter(bongo, 0x4000, 0xFF, 0, 8);
                }
            }
        } else if (eventType == "BOSS_SUBACTOR_KILL") {
            Actor* hand = nullptr;
            for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
                Actor* actor = play->actorCtx.actorLists[cat].head;
                while (actor != nullptr) {
                    if (BuildActorKeyLocal(actor, play->sceneNum) == targetActorKey && actor->id == ACTOR_BOSS_SST) {
                        hand = actor;
                        break;
                    }
                    actor = actor->next;
                }
                if (hand) break;
            }
            if (hand && hand->update != nullptr) {
                hand->colChkInfo.health = 0;
            }
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr || actor->params != kBongoHeadParam) {
            return {};
        }

        const std::string headKey = BuildActorKeyLocal(actor, play->sceneNum);

        int handsAlive = 0;
        Actor* node = play->actorCtx.actorLists[ACTORCAT_BOSS].head;
        while (node != nullptr) {
            if (node->id == ACTOR_BOSS_SST && (node->room == actor->room || node->room == -1 || actor->room == -1)) {
                if ((node->params == kBongoLeftHandParam || node->params == kBongoRightHandParam) &&
                    node->colChkInfo.health > 0) {
                    handsAlive++;
                }
            }
            node = node->next;
        }

        const uint8_t hp = ReadClampedBossHealth(actor);

        nlohmann::json snap;
        snap["bossActorKey"] = headKey;
        snap["bossActorId"] = (int)actor->id;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = ComputePhaseFromHandState(handsAlive, (int)hp);
        snap["hp"] = (int)hp;
        snap["handsAlive"] = handsAlive;
        snap["lateJoinCanSkipIntro"] = true;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const uint8_t hp = (uint8_t)snapshot.value("hp", 1);

        Actor* bongo = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey && actor->id == ACTOR_BOSS_SST) {
                    bongo = actor;
                    break;
                }
                actor = actor->next;
            }
            if (bongo) break;
        }
        if (!bongo || !bongo->update) return;

        bongo->colChkInfo.health = hp;
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateBongoBongoAdapter() {
    return std::make_shared<BongoBongoAdapter>();
}

} // namespace AnchorBossSync
