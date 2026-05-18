Ich versuche, eine Server/Client-Version von SoH zu entwickeln, die das Co-op-Spielerlebnis vollständig synchronisiert.

# Hinzugefügte Features (Host-Authority Co-op Prototyp)

Dieser Fork implementiert eine Host-Authority-Multiplayer-Architektur, die für die Synchronisierung von Gegnern und der Spielwelt im Co-op- und Randomizer-Betrieb konzipiert wurde.

### Kern-Synchronisierung & Kampfmechanik
* **Host-Authority Gegnersynchronisierung:** Setzt das Freeze-Flag bei allen clientseitigen Aktoren (`actor->flags |= ACTOR_FLAG_27` / `Actor_SetFreezeFlags`), wenn sich Spieler im selben Raum befinden. Verhindert Physik-Desynchronisation und Rubberbanding, indem `world.pos`, `world.rot.y` und `skelAnime`-Zustände direkt vom Host gestreamt werden.
* **Client-zu-Host Schadensweitergabe:** Hängt sich in die Kollisions-Engine (`acHit` / `ColliderCylinder`) ein, um clientseitige Waffentreffer abzufangen. Die lokale Schadensberechnung wird auf dem Client unterbrochen und per Netzwerkpaket an den Host weitergeleitet. Der Host wertet den Treffer mit nativen Engine-Routinen aus und löst die synchronisierte Todessequenz aus.
* **Dynamisches Aggro-Spoofing:** Berechnet auf dem Host die Abstände (`Math3D_Vec3fDistSq`) zwischen Gegnern und beiden Spielern. Ist der Client näher dran, wird der Ziel-Pointer der Gegner-KI auf den Client-Dummy umgeleitet, sodass Gegner den Client verfolgen und angreifen.
* **Konfigurierbarer Gegnersynchronisierungs-Radius:** Der Host kann einen Radius in Welteinheiten festlegen, innerhalb dessen Gegnerpositionen und Animationen übertragen werden. Außerhalb dieses Radius läuft die Client-KI lokal ohne Netzwerkverkehr. Radius und Tickrate (5 / 10 / 20 Hz) sind live im Admin-Panel konfigurierbar.
* **Konfigurierbarer Gegnersynchronisierungs-Tickrate:** Positions- und Animationsdaten von Gegnern können auf 5 Hz (jeden 4. Frame), 10 Hz (jeden 2. Frame) oder 20 Hz (jeden Frame) gedrosselt werden, um Flüssigkeit gegen Bandbreite abzuwägen.

### Dynamische Spielwelt & Fortschrittslogik
* **Dynamischer Raumwechsel:** Pausiert die Netzwerk-Aktorsynchronisierung, wenn Spieler sich in verschiedene Szenen oder Räume trennen. Hebt clientseitige Freeze-Flags sofort auf, damit der Client gegen lokale Einzelspieler-KI spielen kann, ohne Speicher- oder Nullzeiger-Abstürze zu verursachen.
* **Zustandsabgleich beim Wiederzusammentreffen:** Wenn Spieler denselben Bereich wieder betreten, wird erfasst, wer zuerst eingetreten ist. Hat der Client währenddessen Gegner besiegt, sendet ein Todes-Listenpaket `Actor_Kill`-Befehle für die entsprechenden IDs an den Host, bevor die Host-Authority-Synchronisierung wieder greift.
* **Persistente Kills außerhalb des Radius:** Gegnerkills eines Nicht-Authority-Spielers außerhalb des Synchronisierungsradius (oder während der Authority in einem anderen Raum war) werden lokal in einer Warteschlange gespeichert und übertragen, sobald die Authority denselben Raum betritt — kein geleerten Raum kann sich auf dem Bildschirm des anderen Spielers jemals zurücksetzen.
* **Physischer Item-Tausch-Modus:** Wenn im Admin-Panel aktiviert, werden empfangene Items und Szenen-Flags gepuffert statt sofort angewendet. Items werden dem empfangenden Spieler erst übergeben, wenn beide Spieler sich auf ~1 Meter genähert haben. Die Übergabe löst die native „Item über den Kopf halten"-Animation und eine vollständig lokalisierte Ingame-Textbox aus: *„Du hast von [Name] das Item [Name] erhalten!"* — jeder Tausch fühlt sich wie eine echte Interaktion in der Spielwelt an.

### Umgebung & Globaler Zustand
* **Zonenübergreifende Tages-/Zeitsynchronisierung:** Synchronisiert den globalen Zeitstatus (`gSaveContext.dayTime`) über das Netzwerk und setzt den Host als primäre Zeitquelle.
* **Globale Zeitsperre:** Prüft `sceneNum` beider Spieler. Betritt einer von ihnen einen Bereich, in dem die Zeit natürlich stillsteht (z. B. Kakariko-Dorf, Marktplatz, Dungeons), friert die Zeituhr global für beide Spieler ein — auch wenn der andere gerade in Hyrule Field unterwegs ist.


<img width="1756" height="592" alt="soH logo" src="https://github.com/user-attachments/assets/c4e2d146-0f84-4cb8-aff2-dcfc9d52bd06" />


## Website

Offizielle Website: https://www.shipofharkinian.com/

## Discord

Offizieller Discord: https://discord.com/invite/shipofharkinian

Bei Problemen nach dem Lesen dieser `README` kannst du in den Support-Textkanälen um Hilfe bitten. Bitte beachte, dass wir Piraterie nicht befürworten.

# Schnellstart

The Ship enthält keine urheberrechtlich geschützten Assets. Du musst eine unterstützte Kopie des Spiels bereitstellen.

