# Shipwright-Online – TODO & Roadmap

> **Kernphilosophie:** Der Server ist die einzige Wahrheit (Single Source of Truth).  
> Alles wird serverseitig ausgeliefert, Clients rendern nur.  
> Bestehender Anchor-Code wird so weit wie möglich wiederverwendet.

---

## FEATURE A – HP & Item-Count Sync Toggle

Ein Button im Netzwerk-Menü, der steuert ob HP und Verbrauchsgegenstände-Anzahl geteilt werden oder nicht.

### Grundregeln

| Kategorie | Sync-Modus AN | Sync-Modus AUS |
|---|---|---|
| **HP / Lebensenergie** | geteilt | jeder Spieler hat eigene HP |
| **Verbrauchsgüter** (Nüsse, Bomben, Pfeile, Stöcke, Bohnen …) | geteilt | jeder hat seine eigene Anzahl |
| **Item-Freischaltung** (Hookshot, Bogen, Hammer …) | **immer sync** | **immer sync** |
| **Story-Items** (Hammer, Armbänder, Handschuhe, Medaillons, Steine …) | **immer sync** | **immer sync** |
| **Equipment-Flags** (Tuniken, Schuhe, Schilde) | **immer sync** | **immer sync** |
| **Welt-Flags** (Truhen, Schalter, Dungeon-Clear, Event-Flags) | **immer sync** | **immer sync** |

---

### A.1 – Datenstruktur erweitern

**Datei:** `soh/soh/Network/Anchor/Anchor.h`

- [ ] Feld `syncHPAndCounts` in `RoomState` ergänzen:
  ```cpp
  typedef struct {
      uint32_t ownerClientId;
      u8 pvpMode;
      u8 showLocationsMode;
      u8 teleportMode;
      u8 syncItemsAndFlags;
      u8 syncHPAndCounts;  // NEU: 0 = jeder Spieler separat, 1 = geteilt
  } RoomState;
  ```

---

### A.2 – Button im Netzwerk-Menü

**Datei:** `soh/soh/SohGui/SohMenuNetwork.cpp`  
*(Im Anchor-Abschnitt, neben dem vorhandenen "Sync Items & Flags"-Toggle)*

- [ ] Toggle-Button "HP & Item Counts synchronisieren" hinzufügen
- [ ] CVar: `CVAR_REMOTE_ANCHOR("SyncHPAndCounts")`
- [ ] Tooltip erklären: "Aus = jeder Spieler hat eigene HP und Verbrauchsgegenstände-Anzahl. Item-Freischaltungen und Story-Items werden weiterhin immer synchronisiert."
- [ ] Änderung live an alle Team-Mitglieder übertragen (über `UpdateRoomState`-Paket, schon vorhanden)

---

### A.3 – GiveItem differenzieren

**Datei:** `soh/soh/Network/Anchor/Packets/GiveItem.cpp`

Aktuell prüft `SendPacket_GiveItem` nur `roomState.syncItemsAndFlags`.  
Neu: zusätzlich prüfen ob das Item ein **Count-Item** oder ein **Unlock/Story-Item** ist.

- [ ] Hilfsfunktion `IsCountItem(u16 modId, s16 getItemId)` schreiben:
  - Gibt `true` zurück für: Deku-Nüsse, Deku-Stöcke, Bomben, Pfeile, Bohnen, Herz-Stücke (max HP), Magie-Erweiterungen
  - Gibt `false` für alles andere (Hookshot, Hammer, Medaillons, Tuniken, Gauntlets, Bogen als Unlock usw.)
- [ ] `SendPacket_GiveItem`: wenn `!roomState.syncHPAndCounts && IsCountItem(...)` → **nicht senden**
- [ ] `HandlePacket_GiveItem`: gleiche Prüfung → eingehende Count-Items ablehnen wenn unsync

---

### A.4 – HP-Sync steuern

**Datei:** `soh/soh/Network/Anchor/Packets/DamagePlayer.cpp`

