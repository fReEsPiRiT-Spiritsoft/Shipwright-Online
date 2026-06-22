#include "soh/Network/Anchor/BossSync/BossSyncRegistry.h"

#include <algorithm>
#include <unordered_map>

extern "C" {
#include "variables.h"
#include "functions.h"
}

namespace AnchorBossSync {

namespace {

struct BossTrackState {
    bool initialized = false;
    uint8_t initialHp = 1;
    uint8_t lastHp = 1;
    int lastPhaseId = -1;
};

std::unordered_map<std::string, BossTrackState> gBossTrack;

bool IsMainBossActor(s16 actorId) {
    switch (actorId) {
        case ACTOR_BOSS_GOMA:
        case ACTOR_BOSS_DODONGO:
        case ACTOR_BOSS_VA:
        case ACTOR_BOSS_FD:
        case ACTOR_BOSS_FD2:
        case ACTOR_BOSS_MO:
        case ACTOR_BOSS_TW:
        case ACTOR_BOSS_SST:
        case ACTOR_BOSS_GANONDROF:
        case ACTOR_BOSS_GANON:
            return true;
        default:
            return false;
    }
}

bool IsMainBossScene(s16 sceneNum) {
    switch (sceneNum) {
        case SCENE_DEKU_TREE_BOSS:
        case SCENE_DODONGOS_CAVERN_BOSS:
        case SCENE_JABU_JABU_BOSS:
        case SCENE_FOREST_TEMPLE_BOSS:
        case SCENE_FIRE_TEMPLE_BOSS:
        case SCENE_WATER_TEMPLE_BOSS:
        case SCENE_SPIRIT_TEMPLE_BOSS:
        case SCENE_SHADOW_TEMPLE_BOSS:
        case SCENE_GANONDORF_BOSS:
        case SCENE_GANON_BOSS:
            return true;
        default:
            return false;
    }
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

int ComputePhaseByHp(uint8_t hp, uint8_t initialHp) {
    if (initialHp <= 1) {
        return hp == 0 ? 3 : 0;
    }

    float ratio = (float)hp / (float)std::max<uint8_t>(initialHp, 1);
    if (hp == 0) return 3;
    if (ratio <= 0.33f) return 2;
    if (ratio <= 0.66f) return 1;
    return 0;
}

class GenericBossHealthPhaseAdapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
                return IsMainBossScene(sceneNum) && IsMainBossActor(actorId);
    }

    const char* Name() const override {
        return "GenericBossHealthPhaseAdapter";
    }

    nlohmann::json CaptureTransition(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        const std::string bossActorKey = BuildActorKeyLocal(actor, play->sceneNum);
        BossTrackState& track = gBossTrack[bossActorKey];

        const uint8_t hp = actor->colChkInfo.health;
        if (!track.initialized) {
            track.initialized = true;
            track.initialHp = std::max<uint8_t>(hp, 1);
            track.lastHp = hp;
            track.lastPhaseId = ComputePhaseByHp(hp, track.initialHp);

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = bossActorKey;
            event["bossActorKey"] = bossActorKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = track.lastPhaseId;
            event["hp"] = (int)hp;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        const int phaseNow = ComputePhaseByHp(hp, track.initialHp);

        if (hp == 0 && track.lastHp != 0) {
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;

            nlohmann::json event;
            event["eventType"] = "BOSS_DEATH_COMMIT";
            event["eventKey"] = bossActorKey;
            event["bossActorKey"] = bossActorKey;
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

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = bossActorKey;
            event["bossActorKey"] = bossActorKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp < track.lastHp) {
            const uint8_t oldHp = track.lastHp;
            track.lastHp = hp;

            nlohmann::json event;
            event["eventType"] = "BOSS_WEAKPOINT_HIT";
            event["eventKey"] = bossActorKey;
            event["bossActorKey"] = bossActorKey;
            event["bossActorId"] = (int)actor->id;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["damage"] = (int)(oldHp - hp);
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp != track.lastHp) {
            track.lastHp = hp;
        }

        return {};
    }

    void ApplyEvent(PlayState* play, const nlohmann::json& payload) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string eventType    = payload.value("eventType", std::string(""));
        const std::string bossActorKey = payload.value("bossActorKey", std::string(""));
        const uint8_t     hp           = (uint8_t)payload.value("hp", 1);
        const s16         bossActorId  = (s16)payload.value("bossActorId", (int)-1);
        if (bossActorKey.empty() || bossActorId < 0) return;

        Actor* boss = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (actor->id == bossActorId &&
                    BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey) {
                    boss = actor;
                    break;
                }
                actor = actor->next;
            }
            if (boss) break;
        }
        if (!boss || !boss->update) return;

        if (eventType == "BOSS_WEAKPOINT_HIT" || eventType == "BOSS_STAGE_ENTER") {
            // Only apply downward HP changes — stale events must never heal the boss.
            if (hp < boss->colChkInfo.health || hp == 0) {
                boss->colChkInfo.health = hp;
            }
            if (eventType == "BOSS_WEAKPOINT_HIT") {
                Actor_SetColorFilter(boss, 0x4000, 0xFF, 0, 8);
            }
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr) {
            return {};
        }

        const std::string bossActorKey = BuildActorKeyLocal(actor, play->sceneNum);
        const uint8_t hp = actor->colChkInfo.health;

        nlohmann::json snap;
        snap["bossActorKey"] = bossActorKey;
        snap["bossActorId"] = (int)actor->id;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = ComputePhaseByHp(hp, gBossTrack.count(bossActorKey) ? gBossTrack[bossActorKey].initialHp : std::max<uint8_t>(hp, 1));
        snap["hp"] = (int)hp;
        snap["lateJoinCanSkipIntro"] = true;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const s16         bossActorId  = (s16)snapshot.value("bossActorId", (int)-1);
        const uint8_t     hp           = (uint8_t)snapshot.value("hp", 1);
        if (bossActorKey.empty() || bossActorId < 0) return;

        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (actor->id == bossActorId &&
                    BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey &&
                    actor->update != nullptr) {
                    // Snapshot sets absolute HP — this is the authoritative initial state
                    // for the late joiner, so we apply regardless of direction.
                    actor->colChkInfo.health = hp;
                    return;
                }
                actor = actor->next;
            }
        }
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreateGenericBossHealthPhaseAdapter() {
    return std::make_shared<GenericBossHealthPhaseAdapter>();
}

} // namespace AnchorBossSync
