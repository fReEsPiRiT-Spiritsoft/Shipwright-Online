#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/BossSync/BossSyncDispatch.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
#include "src/overlays/actors/ovl_En_Ru1/z_en_ru1.h"
#include "src/overlays/actors/ovl_Bg_Ydan_Sp/z_bg_ydan_sp.h"
extern PlayState* gPlayState;
}

namespace {
bool IsBossSyncEventType(const std::string& eventType) {
    return eventType.rfind("BOSS_", 0) == 0;
}
}

/**
 * ROOM_EVENT
 *
 * Phase 6a — Generic idempotent room-event channel.
 *
 * Purpose:
 *   Broadcasts one-shot in-room events that don't map to a save-context flag
 *   (minigame starts, boss phase transitions, score updates, etc.).
 *
 * Idempotency:
 *   Each event is identified by the triple
 *       sceneNum + "_" + roomNum + "_" + eventType + "_" + eventKey
 *   which is stored in `processedRoomEvents` on the receiving client.
 *   Duplicate deliveries (retransmits, late joiners) are silently dropped.
 *   The set is cleared on every OnSceneInit() so events are fresh each room visit.
 *
 * Event types:
 *   "MINIGAME_START"   — a minigame has begun; eventData: { "minigameId": int }
 *   "MINIGAME_SCORE"   — current live score; eventData: { "minigameId": int, "score": int }
 *   "MINIGAME_END"     — minigame finished; eventData: { "minigameId": int, "finalScore": int }
 *   (further types added per Phase 6 special actors)
 *
 * Authority:
 *   Any client may send ROOM_EVENT, but in practice the Room Master is the
 *   authoritative sender for score/state changes.  Clients that receive an
 *   event echo apply it exactly once.
 *
 * Payload structure:
 *   {
 *     "type":      "ROOM_EVENT",
 *     "clientId":  <sender>,          // auto-injected by SendJsonToRemote
 *     "sceneNum":  <int>,
 *     "roomNum":   <int>,
 *     "eventType": <string>,          // "MINIGAME_START", "MINIGAME_SCORE", ...
 *     "eventKey":  <string>,          // stable per-instance key within room
 *     "eventData": { ... }            // optional, type-specific payload
 *   }
 */

// ─────────────────────────────────────────────────────────────────────────────
// Send
// ─────────────────────────────────────────────────────────────────────────────

void Anchor::SendPacket_RoomEvent(const std::string& eventType,
                                  const std::string& eventKey,
                                  const nlohmann::json& eventData,
                                  bool streaming) {
    if (!IsSaveLoaded() || !gPlayState) return;

    const bool isBossEvent = IsBossSyncEventType(eventType);
    if (isBossEvent && !IsRoomMaster()) {
        return;
    }

    nlohmann::json payload;
    payload["type"]      = ROOM_EVENT;
    payload["sceneNum"]  = gPlayState->sceneNum;
    payload["roomNum"]   = gPlayState->roomCtx.curRoom.num;
    payload["eventType"] = eventType;
    payload["eventKey"]  = eventKey;
    if (!eventData.empty()) {
        payload["eventData"] = eventData;
    }
    if (isBossEvent) {
        // Boss events are ordered via monotonic sequence. If caller did not
        // supply one, use current frame as stable local fallback.
        payload["seq"] = eventData.value("seq", (uint32_t)gPlayState->state.frames);
        payload["masterFrame"] = eventData.value("masterFrame", (uint32_t)gPlayState->state.frames);
    }
    if (streaming) {
        // streaming=true: receiver skips idempotency check so every update is applied.
        // Also sets quiet=true to suppress debug spam for high-frequency events.
        payload["streaming"] = true;
        payload["quiet"]     = true;
    }

    SPDLOG_INFO("[Anchor:EventSync] SEND {} | key={} | scene=0x{:02x} room={}",
                eventType, eventKey, (int)gPlayState->sceneNum,
                (int)gPlayState->roomCtx.curRoom.num);

    SendJsonToRemote(payload);
}

// ─────────────────────────────────────────────────────────────────────────────
// Handle
// ─────────────────────────────────────────────────────────────────────────────

