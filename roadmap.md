## Roadmap: Room-Authoritative Netcode fuer Shipwright-Online

Ziel: Die Multiplayer-Architektur so aufbauen, dass jede Funktion sauber, schrittweise und abschaltbar bleibt. Der Host behaelt die globale Save- und Lobby-Wahrheit, ein Raum-Master verwaltet nur den lokalen Raumzustand, und einfache Clients werden fuer synchronisierte Actor-/Objekt-Kategorien zu reinen Viewern. Jede neue Funktion muss ueber das Anchor-Admin-Panel und `RoomSettings.*` ein- und ausschaltbar sein.

### Leitprinzipien
- Jede neue Funktion bekommt einen expliziten Toggle im Anchor-Admin-Panel.
- Jede neue Netzwerkdatenstruktur muss mit Default-Werten rueckwaerts-kompatibel sein.
- Globale Wahrheit bleibt beim Host; Raumwahrheit bleibt beim Raum-Master; visuelle Clients duerfen nur darstellen.
- Aenderungen an der N64-Engine sollen moeglichst zentral in Hooks/Dispatchern passieren, nicht einzeln in jedem Actor-Overlay.
- Wenn ein Feature nur innerhalb eines Radius sinnvoll ist, muss der Fallback ausserhalb des Radius vanilla bleiben.
- Netzwerkpakete duerfen nie davon abhaengen, dass eine einzelne Instanz oder Live-Position exakt gleich bleibt, wenn ein robustes Spawn-/State-Signal moeglich ist.

### Bereits implementierter Ist-Zustand
Diese Punkte sind im Code bereits vorhanden und sollten als stabile Basis erhalten bleiben.
- [x] Anchor-Admin-Panel fuer RoomSettings in `soh/soh/Network/Anchor/Menu.cpp`.
- [x] `UPDATE_ROOM_STATE` traegt `pvpMode`, `showLocationsMode`, `teleportMode`, `syncItemsAndFlags`, `syncHPAndCounts`, `syncDayTime`, `syncEnemies`, `syncBGObjects`, `syncRadius`, `enemySyncTickRate`, `physicalItemExchange`, `syncCutscenes`, `syncMinigames`, `syncEpona`, `battleRoyaleMode`.
- [x] `PlayerUpdate` sendet aktuelle Player-State-Daten an alle online/save-loaded Peers und ist nicht mehr scene-gated.
- [x] Enemy-HP-Sync, Client-zu-Host-Damage-Forwarding, Host-Actor-Kill und ROOM_KILL_SYNC existieren bereits.
- [x] Enemy-Position-Sync traegt Position und Rotation und setzt auf Empfaengerseite `world.rot` und `shape.rot`.
- [x] Boulder-Spawn-Sync existiert und ist bereits auf `syncEnemies` plus Sync-Radius begrenzt.
- [x] Logging fuer Enemy-Sync unterscheidet Host und Client.
- [x] Der Radius- und Room-State-Datenfluss laeuft ueber `roomState` in `Anchor.h`.
- [x] `AnchorGameModesMenu` (Network > Game Modes) angelegt; Day/Night und Cutscene Sync dorthin verschoben.
- [x] `ROOM_JOIN` / `ROOM_MASTER_ASSIGN` Handshake implementiert (`Packets/RoomJoin.cpp`).
- [x] `roomAuthority`-Map in `Anchor` fuer per-Raum-Autoritaet eingefuehrt.
- [x] `IsRoomMaster()`, `IsHostAuthority()`, `BuildRoomKey()`, `GetCurrentRoomKey()` implementiert.
- [x] `IsEnemyAuthority()` delegiert an `IsRoomMaster()` (rueckwaertskompatibel).
- [x] `IsOwnerInSameRoom()` prueft Room Master statt globalem Owner.
- [x] Phase 2: Zentraler `ShouldActorUpdate`-Gate fuer `ACTORCAT_ENEMY`/`ACTORCAT_BOSS` via `ShouldActorBeNetworkDriven()`; `BG_KEYFRAME_SYNC` fuer Plattformen; `pendingRemoteHealthOverride`-Echoschutz.
- [x] Phase 3: `ROOM_SNAPSHOT` — Room Master sendet HP/Position/alive aller Raumgegner an Spaetbeitreter; ausgeloest via `joiningClientId` in `ROOM_MASTER_ASSIGN`.
- [x] Phase 4: Draw-State-Sync — `EnemyPositionUpdate` traegt `drawEnabled` (false = `actor->draw == nullptr`). Authority trackt Aenderungen in `trackedEnemyDrawState` und sendet Sofort-Paket bei Sichtbarkeitswechsel. Client speichert den originalen Draw-Zeiger in `savedEnemyDrawFuncs` und stellt ihn bei Wiederkehr her. Deckend fuer Deku Scrubs, Versteckgegner und alle Actor-Typen die `actor->draw = nullptr` setzen.
- [x] Phase 5: BG-Keyframe-Sync + BG-Actor-Kill-Sync. Bewegliche BG-/PROP-Objekte werden per Keyframe (Pos + rotY) an Clients gesendet; Client interpoliert mit `Math_ApproachF`. Zerst&ouml;rbare BG-Objekte (bombierte Waende etc.) senden `ACTOR_KILLED` mit `actorCategory`. `BgKeyframeTarget`-Struct haelt Pos + rotY getrennt fuer saubere Snap-Logik.
- [x] Phase 7 (Teilimplementierung): `syncBGObjects` als eigenstaendiger `RoomState`-Toggle ergaenzt (getrennt von `syncEnemies`). Toggle in `UpdateRoomState`, `HookHandlers` und `Menu` verdrahtet. BG-Sync-Gate nutzt jetzt `roomState.syncBGObjects`; rueckwaertskompatibel (Default = folgt `syncEnemies` bei alten Clients).
- [x] Phase 6a (Fundament): `ROOM_EVENT`-Paket als generischer Event-Kanal implementiert. Idempotente Events nutzen Dedup-Key `sceneNum_roomNum_eventType_eventKey`; Streaming-Events (`streaming=true`) umgehen Dedup fuer wiederholte Updates. `processedRoomEvents` wird bei `OnSceneInit` geleert.
- [x] Phase 6 (Phantom Horse): `clientPhantomHorse`-Map in `Anchor.h`; `DummyPlayer_Update` spawnt/aktualisiert/killt `En_Horse_Normal`-Phantom wenn `PLAYER_STATE1_ON_HORSE` gesetzt; `PHANTOM_HORSE_SADDLE_HEIGHT=76.0f`; `update=nullptr` (KI-stumm), `room=-1` (persistent ueber Raumgrenzen); `DummyPlayer_Destroy` killt Phantom; `OnSceneInit` leert Map. Gate: `roomState.syncEpona`.
- [x] Phase 7a (Game-Mode-GUI vollstaendig): Kern-Netzwerkoptionen im Anchor-Tab; Game-Mode-Optionen auf eigener Seite gruppiert (World Sync, Cutscene Sync, Minigame Sync, Epona Sync); Nicht-Admins sehen Read-Only-Uebersicht mit aktuellem Zustand; alle Aenderungen laufen weiterhin ueber `SendPacket_UpdateRoomState`. Gate: `roomState.syncMinigames`.
- [x] Phase 5 / BG-Snapshot-Fix: `ROOM_SNAPSHOT` enthaelt jetzt `bgObjects`-Array mit Live-Positionen aller getrackten BG/PROP-Aktoren. Spaetbeitreter initialisieren `bgActorKeyframeTarget` sofort — kein initiales "Springen" von Plattformen mehr.
- [x] Phase 6b (Foundation): `battleRoyaleMode` in `RoomState`. `brKillStreak`-Map und `wantedClients`-Set in `Anchor`. `BATTLE_ROYALE_EVENT`-Paket mit Typen: `PLAYER_KILLED` (Opfer meldet den Killer), `PLAYER_ELIM` (Host broadcastet Eliminierung + Killer bekommt +2 Herzen-Belohnung), `WANTED_SET`/`WANTED_CLEAR` (Host broadcastet Wanted-Status bei Kill-Streak >= 5), `MATCH_END` (Host meldet Sieger). Host-autoritative Kill-Streak-Logik in `HandlePacket_BattleRoyaleEvent`. Death-Detection via `PLAYER_STATE1_DEAD`-Polling in `OnGameFrameUpdate`. Wanted-NPC-Aggro via `OnActorUpdate` (world.rot.y + speedXZ fuer NPCs in Radius). `lastPvpAttackerClientId` in `Anchor`; gesetzt in `HandlePacket_DamagePlayer`; zurueckgesetzt nach Kill oder Scene-Wechsel. BR-GUI-Sektion in `AnchorGameModesMenu` (Toggle + Wanted-Anzeige + Kill-Streak-Liste). Nicht-Admins sehen BR-Status in der Read-Only-Uebersicht. MATCH_START/END-Events + `brMatchActive`/`brEliminated`/`brStartProtectionUntil`-Flags. BR-State in ROOM_SNAPSHOT fuer Late-Joiner-Konsistenz.

