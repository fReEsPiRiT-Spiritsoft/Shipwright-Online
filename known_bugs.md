## Status-Check (29.05.2026)

Legende:
- [x] Im Code bearbeitet
- [ ] Noch offen
- [x] (teilweise) Angefasst, aber noch nicht komplett geloest

- [x] Epona/Phantom-Epona Crash (Skin_UpdateVertices)
- [x] Timesync nur einmal beim Join statt periodisch
- [x] Zugbruecke asynchron (durch Timesync-Umstellung adressiert)
- [x] Skelett-Spawns nachts asynchron (durch Timesync-Umstellung adressiert)
- [x] Nicht-Room-Master koennen mit Toepfen/Kisten nicht interagieren
- [x] Keiner kann mit Pferden interagieren
- [x] Lieder haben keinen Effekt fuer Nicht-Room-Master
- [x] Cutscenes softlocken (Bildrand, keine Bewegung, kein Ende)
- [x] Jabu-Jabu Boss unkillbar (Boss-Sync-Interferenz)

- [x] (teilweise) Zoras Wasserfall nicht synchron / Nicht-Room-Master kann nicht oeffnen
- [x] (teilweise) Lord Jabu-Jabu Maul oeffnet nicht synchron
- [x] (teilweise) Master-Schwert nur durch Room-Master ziehbar

- [x] Zoras Koenig bewegt sich nicht synchron
- [x] (teilweise) Zora Tauchspiel nicht synchron
- [x] (teilweise) Tauchschuppe wird nicht an andere Spieler uebergeben
- [x] (teilweise) Prinzessin Ruto nur fuer Ausloeser sichtbar
- [x] (teilweise) Prinzessin Ruto tragen nicht synchron
- [x] Jabu-Jabu Zwischenboss taucht nicht fuer alle auf
- [x] Wackel-Actors in Jabu-Jabu nicht synchron/buggy
- [x] (teilweise) Minigames LongLon/LonLon nicht absolvierbar

Hinweis: Die [x]-Punkte sind als Code-Fix eingecheckt/aenderbar im aktuellen Stand. Vollstaendig verifiziert sind sie erst nach Ingame-Test (Host + Client).

Zoras wasserfall öffnet sich nicht synchron*'
Nicht raum master kann wasserfall nicht öffnen.

Zoras könig bewegt sich nicht gesynnct
Zora Tauchspiel nicht synchron

TauchSchuppe wird nnicht an andere spieler übergebenn 

Lord jabujabu öffnet maul nicht synchron. 

Cutscennes werden für mitspieler nicht abgespielt. nur der bild rand kommt aber die cutscene selbst niochtr ( Nur bewegungsunfähig und endet nicht)

Prinzessin ruto in jabu nur für den cutscene auslöser sichtbar

Prinzessin ruto tragen nicht synchron

jabu jabu zwischen boss nicht für nicht aulöser aufgetaucht

Wackel actors jabu jabu nicht gesynct und buggy

nicht room master können nicht mit tpfen un kisten interagieren


Timesync müssen wir verbessern, es ist buggy am besten beim betreten des servers nur einmal abgleichen

zugbrücke nicht synchron


Dkellette nachts nicht gesynct

jabu jabu boss gegnersync verursacht unkillable boss bug

Lieder spielen hat keinen effekt für nicht raum master

Keiner auch nicht der raummaster oder Host kannn mit pferden interagieren
minigames longlong range nicht absolvierbar

zuweit in der zitadelle der zeit kann nur der raum master das schwert ziehen... sollte so nicht sein,.


Epona Sync und pahmtom epona löässtz das spiel crashen:

[19:33:33.824] [RoomKillSync.cpp:237] [info] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC applied | scene=0x51 room=0 applied=0/9
[19:33:34.185] [RoomKillSync.cpp:161] [info] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC recv | scene=0x51 room=0 kills=9
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_-118_27_2167_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_-1933_104_5455_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_-29_-473_12613_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_-446_-417_11256_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_-5215_-273_1504_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_-6267_-473_7135_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_1361_421_5869_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_3709_164_1721_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:232] [warning] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC actor not found/already dead | actorKey=81_5_373_0_255_679_-118_8268_0_0_0 | scene=0x51 room=0
[19:33:34.185] [RoomKillSync.cpp:237] [info] [Anchor:EnemySync] HOST: ROOM_KILL_SYNC applied | scene=0x51 room=0 applied=0/9
[19:33:34.277] [DummyPlayer.cpp:213] [info] [Anchor:Horse] Spawned phantom horse for client 115780 (actor=0x55d36936bf80)
[19:34:04.736] [CrashHandler.cpp:72] [critical] Signal: 11
INVALID ACCESS TO STORAGE
Registers:
RAX: 0x0000000000000EE0
RDI: 0x000055D33B640E20
RSI: 0x00007FFD3429F440
RDX: 0x0000000000000155
RCX: 0x0000000000000000
R8:  0x00007FFD3429F508
R9:  0x00007F9E10154D30
R10: 0x0000000000000020
R11: 0x00007FFD3429F444
RSP: 0x00007FFD3429F410
RBX: 0x00007F9E100FF920
RBP: 0x00007F9E101541C0
R12: 0x0000000000000EE0
R13: 0x00007F9E100FF92A
R14: 0x000055D33B640E20
R15: 0x00007FFD3429F508
RIP: 0x000055D33A40F333
EFL: 0x0000000000010202
Traceback:
1 /usr/lib/libc.so.6(+0x44db0) [0x7f9e3d444db0]
2 Skin_UpdateVertices (+0xA3)
3 Skin_ApplyLimbModifications (+0x221)
4 Skin_DrawAnimatedLimb (+0x91)
5 Skin_DrawImpl (+0x9D)
6 func_800A6330 (+0x17)
7 EnHorseNormal_Draw (+0x74)
8 Actor_Draw (+0x1FB)
9 func_800315AC (+0x587)
10 Play_Draw (+0xA5F)
11 Play_Main (+0x68)
12 GameState_Update (+0x30)
13 /mnt/daten/Development/compiled test/soh.appimage(+0x276d45c) [0x55d33a36045c]
14 Graph_ThreadEntry (+0x204)
15 Main (+0x35D)
16 main (+0x3F)
17 /usr/lib/libc.so.6(+0x27c8e) [0x7f9e3d427c8e]
18 __libc_start_main (+0x8B)
19 _start (+0x25)
Build Information:
  Game Version: Ackbar Delta (9.2.3)
  Git Branch: develop
  Git Commit: 54488ba
  Build Date: May 28 2026 09:59:00
Scene: SCENE_HYRULE_FIELD
Room: 0
Actors:
  Category: BG
    Horse (0)
    Ambient Sound Effects (21)
    Ambient Sound Effects (0)
    Ambient Sound Effects (1)
    Rideable Horse (0)
  Category: PLAYER
    Link (4095)
  Category: NPC
    Unused NPC (0)
  Category: ENEMY
    Big/Small Poe Spawn Point (31)
  Category: PROP
    Trees and Bushes (7)
    Trees and Bushes (7)
    Lake Hylia Sun Hitbox, Big Fairy Spawner (-191)
    Brown Bombable Boulder (-32752)
    Brown Bombable Boulder (-32754)
    Brown Bombable Boulder (-32760)
    Trees and Bushes (0)
    Trees and Bushes (0)
    Trees and Bushes (0)
    Trees and Bushes (0)
    Trees and Bushes (0)
    Trees and Bushes (0)
    Trees and Bushes (10)
    Trees and Bushes (19)
    Trees and Bushes (19)
    Trees and Bushes (19)
    Trees and Bushes (19)
    Trees and Bushes (19)
    Trees and Bushes (19)
    Rock/Bush groups (1793)
    Rock/Bush groups (1793)
    Rock/Bush groups (1793)
    Rock/Bush groups (1793)
    Rock/Bush groups (1793)
    Rock/Bush groups (1793)
    Rock/Bush groups (1794)
    Trees and Bushes (2)
    Trees and Bushes (7)
    Trees and Bushes (7)
    Trees and Bushes (7)
    Trees and Bushes (6)
    Trees and Bushes (6)
    Trees and Bushes (6)
    Trees and Bushes (6)
    Trees and Bushes (6)
    Liftable Rock (-255)
    Gameplay_keep items (10)
    Gameplay_keep items (10)
    Gameplay_keep items (10)
    Gameplay_keep items (10)
    Gameplay_keep items (10)
    Gameplay_keep items (10)
    Gameplay_keep items (10)
    Bronze Boulder (21)
    Bronze Boulder (20)
    Bronze Boulder (18)
    Bronze Boulder (17)
    Broken Drawbridge, Fences (1)
    Broken Drawbridge, Fences (1)
    Broken Drawbridge, Fences (1)
    Broken Drawbridge, Fences (0)
    Proximity Weather Effects (2560)
  Category: ITEMACTION
    Grotto Entrance (4351)
    Grotto Entrance (742)
    Grotto Entrance (741)
    Grotto Entrance (740)
    Grotto Entrance (737)
    Grotto Entrance (34)
    Grotto Entrance (3)
    Grotto Entrance (0)
    Fairy (0)
GFX Stack:








