#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Notification/Notification.h"

// ─────────────────────────────────────────────────────────────────────────────
// PING / PONG  —  round-trip latency measurement
//
// Protocol:
//   1. Every PING_INTERVAL (5 s) the local client broadcasts PING with a
//      monotonically increasing seq number.
//   2. Every peer that receives a PING immediately broadcasts PONG back to all,
//      carrying the original senderClientId and seq so only the sender updates.
//   3. The sender measures RTT = now - sentAt and keeps an exponential moving
//      average for ownPingMs (one-way estimate = RTT / 2).
//   4. ownPingMs is included in each UPDATE_CLIENT_STATE so every peer has a
//      complete global ping table, used for deterministic host election.
//
// Race-condition safety:
//   All computation happens on the game thread (ProcessIncomingPacketQueue).
//   pendingPingAt is accessed only there, so no mutex is required.
// ─────────────────────────────────────────────────────────────────────────────

// ─── Send ─────────────────────────────────────────────────────────────────────

void Anchor::SendPacket_Ping() {
    if (!isConnected || ownClientId == 0) return;

    auto now = Clock::now();

    // Expire entries older than 10 s to keep the map small.
    for (auto it = pendingPingAt.begin(); it != pendingPingAt.end(); ) {
        if ((now - it->second) > std::chrono::seconds(10))
            it = pendingPingAt.erase(it);
        else
            ++it;
    }

    const uint32_t seq  = pingSeq++;
    pendingPingAt[seq]  = now;
    lastPingSentAt      = now;

    nlohmann::json payload;
    payload["type"]  = PING;
    payload["seq"]   = seq;
    payload["quiet"] = true;   // suppress debug log spam
    SendJsonToRemote(payload);
}

// ─── Receive: PING ────────────────────────────────────────────────────────────

void Anchor::HandlePacket_Ping(nlohmann::json payload) {
    if (!isConnected || ownClientId == 0) return;
    if (!payload.contains("seq") || !payload.contains("clientId")) return;

    const uint32_t senderClientId = payload["clientId"].get<uint32_t>();
    if (senderClientId == ownClientId) return; // ignore echo of own PING

    // Reply to all peers — only the original sender will process this PONG
    // (they check pingClientId == ownClientId before updating their RTT).
    nlohmann::json pong;
    pong["type"]           = PONG;
    pong["pingClientId"]   = senderClientId;
    pong["seq"]            = payload["seq"];
    pong["quiet"]          = true;
    SendJsonToRemote(pong);
}

// ─── Receive: PONG ────────────────────────────────────────────────────────────

void Anchor::HandlePacket_Pong(nlohmann::json payload) {
    if (!isConnected || ownClientId == 0) return;
    if (!payload.contains("pingClientId") || !payload.contains("seq")) return;

    // Only the original PING sender processes this PONG.
    const uint32_t pingClientId = payload["pingClientId"].get<uint32_t>();
    if (pingClientId != ownClientId) return;

    const uint32_t seq = payload["seq"].get<uint32_t>();
    auto it = pendingPingAt.find(seq);
    if (it == pendingPingAt.end()) return; // seq already expired

    const auto now   = Clock::now();
    const auto rttMs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count());
    pendingPingAt.erase(it);

    // One-way latency estimate = RTT / 2
    const uint32_t oneWayMs = rttMs / 2;

    // Exponential moving average: 80 % old weight, 20 % new sample.
    // This smooths out single-packet jitter while still tracking trends.
    if (ownPingMs == UINT32_MAX) {
        ownPingMs = oneWayMs;
    } else {
        ownPingMs = static_cast<uint32_t>(ownPingMs * 0.8f + oneWayMs * 0.2f);
    }

    SPDLOG_DEBUG("[Anchor:Ping] RTT={}ms  oneWay={}ms  ownPingMs={}ms",
                 rttMs, oneWayMs, ownPingMs);
}