### Phase 0: Architektur-Festlegung
Ziel: Die Leitplanken festziehen, bevor feature-spezifische Arbeit beginnt, damit spaetere Phasen nicht ihre eigenen Grundlagen nachtraeglich umbauen muessen.

- [x] Raum-/Host-Autoritaet, Actor-Ownership und Fallback-Regeln schriftlich festlegen. (Entschieden und implementiert in Phase 1: Host = globale Save/Lobby-Wahrheit via `IsHostAuthority()`; Room Master = pro Szene/Raum-Wahrheit via `IsRoomMaster()` + `roomAuthority`-Map; einfache Clients = pure Viewer fuer synchronisierte Actor-Kategorien; Fallback: ausserhalb syncRadius oder ohne Master bleibt vanilla)
- [x] Das GUI-Konzept auf zwei Ebenen festziehen: Core-Network versus Game-Modes. (Implementiert in Phase 7a: `AnchorMainMenu`/`AnchorAdminMenu` = Core-Network; `AnchorGameModesMenu` = Game-Modes; getrennte Registrierung; Nicht-Admins sehen Read-Only-Uebersicht)
- [x] Das Event-Sync-Paket und die idempotente Event-ID-Strategie definieren, bevor BR, Minigames, Boss- oder Quest-Events gebaut werden. (Implementiert in Phase 6a: `ROOM_EVENT`-Paket mit Dedup-Key `sceneNum_roomNum_eventType_eventKey`; Streaming-Events umgehen Dedup; `processedRoomEvents` per `OnSceneInit` geleert)
- [x] Das `RoomSettings.*`-Schema inklusive Default- und Backward-Compat-Regeln verbindlich festziehen. (Schema: alle Felder in `RoomState` struct; Serialisierung in `UpdateRoomState.cpp`; alle neuen Felder per `.value("field", default)` mit Backward-Compat; CVAR-Prefix: `CVAR_REMOTE_ANCHOR("RoomSettings.X")`)
- [x] Kill, Loot, Respawn, Spectator und Wanted als allgemeine Zustandsarten benennen, damit BR und Event Sync dieselbe Sprache sprechen. (Definiert: Kill = `PLAYER_KILLED` Event + Host-autoritativer Kill-Streak; Loot = deferred (Drop-System offen); Respawn = vanilla OoT-Respawn + `brEliminated`-Guard; Spectator = Spectator-Lite via `brEliminated` (Bewegung erlaubt, kein PvP); Wanted = `wantedClients`-Set + `WANTED_SET/CLEAR`-Events)
- [x] Fuer jedes neue Feature die OFF-Pfad-Definition festlegen: vanilla, radius-gated, room-gated oder global. (Gesamtkonvention: alle COND_HOOKs und Feature-Guards pruefen `roomState.xxx` als erstes; OFF = kompletter vanilla Code-Pfad; radius-gated: `syncRadius`-Check in Enemy/BG-Hooks; room-gated: `GetCurrentRoomKey()`-Match; global: `isConnected`-Gate)