Aktuell hat `HandlePacket_DamagePlayer` keine syncHPAndCounts-Prüfung.

- [ ] In `HandlePacket_DamagePlayer`: prüfen ob `roomState.syncHPAndCounts` gesetzt
  - Falls 0 (unsync): **eingehenden Schaden ignorieren** (kein apply)
- [ ] In `SendPacket_DamagePlayer`: bei unsync den Schaden ebenfalls **nicht senden**

---

### A.5 – UpdateTeamState anpassen

**Datei:** `soh/soh/Network/Anchor/Packets/UpdateTeamState.cpp`

Aktuell wird der komplette `gSaveContext` gesendet (inkl. HP und Ammo-Counts).

- [ ] Beim Empfang von `UPDATE_TEAM_STATE`: wenn `!syncHPAndCounts`, folgende Felder **nicht übernehmen**:
  - `gSaveContext.playerData.health`
  - `gSaveContext.playerData.healthCapacity` (sofern nicht durch Item-Unlock bedingt)
  - `gSaveContext.inventory.ammo[*]` (alle Ammo-Slots)
  - Optonal: `gSaveContext.playerData.rupees` (diskutieren ob Rupien pro-Spieler sein sollen)
- [ ] Item-Slots (`gSaveContext.inventory.items[*]`) **immer** übernehmen (Freischaltungen)
- [ ] Equipment-Flags, Quest-Items, Upgrade-Flags **immer** übernehmen

---

### A.6 – Definitionsliste: Was ist ein "Story/Unlock-Item"

Für die Hilfsfunktionen – diese Items **immer** synchronisieren (unabhängig vom Toggle):

- [ ] Alle Medaillons (`RG_FOREST_MEDALLION` … `RG_LIGHT_MEDALLION`)
- [ ] Alle Spiritual Stones
- [ ] Hammer (`ITEM_HAMMER`)
- [ ] Silver/Golden Gauntlets (`ITEM_GAUNTLETS_SILVER`, `ITEM_GAUNTLETS_GOLD`)
- [ ] Goron-Armband (`ITEM_BRACELET`)
- [ ] Tuniken (`ITEM_TUNIC_GORON`, `ITEM_TUNIC_ZORA`)
- [ ] Schuhe (`ITEM_BOOTS_IRON`, `ITEM_BOOTS_HOVER`)
- [ ] Hookshot / Longshot
- [ ] Bogen (erster Erwerb = Unlock), Bomben-Bag (erster Erwerb), Köcher (erster Erwerb)
- [ ] Alle Boss-Keys, kleine Schlüssel (als Quest-Items)
- [ ] Zelda's Lullaby, Epona's Song, Saria's Song usw. (Okarina-Songs)
- [ ] Alle Event-Check-Inf-Flags (Boss-Niederlagen, Story-Events)

---

## FEATURE B – Server-Authority Multiplayer (8-Wochen-Plan)

### Übersicht neue Dateien

```
soh/soh/Network/Anchor/
├── AnchorServer.cpp/.h           ← Woche 1: Dedizierter Server-Modus
├── Packets/
│   ├── ActorListInit.cpp/.h      ← Woche 2: Gegner-Liste vom Server
│   ├── ActorStateUpdate.cpp/.h   ← Woche 3: Gegner-Positionen & HP
│   ├── ActorKilled.cpp/.h        ← Woche 4: Gegner gestorben
│   ├── PlayerAttackActor.cpp/.h  ← Woche 4: Spieler greift Gegner an
│   ├── SetClearFlag.cpp/.h       ← Woche 5: Raum-Clear-Flag
│   └── SceneTransition.cpp/.h    ← Woche 6: Szenenwechsel
│   [Bereits vorhanden ✅]
│   ├── SetFlag.cpp
│   ├── UnsetFlag.cpp
│   ├── PlayerUpdate.cpp
│   ├── GiveItem.cpp
│   ├── DamagePlayer.cpp
│   ├── TeleportTo.cpp
│   └── ... (alle anderen)
```

---

