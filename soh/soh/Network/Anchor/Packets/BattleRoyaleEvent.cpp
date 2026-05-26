#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <random>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

// ─── Battle Royale Spawn Table ───────────────────────────────────────────────
// 8 gleichmaessig ueber Hyrule Field verteilte Spawnpunkte.
// Y=0 ist auf dem flachen Hyrule Field sicher; die N64-Engine floor-snappt
// den Spieler in jedem Frame automatisch auf den Boden.
// Yaw (s16): OoT-Konvention – 0=Sued, 0x4000=West, 0x8000=Nord, 0xC000=Ost.
struct BrSpawnPoint {
    float x;
    float y;
    float z;
    s16   yaw;
};
static constexpr BrSpawnPoint kBrHyruleFieldSpawns[] = {
    { -1600.0f, 0.0f,  5000.0f, (s16)0x8000 },  // Sued  (Kokiri Forest Seite) – Richtung Norden
    {  3000.0f, 0.0f,  3500.0f, (s16)0xC000 },  // Suedost  (Lon Lon Ranch)   – Richtung Westen
    {  3500.0f, 0.0f,  -500.0f, (s16)0xC000 },  // Ost                        – Richtung Westen
    {  2200.0f, 0.0f, -3500.0f, (s16)0xE000 },  // Nordost  (Kakariko Seite)  – Richtung Suedwest
    {  -500.0f, 0.0f, -2500.0f, (s16)0x0000 },  // Nord  (Schloss-Seite)      – Richtung Sueden
    { -3800.0f, 0.0f,  -500.0f, (s16)0x4000 },  // Nordwest                   – Richtung Osten
    { -4000.0f, 0.0f,  2500.0f, (s16)0x4000 },  // West  (Gerudo Tal Seite)   – Richtung Osten
    { -1000.0f, 0.0f,  2500.0f, (s16)0x8000 },  // Mitte                      – Richtung Norden
};
static constexpr int kBrSpawnCount = (int)(sizeof(kBrHyruleFieldSpawns) / sizeof(kBrHyruleFieldSpawns[0]));

// ─── Item-Loot Whitelist ──────────────────────────────────────────────────────
// Items, die einem besiegten Spieler gestohlen werden können.
// Quest-kritische und kosmetische Items (Ocarina, Trade-Sequence, Medallien-Anzeigeform
// via questItems-Bitmask) sind AUSGENOMMEN – sie stecken in anderen Feldern.
// Hier werden nur die 24 Inventar-Slots (inventory.items[]) berücksichtigt.
static constexpr u8 kBrLootableItems[] = {
    ITEM_STICK,         ITEM_NUT,           ITEM_BOMB,          ITEM_BOW,
    ITEM_ARROW_FIRE,    ITEM_DINS_FIRE,     ITEM_SLINGSHOT,     ITEM_BOMBCHU,
    ITEM_HOOKSHOT,      ITEM_LONGSHOT,      ITEM_ARROW_ICE,     ITEM_FARORES_WIND,
    ITEM_BOOMERANG,     ITEM_LENS,          ITEM_BEAN,          ITEM_HAMMER,
    ITEM_ARROW_LIGHT,   ITEM_NAYRUS_LOVE,   ITEM_BOTTLE,        ITEM_POTION_RED,
    ITEM_POTION_GREEN,  ITEM_POTION_BLUE,   ITEM_MILK_BOTTLE,
    ITEM_SWORD_KOKIRI,  ITEM_SWORD_MASTER,  ITEM_SWORD_BGS,
    ITEM_SHIELD_DEKU,   ITEM_SHIELD_HYLIAN, ITEM_SHIELD_MIRROR,
    ITEM_TUNIC_GORON,   ITEM_TUNIC_ZORA,
    ITEM_BOOTS_IRON,    ITEM_BOOTS_HOVER,
    ITEM_BOW_ARROW_FIRE, ITEM_BOW_ARROW_ICE, ITEM_BOW_ARROW_LIGHT,
};
static constexpr int kBrLootCount = 3;  // Items pro Kill gestohlen

static bool IsBrLootable(u8 item) {
    if (item == ITEM_NONE) return false;
    for (const u8 l : kBrLootableItems) {
        if (l == item) return true;
    }
    return false;
}