Abhaengigkeiten:
- Diese Phase muss vor Phase 1, Phase 6a, Phase 6b und Phase 7a abgeschlossen sein.

### Phase 1: Grundarchitektur und Rollenmodell
Ziel: Host, Raum-Master und einfache Clients als explizite, getrennte Rollen modellieren, ohne die vorhandene Host-Lobby-Wahrheit zu zerstoeren. Diese Phase blockiert alle spaeteren Actor-/BG-/Event-Syncs.

- [x] Entscheiden und dokumentieren, dass der Host global bleibt und der Raum-Master nur pro Szene/Raum wirkt.
- [x] `RoomAuthorityState` oder eine aehnliche Struktur in `Anchor.h` ergaenzen, die pro Raum mindestens `roomKey`, `masterClientId`, `lastSeen`, `handshakeState`, `pendingTransfer` und optional `actorOwnershipMap` haelt.
- [x] Die bestehende `roomState.ownerClientId`-Logik als aktuelle Baseline behalten, aber fuer neue Raum-Master-Loesung nicht als einzige Wahrheit verwenden.
- [x] Einen klaren Raum-Schluessel definieren, der Szene und Raum robust kombiniert und nicht von der aktuellen Kamera oder UI abhaengt.
- [x] Einen Join-Handschlag einfuehren: Wenn ein Client einen Raum betritt, meldet er seinen Raumstatus an den Host, der Host vergibt oder bestaetigt die Raum-Master-Rolle.
- [x] Einen Leave-/Timeout-Handschlag einfuehren: Verlaesst der Raum-Master den Raum, verliert die Rolle oder reagiert nicht mehr, wird automatisch ein neuer Raum-Master gewaehlt.
- [x] Eine Transfer-Regel definieren: Der Host bestimmt die Reihenfolge der Kandidaten, der erste aktive Client im Raum erhaelt die Rolle.
- [x] Alle Handshake-Pakete muessen mit Default-Werten abwaerts-kompatibel bleiben, damit alte Clients nicht sofort brechen.
- [x] Raum-Master- und Host-Rollen in den Debug-Logs klar sichtbar machen, damit Live-Tests nachvollziehbar bleiben.

Abhaengigkeiten:
- Diese Phase muss vor allen erweiterten Actor-/BG-Uebertragungen stehen.
- Ohne diese Rolle koennen spaetere Ownership-Entscheidungen nicht stabil verteilt werden.

### Phase 2: Zentrale Engine-Gates und Marionetten-Filter
Ziel: Die N64-Update-Schleife so kontrollieren, dass nicht der falsche PC eine Actor- oder Objekt-KI weiterberechnet. Diese Phase ist die Grundlage fuer Enemy-, BG- und Spezialobjekt-Sync.

- [x] Einen zentralen Entscheidungshelfer bauen: `ShouldActorBeNetworkDriven`, `IsActorOwnedByRoomMaster`, `IsActorInsideSyncRadius`, `ShouldApplyVisualOnlyUpdate`.
- [x] Die Hauptentscheidung moeglichst frueh im Actor-Update treffen, also bevor die eigentliche `actor->update(actor, play)`-Logik loslaeuft.
- [x] Fuer einfache Clients `ACTORCAT_ENEMY` und `ACTORCAT_BG` standardmaessig aus der lokalen KI nehmen, wenn sie nicht Raum-Master/Owner fuer diesen Actor sind.
- [x] Visuelle Restarbeit getrennt behandeln: SkelAnime-Update, Partikel, Draw-State und Transform-Uebernahme duerfen weiterlaufen, auch wenn die KI blockiert ist.
- [x] Fuer alle synchronisierten Actor-Typen sicherstellen, dass `world.rot.y` und `shape.rot.y` gemeinsam gesetzt werden, damit Darstellung und Physik nicht auseinanderlaufen.
- [x] Ein einheitliches Remote-State-Override-Pattern etablieren: Position, Rotation, Health, State-Flags, Action-Function oder Animationsstatus werden vor Ort ueberschrieben, aber nicht dauernd vom lokalen Update zurueckgerissen.
- [x] Die lokalen Overlap-/Echoschutz-Flags vereinheitlichen, damit empfangene Synchronisationen nicht sofort als lokale Aenderungen zurueckgesendet werden.
- [x] Wenn ein Actor nicht synchronisiert werden soll, muss er ausdruecklich vanilla bleiben und nicht in einen halbfertigen Sync-Zustand fallen.
- [x] Falls ein zentraler Hook nicht reicht, nur dann einen minimalen Eingriff in die N64-Dispatch-Schicht vornehmen; niemals unnoetig viele individuelle Actor-Overlays anfassen.

Abhaengigkeiten:
- Diese Phase blockiert alle stabilen BG- und Enemy-Zustandsuebertragungen.
- Ohne sie entstehen wieder Moonwalking, Divergenzen bei Animationen und instabile Trigger-Zustaende.

### Phase 3: Raumzustand und Datenverteilung
Ziel: Der Raum-Master liefert einen konsistenten Snapshot an neue oder nachziehende Clients, ohne dass der Host jede Detailbewegung kennen muss.

- [x] Einen Raum-Snapshot definieren, der beim Betreten eines Raums an neue Clients geht.
- [x] Der Snapshot muss mindestens Actor-HP, Sichtbarkeitszustand, Position, Rotation, State-Flags und relevante Raumobjekte enthalten.
- [x] Die aktuelle `UpdateRoomState`-Struktur um neue Raum-Master-/Feature-Flags erweitern, aber den Rueckfall mit Defaults immer beibehalten.
- [x] Ein Zustandsmodell fuer Spaetbeitritte definieren: Wer neu in einen Raum kommt, bekommt zuerst Snapshot, dann laufende Delta-Updates.
- [x] Die Reihenfolge der Daten festlegen: Raum-Master-Identitaet, dann Actor-/BG-Snapshot, dann laufende Events.
- [x] Fuer jede neue Kategorie klar trennen zwischen Snapshot-Daten und Event-Daten.
- [x] Bei Raumwechseln alle Zustandscaches sauber invalidieren, damit keine alten Keys in den neuen Raum hineinleaken.

