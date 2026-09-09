#include "soh/Network/Anchor/BossSync/BossSyncRegistry.h"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>

extern "C" {
#include "variables.h"
#include "functions.h"
}

// BossGanondrof (Phantom Ganon) spawns one real boss (params == GND_REAL_BOSS)
// plus several decoy paintings (params >= GND_FAKE_BOSS) during the intro —
// only the real instance carries HP/state, so decoys are ignored entirely.
//
// The main fight loop is BossGanondrof_Neutral, which stays the actionFunc for
// the whole volley fight and internally switches on `flyMode`
// (NEUTRAL/VOLLEY/RETURN/CHARGE); it only hands off to the short-lived
// BossGanondrof_Throw/Return actionFuncs before returning to Neutral. This
// mirrors BossFd_Fly/BossMo_Core, so forcing flyMode + work[GND_ACTION_STATE]
// + the volley timer + targetPos directly is safe (verified against
// z_boss_ganondrof.c/.h) — every flyMode switch is decided by each client's own
// Rand_ZeroOne(), so without this a client's spear-throw pattern and volley
// target silently diverge from what the master (and thus real damage/hits)
// is doing.
//
// work[GND_ACTION_STATE] is intentionally NOT forced by itself: its meaning is
// re-purposed per actionFunc (THROW_SLOW/NORMAL under Throw, STUNNED_FALL/
// GROUND under Stunned, CHARGE_* under Charge, DEATH_* under Death) — same
// caveat as Gohma. We only forward it as part of the flyMode snapshot, and
// only while the master itself is inside BossGanondrof_Neutral so the
// receiving client's own Neutral loop can safely reuse it.
namespace AnchorBossSync {

namespace {

constexpr s16 kRealBossParam = 1; // GND_REAL_BOSS

constexpr size_t kFlyModeOffset      = 0x01C9; // u8 flyMode
constexpr size_t kActionStateOffset  = 0x01A2; // work[GND_ACTION_STATE] (index 7)
constexpr size_t kTimer0Offset       = 0x01BC; // timers[0]
constexpr size_t kTargetPosOffset    = 0x020C; // Vec3f targetPos

u8 ReadGndFlyMode(const Actor* actor) {
    return *(const u8*)((const char*)actor + kFlyModeOffset);
}

void WriteGndFlyMode(Actor* actor, u8 value) {
    *(u8*)((char*)actor + kFlyModeOffset) = value;
}

int16_t ReadGndActionState(const Actor* actor) {
    return *(const int16_t*)((const char*)actor + kActionStateOffset);
}

void WriteGndActionState(Actor* actor, int16_t value) {
    *(int16_t*)((char*)actor + kActionStateOffset) = value;
}

int16_t ReadGndTimer0(const Actor* actor) {
    return *(const int16_t*)((const char*)actor + kTimer0Offset);
}

void WriteGndTimer0(Actor* actor, int16_t value) {
    *(int16_t*)((char*)actor + kTimer0Offset) = value;
}

Vec3f ReadGndTargetPos(const Actor* actor) {
    return *(const Vec3f*)((const char*)actor + kTargetPosOffset);
}

void WriteGndTargetPos(Actor* actor, const Vec3f& value) {
    *(Vec3f*)((char*)actor + kTargetPosOffset) = value;
}

struct PhantomGanonTrackState {
    bool initialized = false;
    uint8_t lastHp = 1;
    int lastPhaseId = -1;
    int lastFlyMode = -1;
    std::deque<nlohmann::json> pendingEvents;
};

std::unordered_map<std::string, PhantomGanonTrackState> gPhantomGanonTrack;

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

// Matches the real threshold the boss code itself checks (colChkInfo.health < 5)
// to unlock the charge-lunge attack, instead of a generic HP-ratio split.
int ComputePhase(uint8_t hp) {
    if (hp == 0) return 3;
    if (hp < 5) return 2;
    if (hp < 15) return 1;
    return 0;
}

class PhantomGanonAdapter : public BossSyncAdapter {
  public:
    bool CanHandle(s16 sceneNum, s16 actorId) const override {
        return sceneNum == SCENE_FOREST_TEMPLE_BOSS && actorId == ACTOR_BOSS_GANONDROF;
    }