void Anchor::HandlePacket_RoomEvent(nlohmann::json payload) {
    if (!IsSaveLoaded() || !gPlayState) return;

    const s16         packetScene   = payload.value("sceneNum",  (int)-1);
    const int         packetRoom    = payload.value("roomNum",   -1);
    const std::string eventType     = payload.value("eventType", std::string(""));
    const std::string eventKey      = payload.value("eventKey",  std::string(""));
    const nlohmann::json eventData  = payload.value("eventData", nlohmann::json{});

    const bool        isStreaming   = payload.value("streaming", false);
    const bool        isBossEvent   = IsBossSyncEventType(eventType);

    if (packetScene < 0 || packetRoom < 0 || eventType.empty() || eventKey.empty()) {
        SPDLOG_WARN("[Anchor:EventSync] RECV malformed ROOM_EVENT — ignored");
        return;
    }

    // Only process events for the current scene.
    if (packetScene != gPlayState->sceneNum) return;

    if (isBossEvent && !roomState.syncEnemies) return;

    if (isBossEvent && payload.contains("clientId")) {
        const uint32_t senderClientId = payload.value("clientId", 0u);
        const std::string roomKey = BuildRoomKey(packetScene, (s8)packetRoom);
        uint32_t roomMasterId = roomState.ownerClientId;
        auto roomIt = roomAuthority.find(roomKey);
        if (roomIt != roomAuthority.end() && roomIt->second != 0) {
            roomMasterId = roomIt->second;
        }
        if (senderClientId != 0 && roomMasterId != 0 && senderClientId != roomMasterId) {
            SPDLOG_WARN("[Anchor:BossSync] Ignore boss event from non-master sender={} expectedMaster={} type={}",
                        senderClientId, roomMasterId, eventType);
            return;
        }
    }

    // Idempotency check (skipped for streaming events like MINIGAME_SCORE).
    const std::string dedupKey = std::to_string(packetScene) + "_" +
                                 std::to_string(packetRoom)  + "_" +
                                 eventType                   + "_" +
                                 eventKey;
    if (!isStreaming && !isBossEvent) {
        if (processedRoomEvents.count(dedupKey)) {
            // Duplicate delivery — silently drop.
            return;
        }
        processedRoomEvents.insert(dedupKey);
    }

    if (!isStreaming) {
        SPDLOG_INFO("[Anchor:EventSync] RECV {} | key={} | scene=0x{:02x} room={}",
                    eventType, eventKey, (int)packetScene, packetRoom);
    }

    bool adapterHandled = false;

    if (isBossEvent) {
        std::string bossActorKey = eventData.value("bossActorKey", std::string(""));
        if (bossActorKey.empty()) {
            bossActorKey = eventKey;
        }

        uint32_t seq = payload.value("seq", eventData.value("seq", (uint32_t)0));
        uint32_t masterFrame = payload.value("masterFrame", eventData.value("masterFrame", (uint32_t)0));

        const std::string roomBossKey = BuildRoomKey(packetScene, (s8)packetRoom) + "_" + bossActorKey;
        // Sequence dedup: seq==0 (frame-0 edge case) counts as seq=1 to prevent bypass.
        // Any event at frame 0 gets treated as strictly ordered sequence 1.
        const uint32_t effectiveSeq = (seq == 0) ? 1u : seq;
        {
            uint32_t& lastSeq = lastBossEventSeqByKey[roomBossKey];
            if (effectiveSeq <= lastSeq) {
                return;
            }
            lastSeq = effectiveSeq;
        }

        nlohmann::json& state = bossSnapshotStateByKey[roomBossKey];
        state["bossActorKey"] = bossActorKey;
        state["sceneNum"] = packetScene;
        state["roomNum"] = packetRoom;
        state["lastEventType"] = eventType;
        state["lastSeq"] = seq;
        state["masterFrame"] = masterFrame;
        if (eventData.contains("phaseId"))       state["phaseId"]       = eventData["phaseId"];
        if (eventData.contains("subState"))      state["subState"]      = eventData["subState"];
        if (eventData.contains("invuln"))        state["invuln"]        = eventData["invuln"];
        if (eventData.contains("weakpointMask")) state["weakpointMask"] = eventData["weakpointMask"];
        // hp and bossActorId are critical for master-migration: if this client
        // later becomes master, it must be able to send a correct bossStates
        // snapshot to late joiners. Without hp, ApplyBossSnapshot uses default=1.
        if (eventData.contains("hp"))            state["hp"]            = eventData["hp"];
        if (eventData.contains("bossActorId"))   state["bossActorId"]   = eventData["bossActorId"];
        state["lateJoinCanSkipIntro"] = eventData.value("lateJoinCanSkipIntro", true);

        const s16 bossActorId = (s16)eventData.value("bossActorId", (int)-1);
        if (bossActorId >= 0) {
            adapterHandled = AnchorBossSync::ApplyBossEvent(gPlayState, packetScene, bossActorId, payload);
        }
    }

    // ── Dispatch per event type ──────────────────────────────────────────────

    if (eventType == "MINIGAME_START") {
        // All clients in the same room enter the same minigame state so their
        // HUD score counter and end-of-round dialog show consistent results.
        const u16 minigameId = (u16)eventData.value("minigameId", 0);
        SPDLOG_INFO("[Anchor:EventSync] MINIGAME_START | minigameId={}", (int)minigameId);
        if (minigameId != 0 && roomState.syncMinigames) {
            gSaveContext.minigameState = minigameId;
            gSaveContext.minigameScore = 0;  // reset score for a clean start
            // Kurze Notification: Spieler weiss, dass das Minispiel gestartet wurde.
            Notification::Emit({
                .prefix      = "Minispiel",
                .prefixColor = ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                .message     = "gestartet!",
                .remainingTime = 3.0f,
            });
        }

    } else if (eventType == "MINIGAME_SCORE") {
        // Streaming live score — applied directly so every participant sees the
        // same running total.  The authority already owns the value; clients receive
        // and mirror it so the end-of-round NPC dialog matches for everyone.
        const u16 score = (u16)eventData.value("score", 0);
        if (roomState.syncMinigames) {
            gSaveContext.minigameScore = score;
        }

    } else if (eventType == "MINIGAME_END") {
        // Authoritative final score.  Ensures the NPC dialog ("You scored X!") shows
        // the same number for all players regardless of local counting differences.
        const u16  finalScore = (u16)eventData.value("finalScore", 0);
        const bool won        = eventData.value("won", false);
        SPDLOG_INFO("[Anchor:EventSync] MINIGAME_END | minigameId={} finalScore={} won={}",
                    eventData.value("minigameId", -1), (int)finalScore, won);
        if (roomState.syncMinigames) {
            gSaveContext.minigameScore = finalScore;
            gSaveContext.minigameState = 0;  // clear active state flag
            // Ergebnis-Notification fuer alle Nicht-Master-Clients.
            Notification::Emit({
                .prefix      = won ? "Minispiel gewonnen!" : "Minispiel beendet",
                .prefixColor = won ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                   : ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                .message     = "Endpunktzahl: " + std::to_string((int)finalScore),
                .remainingTime = 5.0f,
            });
        }

    } else if (eventType == "MINIGAME_PROXY_STATE") {
        // Non-master clients proxy their local minigame RAM state to the room
        // master (needed for actors/minigames that don't mutate the master's
        // local state directly, e.g. diving-game counters). The master then
        // emits canonical MINIGAME_* events in HookHandlers.
        if (!roomState.syncMinigames) return;
        if (!IsRoomMaster()) return;

        const u16 minigameId = (u16)eventData.value("minigameId", 0);
        const u16 score      = (u16)eventData.value("score", 0);

        gSaveContext.minigameState = minigameId;
        gSaveContext.minigameScore = score;

    } else if (eventType == "RUTO_CARRY_STATE") {
        // Streamed carry pose for En_Ru1 inside Jabu-Jabu so remote clients do
        // not lose Ruto or desync her pickup/drop visual state.
        if (gPlayState->sceneNum != SCENE_JABU_JABU) return;

        const bool carrying = eventData.value("carrying", false);

        EnRu1* ruto = nullptr;
        Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;
        while (actor != nullptr) {
            if (actor->id == ACTOR_EN_RU1 && (actor->room == packetRoom || actor->room == -1)) {
                ruto = (EnRu1*)actor;
                break;
            }
            actor = actor->next;
        }
        if (!ruto) return;

        if (carrying) {
            ruto->actor.room = -1;
            ruto->actor.world.pos.x = eventData.value("x", ruto->actor.world.pos.x);
            ruto->actor.world.pos.y = eventData.value("y", ruto->actor.world.pos.y);
            ruto->actor.world.pos.z = eventData.value("z", ruto->actor.world.pos.z);
            ruto->actor.world.rot.y = (s16)eventData.value("rotY", (int)ruto->actor.world.rot.y);
            ruto->actor.shape.rot.y = ruto->actor.world.rot.y;
            ruto->actor.velocity.x = 0.0f;
            ruto->actor.velocity.y = 0.0f;
            ruto->actor.velocity.z = 0.0f;
            ruto->actor.speedXZ = 0.0f;
            ruto->actor.gravity = 0.0f;
            ruto->actor.minVelocityY = 0.0f;
            ruto->action = 31;
            ruto->drawConfig = 1;
        }

    } else if (eventType == "PUZZLE_SWITCH_SOLVED") {
        // One-shot room-local solve state for link-movable push-block puzzles.
        // Only clients inside the same room apply the switch flag.
        if (!roomState.syncBGObjects) return;
        if ((s8)packetRoom != gPlayState->roomCtx.curRoom.num) return;

        const s16 switchFlag = (s16)eventData.value("switchFlag", -1);
        if (switchFlag < 0 || switchFlag > 0x3F) return;

        if (!Flags_GetSwitch(gPlayState, switchFlag)) {
            Flags_SetSwitch(gPlayState, switchFlag);
        }

    } else if (eventType == "WEB_BURNED") {
        // One-shot: a player burned a Deku Tree spider web (BgYdanSp).
        // The eventKey is the web's actorKey.  Find the web and burn it
        // if it is still in idle state on this client.
        // This event is sent via ROOM_EVENT so it works regardless of
        // syncItemsAndFlags (switch-flag sync is gated behind that setting).
        Actor* webActor = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head;
        while (webActor != nullptr) {
            if (webActor->id == ACTOR_BG_YDAN_SP &&
                GetActorKey(webActor, gPlayState->sceneNum) == eventKey) {
                BgYdanSp* web = (BgYdanSp*)webActor;
                if (web->actionFunc == BgYdanSp_FloorWebIdle ||
                    web->actionFunc == BgYdanSp_WallWebIdle) {
                    BgYdanSp_BurnWeb(web, gPlayState);
                }
                break;
            }
            webActor = webActor->next;
        }
        // Room-master authoritative spawn relay for Jabu-specific scripted actors
        // that may not spawn reliably on non-masters due to local cutscene/script
        // divergence (Big Octo + electrified tentacles).
        if (gPlayState->sceneNum != SCENE_JABU_JABU) return;
        if (IsRoomMaster()) return;

        s16 actorId = (s16)eventData.value("actorId", (int)-1);
        s16 params = (s16)eventData.value("params", 0);
        if (actorId != ACTOR_EN_BIGOKUTA && actorId != ACTOR_EN_BX) return;

        Vec3f pos = {
            eventData.value("x", 0.0f),
            eventData.value("y", 0.0f),
            eventData.value("z", 0.0f),
        };
        Vec3s rot = {
            (s16)eventData.value("rotX", 0),
            (s16)eventData.value("rotY", 0),
            (s16)eventData.value("rotZ", 0),
        };

        // Dedup by proximity + params to avoid duplicate spawns when local
        // scripts already created the actor naturally.
        for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
            Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (actor->id == actorId && actor->params == params &&
                    (actor->room == packetRoom || actor->room == -1)) {
                    if (Math3D_Vec3fDistSq(&actor->world.pos, &pos) < (120.0f * 120.0f)) {
                        return;
                    }
                }
                actor = actor->next;
            }
        }

        Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId,
                                     pos.x, pos.y, pos.z,
                                     rot.x, rot.y, rot.z,
                                     params);
        if (spawned) {
            spawned->room = packetRoom;
        }

    } else if (eventType == "OCARINA_SONG_ACTION") {
        // A non-master client played an ocarina song successfully.  Apply the
        // same song-action result on the room master so BG actors that respond
        // to songs (waterfall, Jabu-Jabu, temple triggers) receive the cue.
        // We only apply if WE are the room master; other clients ignore this.
        if (!IsRoomMaster()) return;
        if (!gPlayState) return;
        u16 remoteMode   = (u16)eventData.value("ocarinaMode",    0);
        u16 remoteAction = (u16)eventData.value("ocarinaAction",  0);
        u8  remoteSong   = (u8)eventData.value("lastPlayedSong",  0);
        SPDLOG_INFO("[Anchor:OcarinaSync] MASTER: applying remote song | mode={} action={} song={}",
                    (int)remoteMode, (int)remoteAction, (int)remoteSong);
        gPlayState->msgCtx.ocarinaMode    = (u16)remoteMode;
        gPlayState->msgCtx.ocarinaAction  = remoteAction;
        gPlayState->msgCtx.lastPlayedSong = remoteSong;
        GameInteractor_ExecuteOnOcarinaSongAction();

    } else if (eventType == "BOSS_DEATH_COMMIT") {
        // Set hp=0 so the boss's own AI triggers its native death sequence
        // (animation, sound, cutscene). This matches the master's visual.
        // We do NOT call Actor_Kill here — the definitive kill arrives later
        // via the ACTOR_KILLED packet emitted by the master's OnActorKill hook
        // once its own death sequence completes.
        // If the boss AI on this client also calls Actor_Kill (normal for OoT bosses),
        // the subsequent ACTOR_KILLED is a safe no-op on an already-dead actor.
        // CI guardrail requires explicit adapter/fallback split. For DEATH_COMMIT
        // we intentionally force fallback handling because adapters only clamp HP.
        adapterHandled = false;
        if (adapterHandled) return;
        std::string bossActorKey = eventData.value("bossActorKey", eventKey);
        if (bossActorKey.empty()) return;

        for (int cat : { ACTORCAT_BOSS, ACTORCAT_ENEMY }) {
            Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (GetActorKey(actor, gPlayState->sceneNum) == bossActorKey) {
                    if (actor->update != nullptr && actor->colChkInfo.health != 0) {
                        actor->colChkInfo.health = 0;
                    }
                    return;
                }
                actor = actor->next;
            }
        }

    } else if (eventType == "BOSS_SUBACTOR_KILL") {
        std::string targetActorKey = eventData.value("targetActorKey", std::string(""));
        if (targetActorKey.empty()) return;

        for (int cat = 0; cat < ACTORCAT_MAX; ++cat) {
            Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
            while (actor != nullptr) {
                if (GetActorKey(actor, gPlayState->sceneNum) == targetActorKey) {
                    if (actor->update != nullptr) {
                        Actor_Kill(actor);
                    }
                    return;
                }
                actor = actor->next;
            }
        }

    } else if (eventType == "BOSS_STAGE_ENTER" ||
               eventType == "BOSS_WEAKPOINT_HIT" ||
               eventType == "BOSS_WEAKPOINT_DESTROY" ||
               eventType == "BOSS_SUBACTOR_SPAWN" ||
               eventType == "BOSS_INVULN_SET") {
        // Infrastructure-only phase: state is already cached above in
        // bossSnapshotStateByKey. Concrete boss adapters are added incrementally.
        return;

    } else if (eventType == "BOSS_CUTSCENE_GATE") {
        // Generic gate for boss intros/intermissions. If skipIntro is true,
        // late joiners and in-room clients can immediately leave the cutscene.
        const bool skipIntro = eventData.value("skipIntro", false);
        if (skipIntro && gPlayState->csCtx.state != CS_STATE_IDLE) {
            func_8006450C(gPlayState, &gPlayState->csCtx);
        }
        return;

    } else {
        // Unknown or future event type — warn but don't crash.
        SPDLOG_WARN("[Anchor:EventSync] Unknown eventType '{}' — ignored", eventType);
    }
}