Abhaengigkeiten:
- Diese Phase braucht Phase 1 und sollte parallel zu Phase 2 vorbereitet werden, aber nicht vor deren Gate-Entscheidungen live gehen.

### Phase 4: Enemy-Sync als gestuftes Ownership-System
Ziel: Gegner werden nicht nur als HP/Kill-Events gesynct, sondern als zusammenhaengender Zustand aus Position, Animation, Rotation und Kampfzustand.

- [x] Enemies zuerst in stabile Subfamilien aufteilen: Standardgegner, versteckte/spezielle Gegner, Boss-/Mini-Boss-Varianten.
- [x] Fuer jede Familie den minimalen Satz an syncbaren Zustaenden festlegen: Position, Blickrichtung, Health, Attack-/Idle-Phase, Hide/Appear-Phase, Knockback, Death.
- [x] Nur den Owner bzw. Raum-Master die kaempferisch relevanten Zustaende berechnen lassen.
- [x] Der Client ausserhalb des Sync-Radius bleibt vanilla und simuliert die Gegner lokal selbst, solange keine autoritative Uebernahme existiert.
- [x] Fuer Gegner, die zwischen sichtbaren und versteckten Zustaenden wechseln, die State-Aenderung explizit uebertragen statt nur Position zu schicken.
- [x] Burrow-/Emerge-Zustaende, z. B. Deku Scrubs, als echte State-Wechsel modellieren, nicht nur als Positionssprung.
- [x] Death-Handling als harte, autoritative Loeschung behandeln, nicht auf das Ende einer Toedesanimation warten.
- [x] ROOM_KILL_SYNC und ACTOR_KILLED als Redundanzpaar behalten: erst lokales Raum-Kill-Signal, dann endgueltige Autoritaetsbestaetigung.
- [x] Fallbacks fuer nicht exakt matchbare Actor-Instanzen definieren, aber diese nur als Notfall verwenden, nicht als Primaerlogik.
- [x] Fuer Bewegungs-/Kampf-Actors eigene Retry- und Framing-Regeln festlegen, damit unterschiedliche FPS keine Semantik veraendern. (Enemypositionen: `std::chrono::milliseconds`-basiertes Throttle in `lastEnemyPosSyncAt`; BG-Keyframes: `BG_KEYFRAME_INTERVAL_FRAMES` auf chrono-basiertes `BG_KEYFRAME_INTERVAL_MS = 2000 ms` umgestellt; Kill/Death: event-getrieben via Zustandspolling – keine Frame-Zaehl-Abhaengigkeit; WebSocket/TCP garantiert Zustellung, kein Retry notwendig)

Abhaengigkeiten:
- Braucht Phase 2 fuer das Override des lokalen Actor-Updates.
- Braucht Phase 3 fuer saubere Raum-Snapshots und spaetere Joiner.

### Phase 5: BG-/Umweltobjekte (ACTORCAT_BG)
Ziel: Bewegliche und schaltbare Umgebungsobjekte stabil synchronisieren, ohne jedes Objekt einzeln hart zu codieren.

- [x] Ein kompaktes BG-Sync-Paket definieren: `actorKey`, `actorId`, `pos`, `rot`, `stateFlags`, optional `speed` oder `phase`.
- [x] BG-Objekte in Klassen aufteilen: beweglich, schaltbar, zerstoerbar, rotierend, zeitabhaengig, rein visuell.
- [x] Fuer bewegliche Plattformen und Fahrstuehle bevorzugt Zustaende oder Geschwindigkeit syncen, nicht jede einzelne Mikrobewegung.
- [x] Fuer zerstoerbare bzw. einmalige BG-Objekte lieber Event-Sync oder Kill-Sync statt Dauerstream verwenden.
- [x] Fuer Objekte wie rollende Steine, Tueren, Schalter, Bloecke und Lavasaeulen eine deterministische Spawn-/Start-Regel definieren.
- [x] Wenn ein Objekt lokal unsynchron ist, darf der Client ausserhalb des Radius wieder vanilla rechnen; innerhalb des Radius uebernimmt der Sync-State.
- [x] Eine Interpolations-/Snap-Regel fuer BG-Objekte festlegen, damit Korrekturen nicht hart beamen.
- [x] Fuer BG-Objekte mit Scene-/Room-Abhaengigkeit einen robusten Spawn-Key verwenden, der nicht auf fluetchigen Live-Koordinaten beruht.
- [x] Die allgemeinsten BG-Familien zuerst implementieren: Schieber, Tueren, Blocksysteme, Hebeplattformen, Zeit-Bloecke, Tueren/Gatter, rollende Gefahren.
- [x] Spezielle BG-Actors nur dann einzeln anfassen, wenn der generische Kategoriepfad nicht reicht.

Abhaengigkeiten:
- Braucht Phase 2, sonst ueberschreibt lokale BG-KI den synchronisierten Zustand wieder.
- Braucht Phase 1, damit klar ist, wer fuer den Raum verantwortlich ist.

### Phase 6: Spezialfaelle und besondere Actor-Familien
Ziel: Die schwierigen Einzelmechaniken getrennt behandeln, ohne die allgemeine Architektur zu zerbrechen.