/**
 * BATTLE_ROYALE_EVENT
 *
 * Host-authoritative event channel for Battle Royale match lifecycle.
 * All events are broadcast to every connected client.  Only the host
 * (IsHostAuthority()) acts on PLAYER_KILLED: it updates kill streaks,
 * broadcasts WANTED_SET and PLAYER_ELIM as follow-ups, and declares
 * MATCH_END when a single survivor remains.  Every client acts on
 * WANTED_SET, WANTED_CLEAR, PLAYER_ELIM, and MATCH_END.
 *
 * Event types (payload field "eventType"):
 *
 *   PLAYER_KILLED  – Sent by the local VICTIM immediately after dying from
 *                    PvP damage.  Payload: attackerClientId.
 *                    Host: increments brKillStreak[attacker]; if the streak
 *                    crosses BR_WANTED_KILL_THRESHOLD, broadcasts WANTED_SET.
 *                    Host: then broadcasts PLAYER_ELIM for the victim.
 *
 *   PLAYER_ELIM   – Sent by HOST to all.  Payload: targetClientId.
 *                    All: reset the victim's kill streak; remove from
 *                    wantedClients; log the elimination.
 *                    If only one client remains alive the host also broadcasts
 *                    MATCH_END.
 *
 *   WANTED_SET    – Sent by HOST to all.  Payload: targetClientId.
 *                    All: insert targetClientId into wantedClients.
 *
 *   WANTED_CLEAR  – Sent by HOST to all.  Payload: targetClientId.
 *                    All: remove targetClientId from wantedClients.
 *
 *   MATCH_START   – Sent by HOST to all.  No extra payload.
 *                    All: clear brKillStreak, wantedClients, brEliminated;
 *                    set brMatchActive = true; show start notification.
 *
 *   MATCH_END     – Sent by HOST to all.  Payload: winnerClientId.
 *                    All: clear brKillStreak and wantedClients;
 *                    set brMatchActive = false; log the winner.
 *
 * Routing note: the Anchor server auto-injects "clientId" (sender) into every
 * packet.  "targetClientId" is a game-level field, not a routing directive
 * — the server still broadcasts BATTLE_ROYALE_EVENT to all peers.
 */

// ─── Send ────────────────────────────────────────────────────────────────────

void Anchor::SendPacket_BattleRoyaleEvent(const std::string& eventType, nlohmann::json data) {
    data["type"]      = BATTLE_ROYALE_EVENT;
    data["eventType"] = eventType;
    SendJsonToRemote(data);
}

// ─── Handle ──────────────────────────────────────────────────────────────────

