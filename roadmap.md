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
- [x] `UPDATE_ROOM_STATE` traegt `pvpMode`, `showLocationsMode`, `teleportMode`, `syncItemsAndFlags`, `syncHPAndCounts`, `syncDayTime`, `syncEnemies`, `syncRadius`, `enemySyncTickRate`, `physicalItemExchange`, `syncCutscenes`.
- [x] `PlayerUpdate` sendet aktuelle Player-State-Daten an alle online/save-loaded Peers und ist nicht mehr scene-gated.
- [x] Enemy-HP-Sync, Client-zu-Host-Damage-Forwarding, Host-Actor-Kill und ROOM_KILL_SYNC existieren bereits.
- [x] Enemy-Position-Sync traegt Position und Rotation und setzt auf Empfaengerseite `world.rot` und `shape.rot`.
- [x] Boulder-Spawn-Sync existiert und ist bereits auf `syncEnemies` plus Sync-Radius begrenzt.
- [x] Logging fuer Enemy-Sync unterscheidet Host und Client.
- [x] Der Radius- und Room-State-Datenfluss laeuft ueber `roomState` in `Anchor.h`.

### Phase 0: Architektur-Festlegung
Ziel: Die Leitplanken festziehen, bevor feature-spezifische Arbeit beginnt, damit spaetere Phasen nicht ihre eigenen Grundlagen nachtraeglich umbauen muessen.

- [ ] Raum-/Host-Autoritaet, Actor-Ownership und Fallback-Regeln schriftlich festlegen.
- [ ] Das GUI-Konzept auf zwei Ebenen festziehen: Core-Network versus Game-Modes.
- [ ] Das Event-Sync-Paket und die idempotente Event-ID-Strategie definieren, bevor BR, Minigames, Boss- oder Quest-Events gebaut werden.
- [ ] Das `RoomSettings.*`-Schema inklusive Default- und Backward-Compat-Regeln verbindlich festziehen.
- [ ] Kill, Loot, Respawn, Spectator und Wanted als allgemeine Zustandsarten benennen, damit BR und Event Sync dieselbe Sprache sprechen.
- [ ] Fuer jedes neue Feature die OFF-Pfad-Definition festlegen: vanilla, radius-gated, room-gated oder global.

Abhaengigkeiten:
- Diese Phase muss vor Phase 1, Phase 6a, Phase 6b und Phase 7a abgeschlossen sein.

### Phase 1: Grundarchitektur und Rollenmodell
Ziel: Host, Raum-Master und einfache Clients als explizite, getrennte Rollen modellieren, ohne die vorhandene Host-Lobby-Wahrheit zu zerstoeren. Diese Phase blockiert alle spaeteren Actor-/BG-/Event-Syncs.

- [ ] Entscheiden und dokumentieren, dass der Host global bleibt und der Raum-Master nur pro Szene/Raum wirkt.
- [ ] `RoomAuthorityState` oder eine aehnliche Struktur in `Anchor.h` ergaenzen, die pro Raum mindestens `roomKey`, `masterClientId`, `lastSeen`, `handshakeState`, `pendingTransfer` und optional `actorOwnershipMap` haelt.
- [ ] Die bestehende `roomState.ownerClientId`-Logik als aktuelle Baseline behalten, aber fuer neue Raum-Master-Loesung nicht als einzige Wahrheit verwenden.
- [ ] Einen klaren Raum-Schluessel definieren, der Szene und Raum robust kombiniert und nicht von der aktuellen Kamera oder UI abhaengt.
- [ ] Einen Join-Handschlag einfuehren: Wenn ein Client einen Raum betritt, meldet er seinen Raumstatus an den Host, der Host vergibt oder bestaetigt die Raum-Master-Rolle.
- [ ] Einen Leave-/Timeout-Handschlag einfuehren: Verlaesst der Raum-Master den Raum, verliert die Rolle oder reagiert nicht mehr, wird automatisch ein neuer Raum-Master gewaehlt.
- [ ] Eine Transfer-Regel definieren: Der Host bestimmt die Reihenfolge der Kandidaten, der erste aktive Client im Raum erhaelt die Rolle.
- [ ] Alle Handshake-Pakete muessen mit Default-Werten abwaerts-kompatibel bleiben, damit alte Clients nicht sofort brechen.
- [ ] Raum-Master- und Host-Rollen in den Debug-Logs klar sichtbar machen, damit Live-Tests nachvollziehbar bleiben.