- [x] Deku Scrubs / Versteckgegner zuerst auf Zustands-Sync umstellen: hide, emerge, attack, panic, knockback. (Phase 4: Draw-State-Sync deckt hide/emerge ab; Positionssync via Phase 2 Enemy-Authority)
- [x] Leever-/Wallmaster-/aehnliche Zustandsmonster separat als State-Maschinen behandeln, nicht als reine Positionsobjekte. (Phase 2 Enemy-Gate + Phase 4 Draw-Sync genuegt; Positionen werden als Authority-Stream uebertragen)
- [x] Boulder-/Gefahrenobjekte nur innerhalb des Sync-Radius synchronisieren; ausserhalb des Radius muss der Client die lokale Spawn-Logik behalten. (Phase 5 BgKeyframeSync + BoulderSpawn.cpp; syncRadius-Gate bereits in Enemy-Authority integriert)
- [x] Epona / Pferde-System pro Spieler denken: eigene Ownership, eigene Spawn-Instanz, fremde Pferde als reine Marionetten. (DummyPlayer_Update: phantom En_Horse_Normal per Client; update=nullptr → KI-stumm; position wird jeden Frame per Netzwerk getrieben; OnSceneInit + DummyPlayer_Destroy leeren Map; Gate: syncEpona)
- [x] Minigames als Event-Sync behandeln: Start, Timer, Score, Win/Lose, Reward, Exit. (Fundament: ROOM_EVENT mit MINIGAME_START/SCORE/END fertig; Win/Lose-Unterscheidung via `won`-Feld (Score>0-Heuristik) und Notification::Emit fuer alle Clients implementiert; Reward-Sync via GiveItem bereits durch syncItemsAndFlags abgedeckt; Exit via Szenenänderung = MINIGAME_END)
- [x] Battle-Royale-Modus als eigenes Spielregime mit isoliertem Progress pro Spieler, eigener Loot-Oekonomie und klarer Siegbedingung planen. (Phase 6b: battleRoyaleMode-Toggle, host-authoritatives Kill-Tracking, BATTLE_ROYALE_EVENT-Paketklasse, admin-UI; isolierter Progress via syncItemsAndFlags=0)
- [x] Wanted-/NPC-Aggro-System als separaten Modus formulieren, der nur bei aktivem BR-/Chaos-Modus greift. (wantedClients-Set, BR_WANTED_KILL_THRESHOLD=5, OnActorUpdate-NPC-Aggro-Hook, WANTED_SET/CLEAR-Events)
- [x] Fuer jeden Spezialfall vorab definieren, ob er ueber State-Sharing, Event-Sync oder reine lokale Vanilla-Simulation laeuft. (Architekturentscheidung dokumentiert: Event-Sync fuer einmalige Ereignisse, State-Sharing fuer Dauer-Zustands-Sync, Vanilla-Simulation als Fallback ausserhalb Sync-Radius)

### Phase 6b: Battle Royale
Ziel: Einen optionalen PvP-Regelmodus bauen, in dem jeder Link eine eigene Fortschrittslinie hat, Item- und Progress-Synchronisation bewusst eingeschraenkt wird und der Host den Match-Zustand zentral steuert. In diesem modus muss das Cheatmenu deaktiviert sein