void Anchor::HandlePacket_BattleRoyaleEvent(nlohmann::json payload) {
    if (!roomState.battleRoyaleMode) {
        return;
    }

    const std::string eventType = payload.value("eventType", "");

    // ── MATCH_START ──────────────────────────────────────────────────────────
    // Host-broadcast: a new Battle Royale match begins.  Resets all match state
    // so a rematch can be started cleanly without a full reconnect.
    if (eventType == "MATCH_START") {
        brKillStreak.clear();
        wantedClients.clear();
        brMatchActive = true;
        brEliminated  = false;

        // Startschutz: PvP wird fuer die ersten N Sekunden nach Match-Start gesperrt,
        // damit alle Spieler Zeit haben, sich nach dem Spawn zu orientieren.
        const int protSecs     = payload.value("startProtectionSecs", 10);
        brStartProtectionUntil = Clock::now() + std::chrono::seconds(protSecs);

        // ── Spawn-Teleport nach Hyrule Field ─────────────────────────────────
        // Der Host weist jedem Client per spawnAssignments einen Spawn-Index zu
        // (JSON-Objekt: clientId_string → index).  Wir lesen unseren Index,
        // schlagen die Position in kBrHyruleFieldSpawns nach und teleportieren
        // ueber den RESPAWN_MODE_DOWN-Pfad – identisch zu HandlePacket_TeleportTo.
        if (IsSaveLoaded() && gPlayState) {
            auto spawnMap = payload.value("spawnAssignments", nlohmann::json::object());
            const std::string selfKey = std::to_string(ownClientId);
            const int spawnIdx = spawnMap.value(selfKey, 0) % kBrSpawnCount;
            const BrSpawnPoint& sp = kBrHyruleFieldSpawns[spawnIdx];

            gPlayState->nextEntranceIndex = ENTR_HYRULE_FIELD_0_1;
            gPlayState->transitionTrigger = TRANS_TRIGGER_START;
            gPlayState->transitionType    = TRANS_TYPE_INSTANT;

            gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = ENTR_HYRULE_FIELD_0_1;
            gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex     = 0;
            gSaveContext.respawn[RESPAWN_MODE_DOWN].pos           = { sp.x, sp.y, sp.z };
            gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw           = sp.yaw;
            gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams  = 0xDFF;
            gSaveContext.nextTransitionType                       = TRANS_TYPE_FADE_BLACK_FAST;
            gSaveContext.respawnFlag                              = 1;

            // Void-Schaden beim Respawn unterdruecken (wie HandlePacket_TeleportTo).
            static HOOK_ID spawnHookId = 0;
            spawnHookId = REGISTER_VB_SHOULD(VB_INFLICT_VOID_DAMAGE, {
                *should = false;
                GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(spawnHookId);
            });

            SPDLOG_INFO("[Anchor:BR] Spawn-Punkt {} ({:.0f},{:.0f},{:.0f})", spawnIdx, sp.x, sp.y, sp.z);
        }

        SPDLOG_INFO("[Anchor:BR] Match gestartet! Startschutz: {}s", protSecs);
        Notification::Emit({
            .prefix      = "Battle Royale",
            .prefixColor = ImVec4(1.0f, 0.7f, 0.0f, 1.0f),
            .message     = "Das Match hat begonnen! PvP startet in " + std::to_string(protSecs) + " Sekunden.",
            .remainingTime = (float)(protSecs + 2),
        });
        return;
    }

    // ── PLAYER_KILLED ────────────────────────────────────────────────────────
    if (eventType == "PLAYER_KILLED") {
        // Only the host performs kill accounting and sends follow-up events.
        if (!IsHostAuthority()) {
            return;
        }

        const uint32_t senderClientId   = payload.value("clientId", 0u);  // victim
        const uint32_t attackerClientId = payload.value("attackerClientId", 0u);

        if (attackerClientId == 0 || senderClientId == 0) {
            return;
        }

        // Increment killer's streak.
        u8& streak = brKillStreak[attackerClientId];
        streak     = (streak < 255) ? streak + 1 : 255;

        const std::string attackerName =
            clients.count(attackerClientId) ? clients[attackerClientId].name : std::to_string(attackerClientId);
        const std::string victimName =
            clients.count(senderClientId) ? clients[senderClientId].name : std::to_string(senderClientId);

        SPDLOG_INFO("[Anchor:BR] {} killed {} (kill streak: {})", attackerName, victimName, (int)streak);

        // Threshold check: first time reaching it → WANTED_SET.
        if (streak == BR_WANTED_KILL_THRESHOLD) {
            SendPacket_BattleRoyaleEvent("WANTED_SET", { { "targetClientId", attackerClientId } });
        }

        // Announce the elimination to everyone (host sends on behalf of all).
        // ── Item-Loot: Bis zu kBrLootCount zufaellige lootbare Items ─────────────
        // victimInventory: 24-Element-JSON-Array (inventory.items[0..23]).
        // Ob der Killer ein Item bereits besitzt, prüft dessen Client selbst
        // (kein Überschreiben vorhandener Items).
        nlohmann::json lootItems = nlohmann::json::array();
        {
            auto victimInventory = payload.value("victimInventory", nlohmann::json::array());
            if (victimInventory.size() == 24) {
                std::vector<std::pair<int, u8>> candidates;
                for (int s = 0; s < 24; s++) {
                    const u8 item = victimInventory[s].get<u8>();
                    if (IsBrLootable(item)) {
                        candidates.push_back({ s, item });
                    }
                }
                // Fisher-Yates shuffle – faire Zufallsauswahl ohne Wiederholungen.
                std::mt19937 rng(static_cast<uint32_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count() ^
                    (attackerClientId << 16) ^ senderClientId));
                std::shuffle(candidates.begin(), candidates.end(), rng);
                const int pickCount = std::min(static_cast<int>(candidates.size()), kBrLootCount);
                for (int i = 0; i < pickCount; i++) {
                    lootItems.push_back({ {"slot", candidates[i].first}, {"item", candidates[i].second} });
                }
            }
        }

        // killerClientId für Kill-Feed; lootItems für Item-Transfer beim Empfänger.
        // Das Opfer respawnt (kein permanentes Ausscheiden): PLAYER_ELIM löst
        // Stat-Reset + Teleport nach Hyrule Field aus.
        // Das Match endet erst, wenn ein Spieler Ganondorf besiegt (OnBossDefeat-Hook).
        SendPacket_BattleRoyaleEvent("PLAYER_ELIM", {
            { "targetClientId", senderClientId   },
            { "killerClientId", attackerClientId },
            { "lootItems",      lootItems        }
        });

        return;
    }

    // ── PLAYER_ELIM ──────────────────────────────────────────────────────────
    if (eventType == "PLAYER_ELIM") {
        const uint32_t targetId      = payload.value("targetClientId", 0u);
        const uint32_t killerClientId = payload.value("killerClientId", 0u);

        // Kill-Streak des Opfers zurücksetzen – Spieler respawnt und startet neu.
        // Kein 0xFF-Sentinel mehr: Spieler sind nie permanent ausgeschieden.
        brKillStreak[targetId] = 0;
        wantedClients.erase(targetId);

        const std::string victimName = clients.count(targetId)       ? clients[targetId].name       : std::to_string(targetId);
        const std::string killerName = clients.count(killerClientId) ? clients[killerClientId].name : std::to_string(killerClientId);

        auto lootItems = payload.value("lootItems", nlohmann::json::array());

        SPDLOG_INFO("[Anchor:BR] {} getoetet von {} – respawnt. ({} Items geplündert)",
                    victimName, killerName, (int)lootItems.size());

        // ── Opfer: Stat-Reset + Respawn ──────────────────────────────────────
        if (targetId == ownClientId) {
            // 1. Gestohlene Items aus eigenem Inventar entfernen.
            for (const auto& lootEntry : lootItems) {
                const int slot = lootEntry.value("slot", -1);
                if (slot >= 0 && slot < 24) {
                    gSaveContext.inventory.items[slot] = ITEM_NONE;
                }
            }

            // 2. Lebenspunkte auf Ausgangswert (3 Herzen) zurücksetzen.
            //    Herz-Container (healthCapacity) bleiben erhalten – der Spieler
            //    muss sie nicht erneut freischalten, verliert aber den vollen Balken.
            gSaveContext.health = STARTING_HEALTH;

            // 3. Rupien auf 0 zurücksetzen.
            gSaveContext.rupees = 0;

            // 4. Kisten-Flags aller Szenen löschen, damit Items erneut
            //    geöffnet/erhalten werden können ("normaler Weg").
            for (int i = 0; i < 124; i++) {
                gSaveContext.sceneFlags[i].chest = 0;
            }

            // 5. brEliminated freigeben + Respawn-Schutz aktivieren (10 s).
            brEliminated           = false;
            brStartProtectionUntil = Clock::now() + std::chrono::seconds(10);

            // 6. Respawn in Hyrule Field – zufälliger Spawn-Punkt, damit Spieler
            //    nicht immer an derselben Stelle auftauchen.
            if (IsSaveLoaded() && gPlayState) {
                static uint32_t respawnCounter = 0;
                const int spawnIdx = (++respawnCounter ^ static_cast<uint32_t>(targetId)) % kBrSpawnCount;
                const BrSpawnPoint& sp = kBrHyruleFieldSpawns[spawnIdx];

                gPlayState->nextEntranceIndex = ENTR_HYRULE_FIELD_0_1;
                gPlayState->transitionTrigger = TRANS_TRIGGER_START;
                gPlayState->transitionType    = TRANS_TYPE_INSTANT;

                gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = ENTR_HYRULE_FIELD_0_1;
                gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex     = 0;
                gSaveContext.respawn[RESPAWN_MODE_DOWN].pos           = { sp.x, sp.y, sp.z };
                gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw           = sp.yaw;
                gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams  = 0xDFF;
                gSaveContext.nextTransitionType                       = TRANS_TYPE_FADE_BLACK_FAST;
                gSaveContext.respawnFlag                              = 1;

                // Void-Schaden beim Respawn unterdrücken (wie HandlePacket_TeleportTo).
                static HOOK_ID respawnHookId = 0;
                respawnHookId = REGISTER_VB_SHOULD(VB_INFLICT_VOID_DAMAGE, {
                    *should = false;
                    GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(respawnHookId);
                });

                SPDLOG_INFO("[Anchor:BR] Respawn-Punkt {} ({:.0f},{:.0f},{:.0f})",
                            spawnIdx, sp.x, sp.y, sp.z);
            }

            const int lootCount = static_cast<int>(lootItems.size());
            Notification::Emit({
                .prefix      = "GETÖTET",
                .prefixColor = ImVec4(1.0f, 0.1f, 0.1f, 1.0f),
                .message     = "Von " + killerName + " getötet! Respawn in Hyrule Field.",
                .suffix      = lootCount > 0
                    ? std::to_string(lootCount) + " Items gestohlen"
                    : "",
                .suffixColor = ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                .remainingTime = 8.0f,
            });

        // ── Killer: Items aus Loot erhalten ──────────────────────────────────
        } else if (killerClientId == ownClientId && IsSaveLoaded()) {
            int gainedCount = 0;
            for (const auto& lootEntry : lootItems) {
                const int slot = lootEntry.value("slot", -1);
                const u8  item = static_cast<u8>(lootEntry.value("item", static_cast<int>(ITEM_NONE)));
                // Nur nehmen, wenn der eigene Slot leer ist (kein Überschreiben).
                if (slot >= 0 && slot < 24 && item != ITEM_NONE &&
                    gSaveContext.inventory.items[slot] == ITEM_NONE) {
                    gSaveContext.inventory.items[slot] = item;
                    gainedCount++;
                }
            }
            SPDLOG_INFO("[Anchor:BR] Kill-Loot: {} Items erhalten.", gainedCount);

            // Herz-Belohnung: +2 Herzen (Engine-konformes Clamping via Health_ChangeBy).
            if (gPlayState) {
                Health_ChangeBy(gPlayState, 2 * 0x10);
            }
            Notification::Emit({
                .prefix      = "Kill!",
                .prefixColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                .message     = victimName + " getötet.",
                .suffix      = gainedCount > 0
                    ? std::to_string(gainedCount) + " Items geplündert · +2 Herzen"
                    : "+2 Herzen",
                .suffixColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                .remainingTime = 5.0f,
            });

        // ── Alle anderen: Kill-Feed ───────────────────────────────────────────
        } else {
            Notification::Emit({
                .prefix      = killerName,
                .prefixColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                .message     = "tötete",
                .suffix      = victimName,
                .suffixColor = ImVec4(0.8f, 0.6f, 0.6f, 1.0f),
                .remainingTime = 4.0f,
            });
        }

        return;
    }

    // ── WANTED_SET ───────────────────────────────────────────────────────────
    if (eventType == "WANTED_SET") {
        const uint32_t targetId = payload.value("targetClientId", 0u);
        wantedClients.insert(targetId);

        const std::string name = clients.count(targetId) ? clients[targetId].name : std::to_string(targetId);
        SPDLOG_INFO("[Anchor:BR] {} is now WANTED (kill streak >= {})", name, (int)BR_WANTED_KILL_THRESHOLD);

        // ── Kill-Feed HUD ──────────────────────────────────────────────────
        if (targetId == ownClientId) {
            Notification::Emit({
                .prefix      = "GESUCHT!",
                .prefixColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                .message     = "Du bist jetzt gesucht! NPCs greifen dich an.",
                .remainingTime = 6.0f,
            });
        } else {
            Notification::Emit({
                .prefix      = name,
                .prefixColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                .message     = "ist jetzt GESUCHT!",
                .remainingTime = 4.0f,
            });
        }

        return;
    }

    // ── WANTED_CLEAR ─────────────────────────────────────────────────────────
    if (eventType == "WANTED_CLEAR") {
        const uint32_t targetId = payload.value("targetClientId", 0u);
        wantedClients.erase(targetId);

        const std::string name = clients.count(targetId) ? clients[targetId].name : std::to_string(targetId);
        SPDLOG_INFO("[Anchor:BR] {} is no longer WANTED", name);

        // ── Kill-Feed HUD ──────────────────────────────────────────────────
        if (targetId == ownClientId) {
            Notification::Emit({
                .prefix      = "Frei",
                .prefixColor = ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                .message     = "Dein GESUCHT-Status wurde aufgehoben.",
                .remainingTime = 4.0f,
            });
        } else {
            Notification::Emit({
                .prefix      = name,
                .prefixColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                .message     = "ist nicht mehr gesucht.",
                .remainingTime = 3.0f,
            });
        }

        return;
    }

    // ── MATCH_END ────────────────────────────────────────────────────────────
    if (eventType == "MATCH_END") {
        const uint32_t winnerId = payload.value("winnerClientId", 0u);

        brKillStreak.clear();
        wantedClients.clear();
        brMatchActive = false;

        const std::string winnerName =
            clients.count(winnerId) ? clients[winnerId].name : std::to_string(winnerId);
        SPDLOG_INFO("[Anchor:BR] Match over! Winner: {} (client {})", winnerName, winnerId);

        // ── Kill-Feed HUD ──────────────────────────────────────────────────
        if (winnerId == ownClientId) {
            Notification::Emit({
                .prefix      = "SIEG!",
                .prefixColor = ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
                .message     = "Du hast das Battle Royale gewonnen!",
                .remainingTime = 12.0f,
            });
        } else {
            Notification::Emit({
                .prefix      = winnerName,
                .prefixColor = ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
                .message     = "gewinnt das Battle Royale!",
                .remainingTime = 10.0f,
            });
        }
    }
}