### 1. ROM-Dump überprüfen
Du kannst mit dem Kompatibilitätsprüfer auf https://ship.equipment/ überprüfen, ob du eine unterstützte Spielkopie gedumpt hast. Falls du deinen ROM-Dump lieber manuell validieren möchtest, kannst du seinen `sha1`-Hash mit den Hashes [hier](docs/supportedHashes.json) abgleichen.

### 2. The Ship of Harkinian von [Releases](https://github.com/HarbourMasters/Shipwright/releases) herunterladen

### 3. Spiel starten!
#### Windows
* ZIP-Datei entpacken
* `soh.exe` starten

#### Linux
* Lege deine unterstützte Spielkopie in denselben Ordner wie das AppImage.
* Führe `soh.appimage` aus. Eventuell musst du es über das Terminal mit `chmod +x` ausführbar machen.

#### macOS
* Starte `soh.app`. Wähle bei der Aufforderung deine unterstützte Spielkopie aus.
* Eine Benachrichtigung `Processing OTR` erscheint und nach Abschluss `OTR Successfully Generated`, danach startet das Spiel.

#### Nintendo Switch
* Starte eine der PC-Versionen, um eine `oot.o2r`- und/oder `oot-mq.o2r`-Datei zu erstellen. Diese Dateien befinden sich nach dem Start im selben Verzeichnis wie `soh.exe` oder `soh.appimage`. Unter macOS sind sie unter `/Users/<Benutzername>/Library/Application Support/com.shipofharkinian.soh/` zu finden.
* Kopiere die Dateien auf die SD-Karte:
```
sdcard
└── switch
    └── soh
        ├── oot-mq.o2r
        ├── oot.o2r
        ├── soh.nro
        └── soh.o2r
```
* Starte über Atmosphères `Game+R`-Startmethode.

### 4. Spielen!

Glückwunsch, du segelst jetzt mit dem Ship of Harkinian! Viel Spaß!

# Konfiguration

### Standard-Tastaturbelegung
| N64 | A | B | Z | Start | Analogstick | C-Tasten | D-Pad |
| - | - | - | - | - | - | - | - |
| Tastatur | X | C | Z | Leertaste | WASD | Pfeiltasten | TFGH |

### Weitere Tastenkürzel
| Tasten | Aktion |
| - | - |
| ESC | Menü ein-/ausblenden |
| F2 | Mauseingabe erfassen umschalten |
| F5 | Spielstand speichern |
| F6 | Spielstand wechseln |
| F7 | Spielstand laden |
| F9 | Text-to-Speech umschalten (nur Windows und Mac) |
| F11 | Vollbild |
| Tab | Alternative Assets umschalten |
| Ctrl+R | Zurücksetzen |

# Projektübersicht
Ship of Harkinian (SOH) basiert auf einer eigens entwickelten Bibliothek namens libultraship (LUS). In den N64-Zeiten gab es ein Entwickler-SDK namens libultra; LUS soll dessen Funktionalität auf moderner Hardware nachbilden. Darüber hinaus sind wir auf den Quellcode des OOT-Dekompilierungsprojekts angewiesen.

Damit das Spiel funktioniert, benötigst du eine **legal erworbene** ROM für Ocarina of Time. Klicke [hier](https://ship.equipment/), um die Kompatibilität deiner ROM zu prüfen. Alle urheberrechtlich geschützten Assets werden aus der ROM extrahiert und als `.o2r`-Archivdatei neu formatiert.

### Grafik-Backends
Derzeit werden drei Rendering-APIs unterstützt: DirectX11 (Windows), OpenGL (alle Plattformen) und Metal (macOS). Die API lässt sich im `Einstellungen`-Menü der Menüleiste ändern (Neustart erforderlich). Bei Abstürzen kann die API in der Datei `shipofharkinian.json` durch Ändern von `gfxbackend:""` auf `sdl` für OpenGL umgestellt werden. DirectX 11 ist der Standard unter Windows.

# Eigene Assets

Eigene Assets werden in `.otr`-Archivdateien verpackt. Um sie zu verwenden, lege sie im `mods`-Ordner ab.

Wenn du eigene Asset-`.otr`-Dateien erstellen oder packen möchtest, schau dir folgende Tools an:
* [**retro - OTR-Generator**](https://github.com/HarbourMasters64/retro)
* [**fast64 - Blender-Plugin**](https://github.com/HarbourMasters/fast64)

# Entwicklung
### Kompilieren

Wenn du SoH manuell kompilieren möchtest, lies bitte die [Build-Anleitung](docs/BUILDING.md).

### Playtesting
Wenn du einen Continuous-Integration-Build testen möchtest, findest du diese unter den folgenden Links. Beachte, dass diese nur zum Testen gedacht sind und du auf Bugs und möglicherweise Abstürze treffen wirst.

* [Windows](https://nightly.link/HarbourMasters/Shipwright/workflows/generate-builds/develop/soh-windows.zip)
* [macOS](https://nightly.link/HarbourMasters/Shipwright/workflows/generate-builds/develop/soh-mac.zip)
* [Linux](https://nightly.link/HarbourMasters/Shipwright/workflows/generate-builds/develop/soh-linux.zip)

### Weiterführende Dokumentation
Ausführlichere Dokumentation befindet sich im `docs`-Verzeichnis, einschließlich der oben erwähnten [Build-Anleitung](docs/BUILDING.md).

* [Credits](docs/CREDITS.md)
* [Custom Music](docs/CUSTOM_MUSIC.md)
* [Controller-Belegung](docs/GAME_CONTROLLER_DB.md)
* [Modding](docs/MODDING.md)
* [Versionierung](docs/VERSIONING.md)

<a href="https://github.com/Kenix3/libultraship/">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./docs/poweredbylus.darkmode.png">
    <img alt="Powered by libultraship" src="./docs/poweredbylus.lightmode.png">
  </picture>
</a>