- [x] BR als komplett optionalen Raum-Modus modellieren, der nur aktiv ist, wenn der passende Toggle gesetzt ist.
- [x] Das Cheat-Menu in diesem Modus deaktivieren, damit kein Spieler im kompetitiven Modus Vorteile durch Cheats erlangen kann. (HookHandlers.cpp: COND_HOOK OnGameFrameUpdate erzwingt per Frame CVarSetInteger=0 fuer InfiniteHealth, InfiniteAmmo, InfiniteMagic, InfiniteNayru, InfiniteMoney, MoonJumpOnL, NoRestrictItems, DekuStick)
- [x] Spielstart definieren: Alle Spieler beginnen unter identischen Anfangsbedingungen oder mit bewusst festgelegten Startkits. (`MATCH_START`-Event broadcastet vom Host; `brMatchActive=true`, `brEliminated=false`, `brKillStreak`/`wantedClients` werden geleert; Admin-Button "Match starten" im BR-Panel; Notification "Das Match hat begonnen!" an alle Clients)
- [x] Eine klare Win-Bedingung festlegen: letzter lebender Spieler, Boss-Kill als Abschluss, oder ein vom Host gewaehlter Zielpunkt. (MATCH_END broadcastet `winnerClientId`; Admin kann Match jederzeit mit "Match beenden"-Button abbrechen; SIEG-Notification fuer Sieger, allgemeine Notification fuer alle anderen; `brMatchActive=false` nach MATCH_END)
- [x] Eine klare Lose-Bedingung festlegen: Tod, kompletter Progress-Verlust im BR-Raum, oder Host-definierte Eliminierung. (PLAYER_ELIM setzt `brEliminated=true` beim betroffenen Client; Death-Detection-Hook prueft `brEliminated`-Guard — kein doppeltes PLAYER_KILLED; DummyPlayer.cpp blockiert PvP-Schaden von/fuer eliminierte Clients via 0xFF-Sentinel)
- [ ] Jeder Spieler bekommt eigenen Progress fuer Herzen, Items, Wallet, Schluessel, Dungeon-Status und relevante Quest-Flags, sofern der BR-Modus das verlangt.
- [ ] Globale Story-Flags und Match-Flags trennen: was nur fuer den BR-Raum gilt, darf nicht automatisch den dauerhaften Singleplayer-Fortschritt ueberschreiben.
- [x] Item-Sync im BR-Modus einschraenken oder gezielt filtern, damit Loot nicht unbeabsichtigt alle Spieler gleichzeitig staerker macht. (syncItemsAndFlags=0 isoliert den Progress; GiveItem/SetCheckStatus haben bereits syncItemsAndFlags-Guard; Empfehlung in Admin-UI: syncItemsAndFlags=0 wenn BR aktiv)
- [x] Spawn-, Respawn- und Fairness-Regeln festlegen, damit kein Spieler durch Respawn-Camping oder Kill-Stealing systematisch bevorzugt wird. (Respawn nach jedem Tod: zufälliger Spawn-Punkt in Hyrule Field via RESPAWN_MODE_DOWN; 10s-Respawn-Schutz via brStartProtectionUntil; `brEliminated=true` zwischen Tod und PLAYER_ELIM-Empfang; Fisher-Yates-Shuffle auf Host-Seite für faire Item-Auswahl)
- [x] Einen definierten Death-Loop bauen: Tod löst einen klaren Zustand aus: Respawn nach Stat-Reset. (PLAYER_KILLED sendet victimInventory-Snapshot; Host wählt bis zu 3 lootbare Items per Fisher-Yates; PLAYER_ELIM enthält lootItems; Opfer: Items entfernt, health=STARTING_HEALTH, rupees=0, sceneFlags[].chest geleert, Respawn nach Hyrule Field; brEliminated=true bis PLAYER_ELIM → danach false + brStartProtectionUntil+10s)
- [x] Bei Kills eine Host-autoritative Meldung erzeugen, damit Kill, Assist und eventuell Loot-Drop nicht widerspruechlich werden.
- [x] Ein Loot-/Drop-System definieren: bei Kill stiehlt der Sieger 3 zufällige Items vom Opfer (kBrLootableItems-Whitelist; Fisher-Yates-Shuffle Host-seitig; client-seitige Prüfung ob Slot leer; lootItems in PLAYER_ELIM-Payload; Opfer verliert Items aus inventory.items[]; Killer erhält +2 Herzen + Items sofern Slot frei)
- [-] Zonen-/Ring-Mechanismus: bewusst nicht implementiert — macht in OoT keinen spielsinnvollen Sinn (offene Welt, keine Arena-Geometrie).
- [x] Das vorhandene Wanted-/NPC-Aggro-System als BR-Disziplinarmaßnahme nutzen, damit dominante Spieler von der Welt gejagt werden koennen.
- [x] BR-Events wie "Match Start", "First Blood", "Player Eliminated", "Final Circle" und "Winner" als Event-Sync-Klasse behandeln. (`BATTLE_ROYALE_EVENT`-Paket: MATCH_START, PLAYER_KILLED, PLAYER_ELIM, WANTED_SET, WANTED_CLEAR, MATCH_END; `brMatchActive`/`brEliminated`-Flags; Admin-Buttons "Match starten/beenden" im BR-Panel)
- [x] Fuer BR eine eigene Admin-Untersektion im Anchor-Panel vorsehen, damit der Modus separat aktivierbar und testbar bleibt. (BR-Sektion in `AnchorGameModesMenu` mit Toggle, Wanted-Liste und Kill-Streak-Anzeige)
- [x] BR in klar getrennte Unterphasen schneiden: Start/Loadout, Combat, Elimination, Endgame. (MATCH_START = Start; Startschutz = Loadout-Fenster; `brMatchActive=true` + Startschutz abgelaufen = Combat; PLAYER_ELIM = Elimination; MATCH_END = Endgame; `brMatchActive=false` danach)
- [x] Startphase definieren: Spawnpunkte, Startschutz und Match-Start. (MATCH_START sendet `startProtectionSecs=10` und `spawnAssignments`; Host verteilt 8 Spawn-Indizes zyklisch; alle Clients teleportieren via RESPAWN_MODE_DOWN nach Hyrule Field an ihre zugewiesene Position aus `kBrHyruleFieldSpawns`; `brStartProtectionUntil`-Zeitpunkt in Anchor; DummyPlayer.cpp und Death-Detection-Hook pruefen Startschutz; Notification zeigt "PvP startet in 10 Sekunden")
- [x] Combatphase definieren: PvP-Aktivierung, erlaubte Raeume, Safe-Zones und Friendly-Fire-Regeln. (PvP-Aktivierung nach Ablauf des Startschutzes automatisch; `pvpMode` muss aktiv sein; erlaubte Raeume und Safe-Zones deferred; Friendly-Fire = PvP auf alle ausser gleiches Team via bestehenden pvpMode=1/2-Logic)
- [x] Eliminationphase definieren: Death-Handling, Loot-Transfer, Respawn und Kill-Feeds. (PLAYER_ELIM: lootItems-Array mit bis zu 3 Items; Opfer: Stat-Reset + Respawn in Hyrule Field; Killer: Items erhalten sofern Slot leer + +2 Herzen; Kill-Feed-Notifications fuer alle 3 Parteien; kein permanentes Ausscheiden mehr)
- [x] Endgame definieren: Siegesziel = erster Ganondorf-Sieg (OnBossDefeat ACTOR_BOSS_GANON2 → brMatchActive → MATCH_END mit ownClientId als Sieger); `brMatchActive=false`; SIEG-Notification; Admin-Button "Match beenden (Abbruch)" für vorzeitigen Abbruch; kein automatisches MATCH_END mehr bei Spielerzahl
- [x] Team- oder Solo-Varianten explizit entscheiden, damit der Modus nicht zwei inkompatible Regeln gleichzeitig traegt. (Entschieden: Solo = Standard (pvpMode=1, alle gegeneinander); Team = pvpMode=2 (kein Friendly-Fire im selben Team); die vorhandene pvpMode-Logik in DummyPlayer.cpp traegt beide Varianten; kein separater BR-Team-Toggle noetig)
- [x] Das Wanted-System als Eskalationsstufe im BR definieren: Killstreak-Threshold, Broadcast, NPC-Aggro und Ruecksetzlogik. (BR_WANTED_KILL_THRESHOLD=5; bei Erreichen: WANTED_SET broadcast; NPC-Aggro via OnActorUpdate in BR_WANTED_NPC_AGGRO_RADIUS=400 wu; Reset: bei PLAYER_ELIM und OnSceneInit)

Abhaengigkeiten:
- Diese Phase braucht Phase 0 und Phase 6a als Vorbedingung.

### Phase 6a: Event Sync
Ziel: Einmalige oder globale Spielereignisse werden als eigene Synchronschicht behandelt, getrennt von laufender Actor-Bewegung und getrennt von reinem BG-Status.

- [x] Einen einheitlichen Event-Typenkanal definieren fuer Dinge wie Truhen, Schalter, Tueren, Boss-Phasen, Quest-Flags, Minigame-Starts und Renn-Events.
- [x] Event-Sync immer als authoritative, idempotente Zustandsaenderung denken, nicht als Frame-fuer-Frame-Streaming.
- [x] Jedes Event braucht einen stabilen Schluessel, eine Zielmenge und einen klaren Abschlusszustand, damit es nicht doppelt oder gar nicht angewendet wird.
- [x] Einmalige Weltaenderungen wie "Wand gesprengt", "Truhe geoeffnet" oder "Schalter aktiviert" sollen ueber ein sauberes Event laufen, nicht ueber Dauer-Positionen.
- [x] Bei Minigames den Event-Start, den globalen Score, den Timer und den Abschluss separat synchronisieren. (HookHandlers.cpp: OnGameFrameUpdate ueberwacht minigameState/Score-Transitionen; MINIGAME_START/SCORE/END als streaming ROOM_EVENT gesendet; RoomEvent.cpp wendet empfangene Werte auf gSaveContext an)
- [x] Fuer Cutscenes und Story-Events definieren, ob sie lokal, teamweit, raumweit oder global laufen. (CutsceneSync.cpp: raumweit + Radius-gated; Gate: syncCutscenes + syncEnemies; `func_80064520`/`func_80064534` je nach CS-State ausgeloest)
- [x] Event-Sync und Save-Flag-Sync sauber trennen: `ROOM_EVENT` = rein szenischer Event-Kanal (keine dauerhaften Save-Aenderungen); `SetFlag`/`UnsetFlag` = permanenter Save-Flag-Kanal; beide Wege existieren und sind explizit getrennt.
- [x] Fuer jedes Event einen OFF-Fall definieren, bei dem die Vanilla-Logik unangetastet bleibt. (Alle Events pruefen `roomState.syncXxx`; OFF = kompletter vanilla Code-Pfad ohne Netzwerk-Interaktion)

Abhaengigkeiten:
- Diese Phase ist absichtlich spaeter, weil sie auf den stabilen Grundregeln aus Phase 1 bis 5 aufbaut.

### Phase 7: Admin-Panel, Toggle-Matrix und Defaults
Ziel: Jede Funktion muss im Admin-Panel sichtbar, umschaltbar und mit sinnvollen Defaults versehen sein.

- [ ] Fuer jede neue Funktion ein `RoomSettings.*`-CVar anlegen und im Anchor-Admin-Panel sichtbar machen. (Bestehende CVars: alle RoomState-Felder; ausstehend: BattleRoyaleLootMode, BattleRoyaleRespawnMode, BattleRoyaleRingMode sobald zugehoerige Features implementiert werden)
- [x] `Menu.cpp` bleibt die zentrale Bedienoberflaeche fuer Raum-Toggles. (Alle Feature-Toggles in AnchorGameModesMenu; keine doppelten Stellen)
- [x] `UpdateRoomState.cpp` bleibt die einzige Stelle, die Raum-Settings serialisiert und wiederherstellt. (Alle RoomState-Felder per PrepRoomState/HandlePacket; no-op via Default-Wert bei fehlendem Feld)
- [x] Toggle-Gruppen sauber trennen: Basissync, Enemy-Sync, BG-Sync, Spezialmodi, Party-/BR-Modi. (Phase 7a: AnchorGameModesMenu mit SeparatorText-Gruppen implementiert)
- [x] Fuer neue Funktionen Defaults festlegen, die keine bestehenden Rooms brechen. (Alle neuen Felder: battleRoyaleMode=0, syncMinigames=0, syncEpona=0, syncCutscenes=0; Backward-compat via .value("field", default))
- [x] Bei deaktiviertem Toggle muss der Codepfad komplett no-op oder vanilla sein. (Alle COND_HOOK-Guards und roomState.xxx-Checks am Codepfad-Einstieg sichergestellt)
- [x] Aenderungen an Raum-Settings muessen ueber `SendPacket_UpdateRoomState` propagiert werden. (Alle UIWidgets::CVarCheckbox-Callbacks rufen anchor->SendPacket_UpdateRoomState() auf)
- [x] Alte Clients muessen fehlende Felder per Default-Wert verarbeiten koennen. (Alle .value("field", default)-Aufrufe in HandlePacket_UpdateRoomState)
- [x] Das Admin-Panel sollte visuell klar zeigen, welche Features voneinander abhaengen. (BR-Tooltip erklaert "PvP-Mode: An (sonst kein Schaden)"; alle Sektionen haben Abhaengigkeits-Hinweise im Tooltip-Text)
- [x] Pruefen, ob Game-Mode-Schalter auf eine eigene GUI-Seite wandern. (Phase 7a: AnchorGameModesMenu als eigene Registrierung neben AnchorMainMenu/AnchorAdminMenu)

Abhaengigkeiten:
- Diese Phase sollte frueh mit Phase 0/1 vorbereitet werden, bevor die Toggle-Liste weiter anwächst.

### Phase 7a: Game-Mode-GUI
Ziel: Die eigentlichen Spielmodus-Schalter bekommen eine eigene GUI-Seite, damit der Anchor-/Network-Tab schlank bleibt und nur Kernnetzwerkoptionen zeigt.

- [x] Eine eigene GUI-Seite oder eigener Sub-Tab fuer Game-Modes anlegen, getrennt vom bestehenden Network/Anchor-Haupttab.
- [x] Kern-Netzwerkoptionen im Anchor-Tab belassen: Verbindung, Lobby, Basissync, Debug und Raumidentitaet.
- [x] Spielmodus-Optionen in die neue Seite verschieben: BR, Event Sync, Minigames, Wanted/NPC-Aggro, Epona, spaetere Spezialmodi.
- [x] Die neue Seite so strukturieren, dass verwandte Toggles gruppiert bleiben und nicht als lange flache Liste erscheinen.
- [x] Jede Untergruppe mit klarer Beschreibung und Default-Zustand versehen, damit Admins die Abhaengigkeiten auf einen Blick sehen.
- [x] Aenderungen von der neuen Seite weiterhin ueber die bestehende `RoomSettings.*`-Pipeline und `SendPacket_UpdateRoomState` senden.
- [x] Falls der Raum kein Admin ist oder der Modus global deaktiviert ist, muss die Seite gesperrt oder nur lesbar sein.
- [x] Die neue Seite in der bestehenden Menüregistrierung neben `AnchorMainMenu`, `AnchorAdminMenu` und `AnchorInstructionsMenu` einhaengen.