### Woche 1 – Server-Fundament

**Ziel:** Dedizierter Server-Prozess läuft, Clients verbinden sich, Server weiß wer in welcher Szene ist.

- [ ] **`AnchorServer.cpp/.h` (NEU)** – Server-Klasse ohne Grafik:
  ```cpp
  class AnchorServer {
      std::map<s16, SceneState>    sceneStates;  // sceneNum → Actor-Liste
      std::map<uint32_t, ClientInfo> clients;
  };
  ```
- [ ] Server-Start-Flag (`--server`-Argument oder CMake-Option)
- [ ] Hook: `GameInteractor_ExecuteAfterSceneCommands` → Server erfährt Szenen-Load
- [ ] Hook: `OnSceneSpawnActors` → Server bekommt komplette Actor-Liste beim Laden
- [ ] Clients verbinden sich wie bisher über Sail/WebSocket
- [ ] Server sendet bestätigtes `HANDSHAKE` zurück mit Server-Autoritäts-Flag
- [ ] **Test:** 2 Clients verbinden sich → Server zeigt wer wo ist

---

### Woche 2 – Actor-Liste vom Server

**Ziel:** Clients spawnen Gegner NICHT lokal – nur was der Server schickt existiert.

- [ ] **`ActorListInit.cpp/.h` (NEU)** – Paket-Typ `ACTOR_LIST_INIT`:
  ```cpp
  struct ActorInitEntry {
      uint32_t serverActorId;   // eindeutige Server-ID
      s16      actorId;         // ACTOR_EN_STALFOS, etc.
      s16      params;
      Vec3f    pos;
      Vec3s    rot;
      s8       room;
  };
  ```
- [ ] Stabile Actor-ID Formel implementieren:
  ```cpp
  uint64_t GetStableActorId(Actor* actor, s16 sceneNum, s16 spawnIndex) {
      return ((uint64_t)sceneNum    << 32) |
             ((uint64_t)actor->room << 24) |
             ((uint64_t)actor->id   << 16) |
             ((uint64_t)spawnIndex  <<  0);
  }
  // spawnIndex = Index in play->setupActorList[]
  // ActorListIndex.cpp (ObjectExtension) schon vorhanden ✅
  ```
- [ ] Client-Seite: `VB_SPAWN_ACTOR_ENTRY` Hook blockiert lokale Gegner-Spawns wenn im Server-Modus
- [ ] Client wartet auf `ACTOR_LIST_INIT` vom Server → spawnt Gegner dann
- [ ] Nicht-Gegner (Spieler, Items, Türen) weiterhin lokal spawnen
- [ ] **Test:** Deku Tree – alle Clients sehen dieselben Skulltulas

---

### Woche 3 – Actor-Zustand synchronisieren

**Ziel:** Gegner-Positionen und HP werden 30x/s vom Server gebroadcastet.

- [ ] **`ActorStateUpdate.cpp/.h` (NEU)** – Paket-Typ `ACTOR_STATE_UPDATE`:
  ```cpp
  struct ActorStateUpdate {
      uint32_t serverActorId;
      Vec3f    pos;
      Vec3s    rot;
      u8       health;
      u32      flags;       // Actor-eigene State-Flags
      u8       actionFunc;  // Index der aktuellen Aktion
  };
  ```
- [ ] Server-Seite: `OnActorUpdate` Hook → sammelt alle Gegner-States → 30Hz Broadcast
- [ ] Client-Seite: empfängt → überschreibt lokale Actor-Daten direkt im Speicher
- [ ] `ShouldActorUpdate` Hook auf Client → lokale KI für Server-kontrollierte Actors deaktivieren:
  ```cpp
  COND_HOOK(ShouldActorUpdate, isConnected, [&](void* actor, bool* should) {
      if (isServerControlledActor(actor)) *should = false;
  });
  ```
- [ ] Map: `serverActorId → lokaler Actor*` verwalten auf Client-Seite
- [ ] **Test:** Stalfos bewegt sich auf beiden Screens identisch

