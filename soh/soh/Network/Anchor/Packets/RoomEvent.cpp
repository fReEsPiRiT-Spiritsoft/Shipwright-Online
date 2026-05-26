#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
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

    nlohmann::json payload;
    payload["type"]      = ROOM_EVENT;
    payload["sceneNum"]  = gPlayState->sceneNum;
    payload["roomNum"]   = gPlayState->roomCtx.curRoom.num;
    payload["eventType"] = eventType;
    payload["eventKey"]  = eventKey;
    if (!eventData.empty()) {
        payload["eventData"] = eventData;
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

    if (packetScene < 0 || packetRoom < 0 || eventType.empty() || eventKey.empty()) {
        SPDLOG_WARN("[Anchor:EventSync] RECV malformed ROOM_EVENT — ignored");
        return;
    }

    // Only process events for the current scene.
    if (packetScene != gPlayState->sceneNum) return;

    // Idempotency check (skipped for streaming events like MINIGAME_SCORE).
    const std::string dedupKey = std::to_string(packetScene) + "_" +
                                 std::to_string(packetRoom)  + "_" +
                                 eventType                   + "_" +
                                 eventKey;
    if (!isStreaming) {
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

    } else {
        // Unknown or future event type — warn but don't crash.
        SPDLOG_WARN("[Anchor:EventSync] Unknown eventType '{}' — ignored", eventType);
    }
}