// ─────────────────────────────────────────────────────────────────────────────
// HOST ELECTION
//
// All clients independently run the same deterministic algorithm:
//   1. Sort online clients (+ self) by pingMs ascending.
//   2. Break ties by clientId ascending (lower id = connected earlier).
//   3. If the result == self, claim ownership by broadcasting UPDATE_ROOM_STATE.
//   4. All other clients receive that packet via HandlePacket_UpdateRoomState
//      and update roomState.ownerClientId without any further coordination.
//
// This means only the elected client ever sends UPDATE_ROOM_STATE — no split-brain.
// ─────────────────────────────────────────────────────────────────────────────

uint32_t Anchor::PickBestHostCandidateId() const {
    // Start with self as the baseline candidate.
    uint32_t bestId   = ownClientId;
    uint32_t bestPing = ownPingMs;  // UINT32_MAX when not yet measured

    for (const auto& [id, client] : clients) {
        if (!client.online) continue;
        if (id == ownClientId) continue; // already accounted for above

        // Prefer lower ping; break ties by lower clientId.
        if (client.pingMs < bestPing ||
            (client.pingMs == bestPing && id < bestId)) {
            bestId   = id;
            bestPing = client.pingMs;
        }
    }
    return bestId; // 0 only if ownClientId == 0 (not yet assigned)
}

void Anchor::ElectNewHostIfNeeded() {
    if (!isConnected || ownClientId == 0) return;

    // ── Is the current owner still online? ───────────────────────────────────
    bool ownerOnline = false;
    if (roomState.ownerClientId != 0) {
        if (roomState.ownerClientId == ownClientId) {
            ownerOnline = true; // we ARE the owner
        } else {
            auto it = clients.find(roomState.ownerClientId);
            if (it != clients.end() && it->second.online) {
                ownerOnline = true;
            }
        }
    }

    if (ownerOnline) {
        // Owner is fine — cancel any pending countdown.
        hostElectionArmed = false;
        hostOfflineSince  = {};
        return;
    }

    // ── Owner offline or unset: arm / advance the grace-period timer ─────────
    const auto now = Clock::now();

    if (!hostElectionArmed) {
        hostElectionArmed = true;
        hostOfflineSince  = now;
        SPDLOG_INFO("[Anchor:HostElect] Owner {} offline — election armed ({}ms grace).",
                    roomState.ownerClientId,
                    (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                        HOST_ELECTION_GRACE).count());
        return;
    }

    if ((now - hostOfflineSince) < HOST_ELECTION_GRACE) {
        return; // Still within grace period — wait for potential reconnect.
    }

    // ── Grace period expired: elect new host ─────────────────────────────────
    const uint32_t candidate = PickBestHostCandidateId();
    if (candidate == 0) return;

    const uint32_t candidatePing =
        (candidate == ownClientId)
            ? ownPingMs
            : (clients.count(candidate) ? clients.at(candidate).pingMs : UINT32_MAX);

    SPDLOG_INFO("[Anchor:HostElect] Electing client {} (ping={}ms) as new host.",
                candidate,
                candidatePing == UINT32_MAX ? 9999u : candidatePing);

    // Reset state regardless of who wins — prevents re-running the election.
    hostElectionArmed = false;
    hostOfflineSince  = {};

    if (candidate != ownClientId) {
        // Another client won the election.  They will claim ownership themselves;
        // we do nothing except wait for their UPDATE_ROOM_STATE.
        return;
    }

    // ── We won: claim ownership ──────────────────────────────────────────────
    roomState.ownerClientId = ownClientId;
    SendPacket_UpdateRoomState();

    Notification::Emit({
        .prefix      = "Host-Wechsel",
        .prefixColor = ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
        .message     = "Du bist jetzt Host (niedrigster Ping).",
        .remainingTime = 5.0f,
    });
    SPDLOG_INFO("[Anchor:HostElect] Self elected as new host (ping={}ms).", ownPingMs);
}
