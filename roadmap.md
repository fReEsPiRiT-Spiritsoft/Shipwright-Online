🪲 Kategorie 1: Der Epona-Crash (Dein Log-File)
Der Phantom-Pferd-Absturz (Signal: 11 INVALID ACCESS TO STORAGE)

    Was passiert: Sobald ein Phantom-Pferd für einen Mitspieler auf dem Hyrule-Feld gespawnt wird, schmiert das Spiel Sekunden später mit einem Speicherfehler ab.

    Die Ursache laut Log: Der Crash passiert in Skin_UpdateVertices -> EnHorseNormal_Draw. Du hast die KI des Pferdes stummgeschaltet (update = nullptr), aber das Pferd versucht beim Zeichnen (Draw) auf Animationsdaten und Skelett-Berechnungen (Skin-Limb) zuzugreifen, die ohne die Update-Schleife nie initialisiert wurden oder im Speicher leer sind.

    Die Lösung: Das Phantom-Pferd darf kein update = nullptr haben. Es braucht eine minimale Update-Funktion, die zumindest das Skelett- und Animations-Framework im Speicher warmhält (SkelAnime_Update), während die eigentliche Reit-KI blockiert bleibt.

🗺️ Kategorie 2: Welt- & Zeit-Synchronisation
Zoras Wasserfall & Lord Jabu-Jabu öffnen sich nicht synchron

    Das Problem: Nur der Raum-Master kann den Wasserfall (durch Zeldas Wiegenlied) oder Jabu-Jabus Maul (durch Fisch-Spende) öffnen. Für Mitspieler bleibt der Weg versperrt.

    Die Ursache: Das sind keine normalen "Gegner". Das sind Event-Zustände. Wenn der Nicht-Master die Flöte spielt oder den Fisch wirft, blockiert dein System die Interaktion, weil er nicht die Raum-Autorität besitzt.

    Die Lösung: Diese spezifischen Trigger müssen als ROOM_EVENT abgefangen und an den Raum-Master weitergeleitet werden, damit dieser die Sequenz für alle öffnet.

Die Zugbrücke & Nacht-Skelette (Time-Sync Bug)

    Das Problem: Die Zugbrücke geht asynchron hoch/runter, und die Skelette nachts spawnen wild durcheinander.

    Die Ursache: Euer Time-Sync läuft im Sekundentakt. Da die Engine die Zugbrücke und die Skelette direkt anhand der internen Uhrzeit berechnet, führen minimale Latenzen im Netzwerk dazu, dass die Brücke auf Client A schon zu ist, während Client B sie noch offen sieht.

    Die Lösung: Wie von dir vorgeschlagen: Beim Serverbeitritt die Uhrzeit nur einmal hart abgleichen. Danach läuft die Zeit auf jedem PC lokal weiter. Für die Zugbrücke und die Skelette zählt im BR- oder Koop-Modus nur noch das Signal des Hosts.

🐋 Kategorie 3: Der Jabu-Jabu- & Ruto-Albtraum
Jabu-Jabu Zwischenboss taucht nicht auf / Boss unkillbar

    Das Problem: Der Tentakel-Zwischenboss spawnt für Nicht-Auslöser gar nicht erst. Der Hauptboss (Barinade) wird durch den Gegner-Sync unsterblich.

    Die Ursache: Bosse bestehen oft aus mehreren Actor-Teilen (Körper, Tentakel, Hitboxen). Wenn der Gegner-Sync die HP oder Positionen synchronisiert, funkt das dem komplexen N64-Boss-Skript dazwischen. Ein Client meldet "Teil A ist tot", der andere sagt "Nein, Teil A lebt noch" – der Boss verfängt sich in einer unendlichen Logikschleife.

    Die Lösung: Bosse (ACTORCAT_BOSS) müssen komplett vom normalen Gegner-Sync ausgenommen werden! Hier darf nur der Raum-Master den Boss berechnen und streamen, die Clients übernehmen seine Werte blind ohne eigene Schadensprüfung.