    const char* Name() const override {
        return "PhantomGanonAdapter";
    }

    void Reset() override {
        gPhantomGanonTrack.clear();
    }

    nlohmann::json CaptureTransition(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr || actor->params != kRealBossParam) {
            return {};
        }

        const std::string bossActorKey = BuildActorKeyLocal(actor, play->sceneNum);
        PhantomGanonTrackState& track = gPhantomGanonTrack[bossActorKey];

        const uint8_t hp = ReadClampedBossHealth(actor);
        const int flyMode = (int)ReadGndFlyMode(actor);
        const int phaseNow = ComputePhase(hp);

        // Volley/attack-mode sync: captured before the early-return branches so a
        // flyMode change landing on the same frame as an HP/phase change is
        // queued instead of dropped.
        if (track.initialized && flyMode != track.lastFlyMode) {
            nlohmann::json actionEvent;
            actionEvent["eventType"] = "BOSS_ACTION_STATE";
            actionEvent["eventKey"] = bossActorKey;
            actionEvent["bossActorKey"] = bossActorKey;
            actionEvent["bossActorId"] = (int)ACTOR_BOSS_GANONDROF;
            actionEvent["flyMode"] = flyMode;
            actionEvent["actionState"] = (int)ReadGndActionState(actor);
            actionEvent["timer0"] = (int)ReadGndTimer0(actor);
            const Vec3f targetPos = ReadGndTargetPos(actor);
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
            track.lastFlyMode = flyMode;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = bossActorKey;
            event["bossActorKey"] = bossActorKey;
            event["bossActorId"] = (int)ACTOR_BOSS_GANONDROF;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["flyMode"] = flyMode;
            event["seq"] = (uint32_t)play->state.frames;
            event["masterFrame"] = (uint32_t)play->state.frames;
            event["lateJoinCanSkipIntro"] = true;
            return event;
        }

        if (hp == 0 && track.lastHp != 0) {
            track.lastHp = hp;
            track.lastPhaseId = phaseNow;
            track.lastFlyMode = flyMode;

            nlohmann::json event;
            event["eventType"] = "BOSS_DEATH_COMMIT";
            event["eventKey"] = bossActorKey;
            event["bossActorKey"] = bossActorKey;
            event["bossActorId"] = (int)ACTOR_BOSS_GANONDROF;
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
            track.lastFlyMode = flyMode;

            nlohmann::json event;
            event["eventType"] = "BOSS_STAGE_ENTER";
            event["eventKey"] = bossActorKey;
            event["bossActorKey"] = bossActorKey;
            event["bossActorId"] = (int)ACTOR_BOSS_GANONDROF;
            event["phaseId"] = phaseNow;
            event["hp"] = (int)hp;
            event["flyMode"] = flyMode;
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
            event["bossActorId"] = (int)ACTOR_BOSS_GANONDROF;
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

        // Always advance, even when no other branch fired this frame — otherwise
        // the flyMode detector above would re-queue duplicate events forever.
        track.lastFlyMode = flyMode;

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

        Actor* phantomGanon = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (actor->id == ACTOR_BOSS_GANONDROF && actor->params == kRealBossParam &&
                    BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey) {
                    phantomGanon = actor;
                    break;
                }
                actor = actor->next;
            }
            if (phantomGanon) break;
        }
        if (!phantomGanon || phantomGanon->update == nullptr) return;

