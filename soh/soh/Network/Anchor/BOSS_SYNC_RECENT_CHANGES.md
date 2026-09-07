# BOSS SYNC — Letzte Änderungen & Begründung

Datum: 2026-09-07

Zweck: Diese Datei fasst die zuletzt vorgenommenen Änderungen am Anchor/BossSync-System zusammen, erklärt das Warum und gibt eine kurze Anleitung für weitere Arbeiten.

**Kurzüberblick**
- Ziel: Boss-/Dungeon-Objekt- und Puzzle-Synchronisation robuster machen, Action-State (Zielwahl/Lochwahl) für bestimmte Bosse synchronisieren, GitHub Actions / Linux-Build-Fehler beseitigen.
- Fokusänderung: Von CI/Build-Fixes → Anchor-Hardening → Per-Boss-Action-State-Sync (Volvagia, Morpha) → Dodongo-Health-Fix.

**Wesentliche Änderungen (Dateien/Module)**
- Anchor Hook & Packets: HookHandlers.cpp, RoomEvent.cpp, ActorKilled.cpp — sichere Behandlung von Raum-Events (z.B. WEB_BURNED), Burn-Logik entflochten von nicht-exportierten Funktionen.
- BgKeyframeSync.cpp — GET_PLAYER-Makro entfernt; Actor-List-Access genutzt; TTL hinzugefügt, damit Clients lokal übernehmen können, wenn Master ausbleibt.
- UIWidgets.hpp — `longest` initialisiert zur Behebung von -Wmaybe-uninitialized.
- BossSync Adapters: mehrere Adapter aktualisiert / neu hinzugefügt:
  - `KingDodongoAdapter.cpp` — liest 16-bit Health-Feld korrekt (s16) und vermeidet Health-Wraparound.
  - `GenericBossHealthPhaseAdapter.cpp`, `VolvagiaAdapter.cpp`, `MorphaBossAdapter.cpp`, u.a. — Wiederherstellung von CaptureTransition-Signaturen, Health-Clamping, und Volvagia/Morpha Action-State-Sync.
- BossSync Registry — Dodongo-Adapter vor generischem Fallback registriert.

**Technische Begründung / Rationale**
- Viele Bosszustände leben in nicht-standardisierten Feldern (work[], params, action/state). Für korrekte Multiplayer-Repräsentation müssen wir:
  1) das richtige Feld lesen (z.B. s16 vs float),
  2) Werte gegen Überlauf/wrapping schützen (signed-clamped reader), und
  3) deterministische Entscheidungszustände (z.B. Zielwahl, Lochwahl) als einmalige Raum-Events (`BOSS_ACTION_STATE`) senden, damit Late-Join/Clients dieselbe Attacke sehen.
- Warum Events statt Aufrufe: Direkte Funktionsaufrufe in Boss-Code sind oft nicht exportiert oder ändern intern Verhalten; Events + Feldüberschreibung sind weniger invasiv und wiederholbar.

**Bekannte Einschränkungen / Risiken**
- Nicht alle Bosse sind vollständig auf Action-State-Sync geprüft — bisher Volvagia & Morpha umgesetzt; andere (Barinade, Bongo Bongo, Twinrova, Ganon2 etc.) brauchen per-Boss-Analyse.
- In manchen Fällen ist es nötig, Race-Conditions zu handhaben, wenn HP-Phase-Wechsel und Action-State-Änderungen in derselben Frame auftreten. Für kritische Bosse muss die Reihenfolge sichergestellt werden.

**Testing / Verifikation**
- Lokaler Kompilations-Check (get_errors) wurde nach Änderungen ausgeführt — keine Compiler-Fehler.
- Laufzeittests: Multiplayer-Playtesting erforderlich, besonders für Boss-Angriffswahl, Gwebburning/Portal-Flags und Raum-Events bei Late-Join.

**Nächste Schritte / TODOs**
1. Template: Übertragen des `BOSS_ACTION_STATE`-Eventmusters auf weitere Bosse; pro Boss:
   - Lokalisieren des wirklichen Action-State-Felds
   - Entweder Feld-Write (sicher) oder kleiner Shim
2. Review: Szenarien testen, in denen HP-Phase und Action-State gleichzeitig wechseln.
3. CI: vollständigen Linux/GCC-Build in GitHub Actions verifizieren.

**Referenzen & Notizen**
- Vollständige Änderungsliste und Diskussion sind in der Copilot-Transkriptdatei (lokal):
  `/home/patricks/.config/Code/User/workspaceStorage/65ffeff8ac87fdd6e9ab605a25badbea/GitHub.copilot-chat/transcripts/14891b3b-a029-4713-9585-737676afb968.jsonl`
- Relevante Quellpfade: `soh/soh/Network/Anchor/*`, `soh/soh/Network/Anchor/BossSync/*`.

Wenn du möchtest, kann ich jetzt eine TODO-Liste für die nächsten Bosse (Gohma, Barinade, BongoBongo...) erstellen und mit der Analyse beginnen.