---

### Woche 4 – Spieler-Angriffe auf Gegner

**Ziel:** Schaden wird immer über den Server abgewickelt – kein doppelter Kill möglich.

- [ ] **`PlayerAttackActor.cpp/.h` (NEU)** – Paket-Typ `PLAYER_ATTACK_ACTOR`:
  ```cpp
  struct PlayerAttackActor {
      uint32_t serverActorId;
      u8       damageEffect;    // Schwert, Bombe, Pfeil …
      u8       damage;
      uint32_t attackerClientId;
  };
  ```
- [ ] Hook: Vor `CollisionCheck`-Schadensanwendung auf Gegner → `SendPacket_PlayerAttackActor`
- [ ] Lokale Schadensanwendung **blockieren** (warten auf Server-Bestätigung)
- [ ] **`ActorKilled.cpp/.h` (NEU)** – Paket-Typ `ACTOR_KILLED`:
  - Server sendet wenn HP eines Actors auf 0 fällt
  - Clients rufen `Actor_Kill()` auf den entsprechenden Actor auf
- [ ] Server-Logik:
  1. `PLAYER_ATTACK_ACTOR` empfangen
  2. HP server-seitig reduzieren
  3. `ACTOR_STATE_UPDATE` mit neuen HP an alle senden
  4. Bei HP = 0 → `ACTOR_KILLED` an alle senden
- [ ] Bereits vorhandenes `DamagePlayer.cpp` als Vorlage verwenden ✅
- [ ] **Test:** 2 Spieler treffen Gegner gleichzeitig → HP sinkt nur einmal

---

### Woche 5 – Türen & Clear-Flags

**Ziel:** Türen öffnen sich für alle wenn der Raum geleert ist. Jeder getötete Gegner zählt für alle.

- [ ] **`SetClearFlag.cpp/.h` (NEU)** – Paket-Typ `SET_CLEAR_FLAG`:
  ```cpp
  struct SetClearFlag {
      s16 sceneNum;
      s8  roomNum;
  };
  ```
- [ ] Server-Logik: `OnEnemyDefeat` Hook → prüft ob alle Gegner im Raum tot
  - Wenn ja: `SET_CLEAR_FLAG` an alle senden
- [ ] Clients: `Flags_SetClear()` aufrufen → DoorShutter öffnet sich automatisch
- [ ] Erweiterung von bestehendem `SetFlag.cpp` möglich ✅ (gleicher Mechanismus)
- [ ] **Wichtig:** Wenn Spieler A einen Gegner tötet → alle Clients entfernen diesen Gegner (`ACTOR_KILLED`)
- [ ] **Test:** Raum leeren → Tür geht bei allen Clients auf

---

### Woche 6 – Szenen-Transitionen synchronisieren

**Ziel:** Szenenwechsel eines Spielers wird koordiniert – alle landen im gleichen Dungeon.

- [ ] **`SceneTransition.cpp/.h` (NEU)** – Paket-Typ `SCENE_TRANSITION_REQUEST`:
  ```cpp
  struct SceneTransitionRequest {
      s32      entranceIndex;   // einheitlich für alle
      uint32_t clientId;
  };
  ```
- [ ] **Phase A (einfach):** Alle Spieler werden zusammen teleportiert
  - Bestehende `TeleportTo.cpp`/`SendPacket_TeleportTo` als Vorlage ✅
  - `entranceIndex` ist bereits in `AnchorClient` gespeichert ✅
- [ ] Server entscheidet welchen `entranceIndex` alle nutzen
- [ ] **Phase B (später):** Spieler können in verschiedenen Szenen sein (komplexer, separates Ticket)
- [ ] **Test:** Spieler A betritt Deku Tree → Spieler B folgt automatisch

---

### Woche 7 – Bosse & Story-Events

**Ziel:** Boss-HP synchron, Boss-Türen öffnen sich, Cutscene-Trigger koordiniert.