Abhaengigkeiten:
- Diese Phase muss vor allen erweiterten Actor-/BG-Uebertragungen stehen.
- Ohne diese Rolle koennen spaetere Ownership-Entscheidungen nicht stabil verteilt werden.

### Phase 2: Zentrale Engine-Gates und Marionetten-Filter
Ziel: Die N64-Update-Schleife so kontrollieren, dass nicht der falsche PC eine Actor- oder Objekt-KI weiterberechnet. Diese Phase ist die Grundlage fuer Enemy-, BG- und Spezialobjekt-Sync.

- [ ] Einen zentralen Entscheidungshelfer bauen: `ShouldActorBeNetworkDriven`, `IsActorOwnedByRoomMaster`, `IsActorInsideSyncRadius`, `ShouldApplyVisualOnlyUpdate`.
- [ ] Die Hauptentscheidung moeglichst frueh im Actor-Update treffen, also bevor die eigentliche `actor->update(actor, play)`-Logik loslaeuft.
- [ ] Fuer einfache Clients `ACTORCAT_ENEMY` und `ACTORCAT_BG` standardmaessig aus der lokalen KI nehmen, wenn sie nicht Raum-Master/Owner fuer diesen Actor sind.
- [ ] Visuelle Restarbeit getrennt behandeln: SkelAnime-Update, Partikel, Draw-State und Transform-Uebernahme duerfen weiterlaufen, auch wenn die KI blockiert ist.
- [ ] Fuer alle synchronisierten Actor-Typen sicherstellen, dass `world.rot.y` und `shape.rot.y` gemeinsam gesetzt werden, damit Darstellung und Physik nicht auseinanderlaufen.
- [ ] Ein einheitliches Remote-State-Override-Pattern etablieren: Position, Rotation, Health, State-Flags, Action-Function oder Animationsstatus werden vor Ort ueberschrieben, aber nicht dauernd vom lokalen Update zurueckgerissen.
- [ ] Die lokalen Overlap-/Echoschutz-Flags vereinheitlichen, damit empfangene Synchronisationen nicht sofort als lokale Aenderungen zurueckgesendet werden.
- [ ] Wenn ein Actor nicht synchronisiert werden soll, muss er ausdruecklich vanilla bleiben und nicht in einen halbfertigen Sync-Zustand fallen.
- [ ] Falls ein zentraler Hook nicht reicht, nur dann einen minimalen Eingriff in die N64-Dispatch-Schicht vornehmen; niemals unnoetig viele individuelle Actor-Overlays anfassen.

Abhaengigkeiten:
- Diese Phase blockiert alle stabilen BG- und Enemy-Zustandsuebertragungen.
- Ohne sie entstehen wieder Moonwalking, Divergenzen bei Animationen und instabile Trigger-Zustaende.

### Phase 3: Raumzustand und Datenverteilung
Ziel: Der Raum-Master liefert einen konsistenten Snapshot an neue oder nachziehende Clients, ohne dass der Host jede Detailbewegung kennen muss.

- [ ] Einen Raum-Snapshot definieren, der beim Betreten eines Raums an neue Clients geht.
- [ ] Der Snapshot muss mindestens Actor-HP, Sichtbarkeitszustand, Position, Rotation, State-Flags und relevante Raumobjekte enthalten.
- [ ] Die aktuelle `UpdateRoomState`-Struktur um neue Raum-Master-/Feature-Flags erweitern, aber den Rueckfall mit Defaults immer beibehalten.
- [ ] Ein Zustandsmodell fuer Spaetbeitritte definieren: Wer neu in einen Raum kommt, bekommt zuerst Snapshot, dann laufende Delta-Updates.
- [ ] Die Reihenfolge der Daten festlegen: Raum-Master-Identitaet, dann Actor-/BG-Snapshot, dann laufende Events.
- [ ] Fuer jede neue Kategorie klar trennen zwischen Snapshot-Daten und Event-Daten.
- [ ] Bei Raumwechseln alle Zustandscaches sauber invalidieren, damit keine alten Keys in den neuen Raum hineinleaken.

