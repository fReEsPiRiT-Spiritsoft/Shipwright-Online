# Features (implementiert)

Diese Datei listet alle Features auf, die in diesem Fork bereits umgesetzt wurden.

## Core Synchronization & Combat

- Host-Authority Enemy Sync: Client-seitige Gegner werden im Shared-Room-Modus eingefroren und vom Host via Positions-, Rotations- und Animationsdaten gesteuert.
- Client-to-Host Damage Routing: Treffer vom Client werden abgefangen, an den Host geschickt und dort nativ validiert sowie angewendet.
- Dynamic Aggro Spoofing: Gegner-Aggro kann auf den Client-Dummy umgebogen werden, wenn der Client naeher am Gegner ist.
- Configurable Enemy Sync Radius: Synchronisierung ist auf einen einstellbaren Radius begrenzt.
- Configurable Enemy Sync Tick Rate: Synchronisierungstakt live konfigurierbar (5/10/20 Hz).

## Dynamic World & Progression

- Dynamic Room Switching: Beim Trennen in Szene/Raum wird Network-AI-Sync pausiert, lokale KI laeuft stabil weiter.
- State Merging & Re-Entry Catch-Up: Kills aus Solo-Phasen werden beim Rejoin korrekt uebernommen.
- Out-of-Radius Kill Persistence: Kills ausserhalb des Sync-Radius gehen nicht verloren und werden spaeter angewendet.
- Physical Item Exchange Mode: Items/Flags koennen gepuffert und bei Naehe mit nativer Uebergabe-Inszenierung ausgetauscht werden.

## Environment & Global State

- Cross-Zone Day/Time Synchronization: Tageszeit wird netzwerkweit synchron gehalten (Host als Referenz).
- Global Time-Lock Feature: Zeit stoppt global, sobald einer in einer zeitlosen Szene ist.

## Battle Royale Mode (neu implementiert)

- BR Match Start + Spawn Assignments: Host startet Match und verteilt Spawnpunkte (Hyrule Field Tabelle).
- BR Respawn System: Spieler respawnen nach Tod wieder im Match (keine permanente Eliminierung).
- BR Start Protection: Kurzzeitiger Spawn-Schutz direkt nach Match-Start.
- BR Kill Event Pipeline: Death-Detection sendet PLAYER_KILLED, Host verarbeitet und sendet PLAYER_ELIM.
- BR Item Theft on Kill: Killer erhaelt zufaellige lootbare Items aus Opfer-Inventar (Whitelist-basiert).
- BR Victim Stat Reset: Opfer wird auf BR-Basiszustand zurueckgesetzt (u.a. Health/Rupees/Chest-Flags/gestohlene Items).
- BR Killer Reward: Killer erhaelt zusaetzlich Herz-Bonus nach Kill.
- BR Killstreak/Wanted System: Killstreak-Tracking und Wanted-Status inkl. Set/Clear-Events.
- BR Cheat Disable: Relevante Cheats werden pro Frame deaktiviert, solange BR aktiv ist.
- BR Match End by Ganondorf: Defeat von Boss Ganon 2 beendet das Match und setzt Winner.

## Multiplayer Room Features (ergänzt)

- Minigame Sync: Room-Master sendet Start/Score/End, Empfaenger aktualisiert `gSaveContext.minigameState` und `gSaveContext.minigameScore`.
- Epona Sync via Phantom Horse: Pro Remote-Client wird ein AI-silent Phantom-Horse-Actor gepflegt (spawn/update/cleanup), gated ueber `syncEpona`.

## Stability & Protocol

- Backward-compatible Packet Parsing: Neue Felder werden defensiv mit Defaults gelesen.
- Late-Join BR Handling verbessert: Keine rekonstruierten Pseudo-Eliminierungszustande fuer Joiner.
- Room Snapshot/State Sync laufend erweitert fuer konsistente Rejoins.

## Bekannte bewusst verschobene Punkte

- Eigener Progress pro Spieler (vollstaendige Trennung) ist noch nicht final abgeschlossen.
- Globale Story-Flag-Trennung ist noch nicht final abgeschlossen.