Prinzessin Ruto unsichtbar / Tragen asynchron

    Das Problem: Nur der Spieler, der die Cutscene auslöst, sieht Ruto. Versucht man sie zu tragen, glitcht das Spiel komplett.

    Die Ursache: Ruto ist als Actor (En_Ru1) extrem eng mit globalen Event-Flags verknüpft. Wenn Spieler A sie aufhebt, weiß der PC von Spieler B nichts davon, weil das "Tragen" ein Spieler-Zustand ist, kein Gegner-Zustand.

    Die Lösung: Ruto darf nicht über den Gegner-Sync laufen. Ihr Zustand (wird getragen / sitzt rum / ID-Status) muss als dediziertes ROOM_EVENT synchronisiert werden.

🎮 Kategorie 4: Blockiertes Gameplay für Clients
Nicht-Raum-Master können nicht mit Töpfen, Kisten und Pferden interagieren

    Das Problem: Nur der Raum-Master kann Krüge zerschlagen, Truhen öffnen oder auf Pferde steigen.

    Die Ursache: Dein Maulkorb (Engine-Sperre) greift zu hart. Töpfe und Kisten liegen oft in der Kategorie ACTORCAT_BG oder PROP. Da dein System Updates für Nicht-Master in diesen Kategorien blockiert, reagieren die Objekte schlichtweg nicht auf die Schwerthiebe oder Interaktionen der Clients.

    Die Lösung: Die Interaktions-Reaktion (Collision-Check) muss für den lokalen Spieler immer erlaubt sein. Erst wenn das Objekt zerstört oder geöffnet wird, sendet der Client das Signal an den Raum-Master.

Lieder spielen hat keine Auswirkung

    Das Problem: Wenn ein Nicht-Raum-Master eine Melodie spielt (z.B. die Hymne der Zeit), passiert in der Welt nichts.

    Die Ursache: Die Engine prüft beim Beenden des Liedes, ob ein passender Actor im Raum ist, der darauf reagiert. Da die Updates dieser Actors für den Client aber blockiert sind, "schlafen" die Objekte und hören das Lied nicht.

    Die Lösung: Das erfolgreiche Abspielen eines Liedes muss ein globales Event an den Raum-Master feuern ("Spieler X hat Lied Y gespielt"). Der Raum-Master führt den Effekt dann in seiner authoritativen Welt aus.

Das Schwert in der Zitadelle der Zeit ziehen

    Das Problem: Nur der Raum-Master kann das Master-Schwert ziehen, um erwachsen zu werden.

    Die Ursache: Das Ziehen des Schwerts verändert den globalen Savegame-Status und triggert eine massive Cutscene. Da der Client blockiert ist, schlägt der Trigger fehl.

    Die Lösung: Die Zitadelle der Zeit muss eine absolute "Safe-Zone" ohne jegliche Raum-Master-Einschränkung sein. Hier muss jeder Client lokal vollen Zugriff auf das Schwert haben.

🏅 Kategorie 5: Quests & Minispiele
Zora-Tauchspiel & Tauchschuppe asynchron

    Das Problem: Das Tauchspiel startet nicht für alle, und die gewonnene Tauchschuppe wird im Netzwerk nicht an die Mitspieler übergeben.

    Die Ursache: Minispiele nutzen interne Timer und Zähler im RAM der Engine. Wenn Spieler A die Rubine einsammelt, erhöht sich nur sein lokaler Zähler. Die Schuppe ist ein Item-Give-Event, das von deinem aktuellen System fälschlicherweise blockiert wird, wenn syncItemsAndFlags nicht exakt greift.

    Die Lösung: Das Tauchspiel muss über den Minigame-Event-Kanal laufen. Das Einsammeln eines Rubins muss an alle gemeldet werden.

Cutscenes bleiben hängen (Softlock)

    Das Problem: Eine Zwischensequenz startet, Mitspieler bekommen Kinobalken, bleiben aber bewegungsunfähig stecken und die Cutscene endet nie.

    Die Ursache: Die Kamera-Funktion der Engine (func_80064520) friert die Spieler ein, wartet dann aber darauf, dass der lokale PC die Cutscene-Befehle weiterschaltet. Da der Client die Cutscene aber gar nicht richtig geladen hat, wartet er ewig auf das "Ende"-Signal.

    Die Lösung: Wenn eine Cutscene raumweit synchronisiert wird, muss der Raum-Master nicht nur den Start, sondern auch die exakte Frame-Länge oder ein explizites CUTSCENE_END-Paket an alle Clients senden, um sie gewaltsam aus dem Freeze zu befreien.