Abhaengigkeiten:
- Diese Phase braucht Phase 1 und sollte parallel zu Phase 2 vorbereitet werden, aber nicht vor deren Gate-Entscheidungen live gehen.

### Phase 4: Enemy-Sync als gestuftes Ownership-System
Ziel: Gegner werden nicht nur als HP/Kill-Events gesynct, sondern als zusammenhaengender Zustand aus Position, Animation, Rotation und Kampfzustand.

- [ ] Enemies zuerst in stabile Subfamilien aufteilen: Standardgegner, versteckte/spezielle Gegner, Boss-/Mini-Boss-Varianten.
- [ ] Fuer jede Familie den minimalen Satz an syncbaren Zustaenden festlegen: Position, Blickrichtung, Health, Attack-/Idle-Phase, Hide/Appear-Phase, Knockback, Death.
- [ ] Nur den Owner bzw. Raum-Master die kaempferisch relevanten Zustaende berechnen lassen.
- [ ] Der Client ausserhalb des Sync-Radius bleibt vanilla und simuliert die Gegner lokal selbst, solange keine autoritative Uebernahme existiert.
- [ ] Fuer Gegner, die zwischen sichtbaren und versteckten Zustaenden wechseln, die State-Aenderung explizit uebertragen statt nur Position zu schicken.
- [ ] Burrow-/Emerge-Zustaende, z. B. Deku Scrubs, als echte State-Wechsel modellieren, nicht nur als Positionssprung.
- [ ] Death-Handling als harte, autoritative Loeschung behandeln, nicht auf das Ende einer Toedesanimation warten.
- [ ] ROOM_KILL_SYNC und ACTOR_KILLED als Redundanzpaar behalten: erst lokales Raum-Kill-Signal, dann endgueltige Autoritaetsbestaetigung.
- [ ] Fallbacks fuer nicht exakt matchbare Actor-Instanzen definieren, aber diese nur als Notfall verwenden, nicht als Primaerlogik.
- [ ] Fuer Bewegungs-/Kampf-Actors eigene Retry- und Framing-Regeln festlegen, damit unterschiedliche FPS keine Semantik veraendern.

Abhaengigkeiten:
- Braucht Phase 2 fuer das Override des lokalen Actor-Updates.
- Braucht Phase 3 fuer saubere Raum-Snapshots und spaetere Joiner.

### Phase 5: BG-/Umweltobjekte (ACTORCAT_BG)
Ziel: Bewegliche und schaltbare Umgebungsobjekte stabil synchronisieren, ohne jedes Objekt einzeln hart zu codieren.

- [ ] Ein kompaktes BG-Sync-Paket definieren: `actorKey`, `actorId`, `pos`, `rot`, `stateFlags`, optional `speed` oder `phase`.
- [ ] BG-Objekte in Klassen aufteilen: beweglich, schaltbar, zerstoerbar, rotierend, zeitabhaengig, rein visuell.
- [ ] Fuer bewegliche Plattformen und Fahrstuehle bevorzugt Zustaende oder Geschwindigkeit syncen, nicht jede einzelne Mikrobewegung.
- [ ] Fuer zerstoerbare bzw. einmalige BG-Objekte lieber Event-Sync oder Kill-Sync statt Dauerstream verwenden.
- [ ] Fuer Objekte wie rollende Steine, Tueren, Schalter, Bloecke und Lavasaeulen eine deterministische Spawn-/Start-Regel definieren.
- [ ] Wenn ein Objekt lokal unsynchron ist, darf der Client ausserhalb des Radius wieder vanilla rechnen; innerhalb des Radius uebernimmt der Sync-State.
- [ ] Eine Interpolations-/Snap-Regel fuer BG-Objekte festlegen, damit Korrekturen nicht hart beamen.
- [ ] Fuer BG-Objekte mit Scene-/Room-Abhaengigkeit einen robusten Spawn-Key verwenden, der nicht auf fluetchigen Live-Koordinaten beruht.
- [ ] Die allgemeinsten BG-Familien zuerst implementieren: Schieber, Tueren, Blocksysteme, Hebeplattformen, Zeit-Bloecke, Tueren/Gatter, rollende Gefahren.
- [ ] Spezielle BG-Actors nur dann einzeln anfassen, wenn der generische Kategoriepfad nicht reicht.