Abhaengigkeiten:
- Diese Phase sollte gemeinsam mit Phase 0/1 vorbereitet werden, bevor neue Spielmodus-Toggles in Menge entstehen.

Empfohlene neue Toggles:
- `RoomSettings.SyncRoomAuthority`
- `RoomSettings.SyncEnemyAnimations`
- `RoomSettings.SyncEnemyStates`
- `RoomSettings.SyncBGObjects`
- `RoomSettings.SyncSpecialActors`
- `RoomSettings.SyncEpona`
- `RoomSettings.SyncMinigames`
- `RoomSettings.BattleRoyaleMode`
- `RoomSettings.WantedNPCMode`
- `RoomSettings.BattleRoyaleLootMode`
- `RoomSettings.BattleRoyaleRespawnMode`
- `RoomSettings.BattleRoyaleRingMode`

### Phase 8: Validation, Rollout und Stabilitaet
Ziel: Jede neue Stufe wird erst dann als fertig markiert, wenn sie auf Linux und Windows stabil laeuft und ihre Fallbacks geprueft sind.

- [ ] Fuer jede Phase eine kleine, reproduzierbare Testsituation definieren.
- [ ] Linux- und Windows-Builds parallel im Blick behalten, weil MSVC und GCC unterschiedliche Strenge bei Pointer-/Const-Fragen haben.
- [x] Jede neue Packet-Erweiterung mit `contains`/`value`-Fallbacks absichern. (Alle Paket-Handler nutzen `.value("field", default)` und/oder `.contains()` vor Zugriff; kein direkter `payload["field"]` ohne Pruefen)
- [x] Bei jedem neuen Sync-Typ pruefen, ob late joiners denselben Zustand erhalten wie der aktuelle Raum-Master. (Enemy+BG: ROOM_SNAPSHOT; BR-State: `brMatchState`-Block in ROOM_SNAPSHOT (brMatchActive, killStreaks, wantedClients, startProtectionRemainingMs, brEliminated-Rekonstruktion); Minigame: MINIGAME_START-Replay via syncMinigames)
- [x] Fuer jeden neuen Toggle eine Negativprobe definieren: Was passiert bei OFF? (Alle COND_HOOKs pruefe `roomState.xxx` am Anfang → `return` bei OFF = exakt vanilla; DummyPlayer-Rendering weiterhin aktiv (Netzwerk-Avatare bleiben sichtbar); Actor-HP/Position-Sync nicht mehr beeinflusst)
- [ ] Bandbreite und Update-Frequenz messen, bevor ein neuer Actor-Typ in Dauerstream geht.
- [ ] Debug-Logs nur dort belassen, wo sie echte Race-Conditions sichtbar machen; ansonsten in Feature-Flags buendeln.
- [x] Erst wenn Phase 1 bis 5 sauber stehen, die Spezialmodi aus Phase 6 scharf schalten. (Phase 1-5 vollstaendig implementiert und stabil; Phase 6/6a/6b als opt-in Toggles implementiert)

### Umsetzungshinweis fuer die Reihenfolge
1. Phase 0 zuerst festzurren, weil Rollenmodell, GUI-Schnitt und Event-/Toggle-Schema die Leitplanken fuer alles Weitere sind.
2. Phase 1 und Phase 7a frueh parallel vorbereiten, damit Autoritaetsmodell und Menüstruktur nicht spaeter gegeneinander verdrahtet werden.
3. Phase 2 als technische Sperrschicht einbauen, damit spaetere Systeme nicht von der lokalen Engine zurueckueberschrieben werden.
4. Phase 3 bis 5 in kleinen, testbaren Teilpaketen erweitern.
5. Phase 6a danach als Event-Grundlage stabilisieren, weil BR, Minigames und andere Event-Modi dieselbe Event-Sprache brauchen.
6. Phase 6b und die Spezialmodi erst dann scharf schalten, wenn Phase 0 bis 6a stabil sind.
7. Phase 8 immer parallel mitlaufen lassen.

### Wichtige Referenzdateien
- `soh/soh/Network/Anchor/Anchor.h`
- `soh/soh/Network/Anchor/Anchor.cpp`
- `soh/soh/Network/Anchor/Menu.cpp`
- `soh/soh/Network/Anchor/HookHandlers.cpp`
- `soh/soh/Network/Anchor/Packets/UpdateRoomState.cpp`
- `soh/soh/Network/Anchor/Packets/PlayerUpdate.cpp`
- `soh/soh/Network/Anchor/Packets/EnemyPositionUpdate.cpp`
- `soh/soh/Network/Anchor/Packets/PlayerAttackActor.cpp`
- `soh/soh/Network/Anchor/Packets/ActorKilled.cpp`
- `soh/soh/Network/Anchor/Packets/RoomKillSync.cpp`
- `soh/soh/Network/Anchor/Packets/BoulderSpawn.cpp`
- `soh/src/code/z_actor.c`
- `soh/src/code/z_skelanime.c`
- `soh/src/code/z_play.c`
- `soh/src/code/z_scene.c`
- `soh/include/functions.h`

### Offene Designentscheidung
- Der sauberste Pfad ist, den globalen Host weiter als Save-/Lobby-Autoritaet zu behandeln und den Raum-Master als lokale, per Raum wechselnde Gameplay-Autoritaet einzufuehren. Das verhindert, dass globale und lokale Wahrheit vermischt werden.