        if (eventType == "BOSS_WEAKPOINT_HIT" || eventType == "BOSS_STAGE_ENTER") {
            if (hp < phantomGanon->colChkInfo.health || hp == 0) {
                phantomGanon->colChkInfo.health = hp;
            }
            if (eventType == "BOSS_WEAKPOINT_HIT") {
                Actor_SetColorFilter(phantomGanon, 0x4000, 0xFF, 0, 8);
            }
        } else if (eventType == "BOSS_ACTION_STATE") {
            const u8 flyMode = (u8)payload.value("flyMode", (int)0);
            const int16_t actionState = (int16_t)payload.value("actionState", (int)0);
            const int16_t timer0 = (int16_t)payload.value("timer0", (int)0);
            const Vec3f targetPos = {
                payload.value("targetX", phantomGanon->world.pos.x),
                payload.value("targetY", phantomGanon->world.pos.y),
                payload.value("targetZ", phantomGanon->world.pos.z),
            };

            // Force the SAME volley/return/charge decision the master already
            // made — BossGanondrof_Neutral keeps running locally and reacts to
            // these corrected fields on its very next tick.
            WriteGndFlyMode(phantomGanon, flyMode);
            WriteGndActionState(phantomGanon, actionState);
            WriteGndTimer0(phantomGanon, timer0);
            WriteGndTargetPos(phantomGanon, targetPos);
        }
    }

    nlohmann::json BuildSnapshot(PlayState* play, Actor* actor) override {
        if (play == nullptr || actor == nullptr || actor->params != kRealBossParam) {
            return {};
        }

        const std::string bossActorKey = BuildActorKeyLocal(actor, play->sceneNum);
        const uint8_t hp = ReadClampedBossHealth(actor);
        const int flyMode = (int)ReadGndFlyMode(actor);

        nlohmann::json snap;
        snap["bossActorKey"] = bossActorKey;
        snap["bossActorId"] = (int)ACTOR_BOSS_GANONDROF;
        snap["sceneNum"] = play->sceneNum;
        snap["roomNum"] = (int)play->roomCtx.curRoom.num;
        snap["phaseId"] = ComputePhase(hp);
        snap["hp"] = (int)hp;
        snap["flyMode"] = flyMode;
        snap["actionState"] = (int)ReadGndActionState(actor);
        snap["timer0"] = (int)ReadGndTimer0(actor);
        const Vec3f targetPos = ReadGndTargetPos(actor);
        snap["targetX"] = targetPos.x;
        snap["targetY"] = targetPos.y;
        snap["targetZ"] = targetPos.z;
        snap["lateJoinCanSkipIntro"] = true;
        return snap;
    }

    void ApplySnapshot(PlayState* play, const nlohmann::json& snapshot) override {
        if (!IsPlaySessionActive(play)) return;

        const std::string bossActorKey = snapshot.value("bossActorKey", std::string(""));
        const uint8_t hp = (uint8_t)snapshot.value("hp", 1);

        Actor* phantomGanon = nullptr;
        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = play->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (actor->id == ACTOR_BOSS_GANONDROF && actor->params == kRealBossParam &&
                    BuildActorKeyLocal(actor, play->sceneNum) == bossActorKey) {
                    phantomGanon = actor;
                    break;
                }
                actor = actor->next;
            }
            if (phantomGanon) break;
        }
        if (!phantomGanon || !phantomGanon->update) return;

        phantomGanon->colChkInfo.health = hp;

        const u8 flyMode = (u8)snapshot.value("flyMode", (int)0);
        const int16_t actionState = (int16_t)snapshot.value("actionState", (int)0);
        const int16_t timer0 = (int16_t)snapshot.value("timer0", (int)0);
        const Vec3f targetPos = {
            snapshot.value("targetX", phantomGanon->world.pos.x),
            snapshot.value("targetY", phantomGanon->world.pos.y),
            snapshot.value("targetZ", phantomGanon->world.pos.z),
        };
        WriteGndFlyMode(phantomGanon, flyMode);
        WriteGndActionState(phantomGanon, actionState);
        WriteGndTimer0(phantomGanon, timer0);
        WriteGndTargetPos(phantomGanon, targetPos);
    }
};

} // namespace

std::shared_ptr<BossSyncAdapter> CreatePhantomGanonAdapter() {
    return std::make_shared<PhantomGanonAdapter>();
}

} // namespace AnchorBossSync