- [ ] Boss-HP wie normaler Actor über `ACTOR_STATE_UPDATE` – höhere Senderate (60Hz)
- [ ] Boss-Türen bereits über `SetFlag`/`UnsetFlag` abgedeckt ✅
- [ ] Cutscene-Trigger: nur der "Szenen-Owner"-Client triggert lokal
  - Anderen Clients empfangen Event-Flag → spielen Cutscene lokal ab
  - Server schickt `SET_FLAG` (EVENT_CHECK_INF) → schon vorhanden ✅
- [ ] Boss-spezifische State-Daten (Phase, besondere Aktionen) in `ACTOR_STATE_UPDATE` ergänzen
- [ ] **Test:** Gohma/Ganondorf-HP sinkt bei beiden Clients synchron

---

### Woche 8 – Stabilisierung & Debug-Tools

**Ziel:** Das System ist spielbar und robust.

- [ ] **Desync-Detection:** Server vergleicht periodisch Flag-Checksummen mit allen Clients
- [ ] **Reconnect-Handling:** Client trennt Verbindung → kann rejoinen → Server schickt aktuellen State
  - Ähnlich wie bestehendes `RequestTeamState.cpp` / `AllClientState.cpp` ✅
- [ ] **Debug-Overlay:** Server-Actor-IDs im bestehenden `actorViewer.cpp` (debugger/) anzeigen
- [ ] **Latenz-Kompensation:** Positionen interpolieren (Basis bereits in `DummyPlayer.cpp` vorhanden ✅)
- [ ] Verbindungsabbruch-Tests (Client crash, Netz weg, Reconnect)
- [ ] Performance-Messung: Wie viele Actor-Updates pro Frame/Sekunde?
- [ ] **Test:** Kompletter Deku-Tree-Run zu zweit von Anfang bis Boss

---

## Neue Pakettypen – Übersicht

| Paket | Richtung | Woche | Status |
|---|---|---|---|
| `ACTOR_LIST_INIT` | Server → Clients | 2 | ⬜ TODO |
| `ACTOR_STATE_UPDATE` | Server → Clients (30Hz) | 3 | ⬜ TODO |
| `ACTOR_KILLED` | Server → Clients | 4 | ⬜ TODO |
| `PLAYER_ATTACK_ACTOR` | Client → Server | 4 | ⬜ TODO |
| `SET_CLEAR_FLAG` | Server → Clients | 5 | ⬜ TODO |
| `SCENE_TRANSITION_REQUEST` | Client → Server | 6 | ⬜ TODO |
| `HANDSHAKE` | beidseitig | 1 | ✅ vorhanden (erweitern) |
| `PLAYER_UPDATE` | Client → Server → Clients | — | ✅ vorhanden |
| `SET_FLAG` / `UNSET_FLAG` | Client → Server → Clients | — | ✅ vorhanden |
| `GIVE_ITEM` | Client → Server → Clients | — | ✅ vorhanden (anpassen) |
| `DAMAGE_PLAYER` | Server → Client | — | ✅ vorhanden (anpassen) |
| `UPDATE_TEAM_STATE` | Client → Server → Clients | — | ✅ vorhanden (anpassen) |
| `TELEPORT_TO` | Server → Clients | — | ✅ vorhanden |
| `REQUEST_TEAM_STATE` | Server → Client | — | ✅ vorhanden |
| `ALL_CLIENT_STATE` | Server → Clients | — | ✅ vorhanden |

---

## Reihenfolge – Womit anfangen?

```
1. Feature A (HP/Item-Count Toggle) – unabhängig, kein Risiko, sofort spielbar
   └── A.1 RoomState erweitern
   └── A.2 Button ins Menü
   └── A.3 GiveItem differenzieren
   └── A.4 DamagePlayer absichern
   └── A.5 UpdateTeamState anpassen

2. Feature B Woche 1 (AnchorServer) – Fundament für alles weitere
3. Feature B Woche 2 (ActorListInit) – wichtigster Schritt
4. Feature B Woche 3–8 in Reihenfolge
```