Abhaengigkeiten:
- Braucht Phase 2, sonst ueberschreibt lokale BG-KI den synchronisierten Zustand wieder.
- Braucht Phase 1, damit klar ist, wer fuer den Raum verantwortlich ist.

### Phase 6: Spezialfaelle und besondere Actor-Familien
Ziel: Die schwierigen Einzelmechaniken getrennt behandeln, ohne die allgemeine Architektur zu zerbrechen.

- [ ] Deku Scrubs / Versteckgegner zuerst auf Zustands-Sync umstellen: hide, emerge, attack, panic, knockback.
- [ ] Leever-/Wallmaster-/aehnliche Zustandsmonster separat als State-Maschinen behandeln, nicht als reine Positionsobjekte.
- [ ] Boulder-/Gefahrenobjekte nur innerhalb des Sync-Radius synchronisieren; ausserhalb des Radius muss der Client die lokale Spawn-Logik behalten.
- [ ] Epona / Pferde-System pro Spieler denken: eigene Ownership, eigene Spawn-Instanz, fremde Pferde als reine Marionetten.
- [ ] Minigames als Event-Sync behandeln: Start, Timer, Score, Win/Lose, Reward, Exit.
- [ ] Battle-Royale-Modus als eigenes Spielregime mit isoliertem Progress pro Spieler, eigener Loot-Oekonomie und klarer Siegbedingung planen.
- [ ] Wanted-/NPC-Aggro-System als separaten Modus formulieren, der nur bei aktivem BR-/Chaos-Modus greift.
- [ ] Fuer jeden Spezialfall vorab definieren, ob er ueber State-Sharing, Event-Sync oder reine lokale Vanilla-Simulation laeuft.

### Phase 6b: Battle Royale
Ziel: Einen optionalen PvP-Regelmodus bauen, in dem jeder Link eine eigene Fortschrittslinie hat, Item- und Progress-Synchronisation bewusst eingeschraenkt wird und der Host den Match-Zustand zentral steuert.

