"Wir fügen zwei Slider und einen Button im Anchor-Admin-Panel hinzu: Einen Button zum kompletten Ein- und Ausschalten des gesamten Gegner-Syncs, gNetworkSyncRadius für die Distanz-Blase und gNetworkTickRate (Optionen: 20Hz, 10Hz, 5Hz) für die Frequenz.

Bitte setze die Logik im Hook im Detail wie folgt um:

    Hauptschalter: Ist der Button ausgeschaltet, wird der gesamte Sync-Hook sofort abgebrochen und das Spiel läuft komplett im Vanilla-Singleplayer-Modus.

    Status-Sync (HP & Kills): Ist der Button aktiv, wird dieser Datenstrom IMMER sofort und global im selben Raum übertragen – völlig unabhängig von den Slidern.

    Ausserhalb des gNetworkSyncRadius: Befindet sich ein Gegner außerhalb des Radius zum Client-Link, wird der Echtzeit-Positions- und Animations-Sync für diesen Actor komplett blockiert. Der Client steuert den Gegner temporär über seine eigene, lokale KI. Stirbt der Gegner dort oder verliert HP, greift Punkt 2 (HP/Killstate-Sync wird trotzdem gefunkt, um das Wiederaufploppen später zu verhindern).

    Innerhalb des gNetworkSyncRadius: Befindet sich der Gegner innerhalb des Radius, schaltet das System auf vollen Host-Authority-Sync um. Die Positions- und Animationsdaten werden erzwungen, allerdings gedrosselt durch die gNetworkTickRate: Bei 20Hz in jedem Frame, bei 10Hz nur jeden 2. Frame und bei 5Hz nur jeden 4. Frame, basierend auf dem globalen Frame-Counter der Engine (gPlayState->state.frames)."