- [ ] BR als komplett optionalen Raum-Modus modellieren, der nur aktiv ist, wenn der passende Toggle gesetzt ist.
- [ ] Spielstart definieren: Alle Spieler beginnen unter identischen Anfangsbedingungen oder mit bewusst festgelegten Startkits.
- [ ] Eine klare Win-Bedingung festlegen: letzter lebender Spieler, Boss-Kill als Abschluss, oder ein vom Host gewaehlter Zielpunkt.
- [ ] Eine klare Lose-Bedingung festlegen: Tod, kompletter Progress-Verlust im BR-Raum, oder Host-definierte Eliminierung.
- [ ] Jeder Spieler bekommt eigenen Progress fuer Herzen, Items, Wallet, Schluessel, Dungeon-Status und relevante Quest-Flags, sofern der BR-Modus das verlangt.
- [ ] Globale Story-Flags und Match-Flags trennen: was nur fuer den BR-Raum gilt, darf nicht automatisch den dauerhaften Singleplayer-Fortschritt ueberschreiben.
- [ ] Item-Sync im BR-Modus einschraenken oder gezielt filtern, damit Loot nicht unbeabsichtigt alle Spieler gleichzeitig staerker macht.
- [ ] Spawn-, Respawn- und Fairness-Regeln festlegen, damit kein Spieler durch Respawn-Camping oder Kill-Stealing systematisch bevorzugt wird.
- [ ] Einen definierten Death-Loop bauen: Tod loest einen klaren Zustand aus, etwa Spectator, Respawn nach Timer oder permanente Eliminierung.
- [ ] Bei Kills eine Host-autoritative Meldung erzeugen, damit Kill, Assist und eventuell Loot-Drop nicht widerspruechlich werden.
- [ ] Ein Loot-/Drop-System definieren, das fuer BR sinnvoll ist: entweder feste Drops, lokales Loot oder host-gesteuerte Arena-Drops.
- [ ] Einen BR-spezifischen Zonen- oder Ring-Mechanismus pruefen, der Spieler dynamisch in dieselbe Kampfzone zwingt.
- [ ] Das vorhandene Wanted-/NPC-Aggro-System als BR-Disziplinarmaßnahme nutzen, damit dominante Spieler von der Welt gejagt werden koennen.
- [ ] BR-Events wie "Match Start", "First Blood", "Player Eliminated", "Final Circle" und "Winner" als Event-Sync-Klasse behandeln.
- [ ] Fuer BR eine eigene Admin-Untersektion im Anchor-Panel vorsehen, damit der Modus separat aktivierbar und testbar bleibt.
- [ ] BR in klar getrennte Unterphasen schneiden: Start/Loadout, Combat, Elimination, Endgame.
- [ ] Startphase definieren: Spawnpunkte, Startschutz, Loadouts, Match-Countdown und Room-Ready-Check.
- [ ] Combatphase definieren: PvP-Aktivierung, erlaubte Raeume, Safe-Zones und Friendly-Fire-Regeln.
- [ ] Eliminationphase definieren: Death-Handling, Loot-Transfer, Respawn/Spectator und Kill-Feeds.
- [ ] Endgame definieren: Final Circle, Finale-Gegenueberstellung, Siegerzustand, Belohnung und Match-Cleanup.
- [ ] Team- oder Solo-Varianten explizit entscheiden, damit der Modus nicht zwei inkompatible Regeln gleichzeitig traegt.
- [ ] Das Wanted-System als Eskalationsstufe im BR definieren: Killstreak-Threshold, Broadcast, NPC-Aggro und Ruecksetzlogik.

Abhaengigkeiten:
- Diese Phase braucht Phase 0 und Phase 6a als Vorbedingung.

### Phase 6a: Event Sync
Ziel: Einmalige oder globale Spielereignisse werden als eigene Synchronschicht behandelt, getrennt von laufender Actor-Bewegung und getrennt von reinem BG-Status.

- [ ] Einen einheitlichen Event-Typenkanal definieren fuer Dinge wie Truhen, Schalter, Tueren, Boss-Phasen, Quest-Flags, Minigame-Starts und Renn-Events.
- [ ] Event-Sync immer als authoritative, idempotente Zustandsaenderung denken, nicht als Frame-fuer-Frame-Streaming.
- [ ] Jedes Event braucht einen stabilen Schluessel, eine Zielmenge und einen klaren Abschlusszustand, damit es nicht doppelt oder gar nicht angewendet wird.
- [ ] Einmalige Weltaenderungen wie "Wand gesprengt", "Truhe geoeffnet" oder "Schalter aktiviert" sollen ueber ein sauberes Event laufen, nicht ueber Dauer-Positionen.
- [ ] Bei Minigames den Event-Start, den globalen Score, den Timer und den Abschluss separat synchronisieren.
- [ ] Fuer Cutscenes und Story-Events definieren, ob sie lokal, teamweit, raumweit oder global laufen.
- [ ] Event-Sync und Save-Flag-Sync sauber trennen: manche Ereignisse aendern nur Sichtbarkeit, andere den permanenten Fortschritt.
- [ ] Fuer jedes Event einen OFF-Fall definieren, bei dem die Vanilla-Logik unangetastet bleibt.

Abhaengigkeiten:
- Diese Phase ist absichtlich spaeter, weil sie auf den stabilen Grundregeln aus Phase 1 bis 5 aufbaut.

### Phase 7: Admin-Panel, Toggle-Matrix und Defaults
Ziel: Jede Funktion muss im Admin-Panel sichtbar, umschaltbar und mit sinnvollen Defaults versehen sein.

- [ ] Fuer jede neue Funktion ein `RoomSettings.*`-CVar anlegen und im Anchor-Admin-Panel sichtbar machen.
- [ ] `Menu.cpp` bleibt die zentrale Bedienoberflaeche fuer Raum-Toggles.
- [ ] `UpdateRoomState.cpp` bleibt die einzige Stelle, die Raum-Settings serialisiert und wiederherstellt.
- [ ] Toggle-Gruppen sauber trennen: Basissync, Enemy-Sync, BG-Sync, Spezialmodi, Party-/BR-Modi.
- [ ] Fuer neue Funktionen Defaults festlegen, die keine bestehenden Rooms brechen.
- [ ] Bei deaktiviertem Toggle muss der Codepfad komplett no-op oder vanilla sein.
- [ ] Aenderungen an Raum-Settings muessen ueber `SendPacket_UpdateRoomState` propagiert werden.
- [ ] Alte Clients muessen fehlende Felder per Default-Wert verarbeiten koennen.
- [ ] Das Admin-Panel sollte visuell klar zeigen, welche Features voneinander abhaengen.
- [ ] Pruefen, ob Game-Mode-Schalter auf eine eigene GUI-Seite wandern, damit der Anchor-/Network-Tab nicht ueberfuellt wird.

Abhaengigkeiten:
- Diese Phase sollte frueh mit Phase 0/1 vorbereitet werden, bevor die Toggle-Liste weiter anwächst.

### Phase 7a: Game-Mode-GUI
Ziel: Die eigentlichen Spielmodus-Schalter bekommen eine eigene GUI-Seite, damit der Anchor-/Network-Tab schlank bleibt und nur Kernnetzwerkoptionen zeigt.

- [ ] Eine eigene GUI-Seite oder eigener Sub-Tab fuer Game-Modes anlegen, getrennt vom bestehenden Network/Anchor-Haupttab.
- [ ] Kern-Netzwerkoptionen im Anchor-Tab belassen: Verbindung, Lobby, Basissync, Debug und Raumidentitaet.
- [ ] Spielmodus-Optionen in die neue Seite verschieben: BR, Event Sync, Minigames, Wanted/NPC-Aggro, Epona, spaetere Spezialmodi.
- [ ] Die neue Seite so strukturieren, dass verwandte Toggles gruppiert bleiben und nicht als lange flache Liste erscheinen.
- [ ] Jede Untergruppe mit klarer Beschreibung und Default-Zustand versehen, damit Admins die Abhaengigkeiten auf einen Blick sehen.
- [ ] Aenderungen von der neuen Seite weiterhin ueber die bestehende `RoomSettings.*`-Pipeline und `SendPacket_UpdateRoomState` senden.
- [ ] Falls der Raum kein Admin ist oder der Modus global deaktiviert ist, muss die Seite gesperrt oder nur lesbar sein.
- [ ] Die neue Seite in der bestehenden Menüregistrierung neben `AnchorMainMenu`, `AnchorAdminMenu` und `AnchorInstructionsMenu` einhaengen.

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
- [ ] Jede neue Packet-Erweiterung mit `contains`/`value`-Fallbacks absichern.
- [ ] Bei jedem neuen Sync-Typ pruefen, ob late joiners denselben Zustand erhalten wie der aktuelle Raum-Master.
- [ ] Fuer jeden neuen Toggle eine Negativprobe definieren: Was passiert bei OFF?
- [ ] Bandbreite und Update-Frequenz messen, bevor ein neuer Actor-Typ in Dauerstream geht.
- [ ] Debug-Logs nur dort belassen, wo sie echte Race-Conditions sichtbar machen; ansonsten in Feature-Flags buendeln.
- [ ] Erst wenn Phase 1 bis 5 sauber stehen, die Spezialmodi aus Phase 6 scharf schalten.

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
