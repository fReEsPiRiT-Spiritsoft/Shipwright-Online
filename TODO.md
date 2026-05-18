"Der Kill- und Positions-Sync vom Host zum Client steht. Problem jetzt: Der Client kann den Gegnern keinen Schaden zufügen, da ihre KI dort gefroren ist und der Host nichts vom Angriff des Clients weiß.

Wir müssen ein Damage-Packet über Anchor implementieren:

    Wenn der Client mit seinem Schwert-Collider (ColliderCylinder / acHit) einen Actor trifft, darf er den Schaden nicht lokal berechnen.

    Der Client muss ein Netzwerk-Paket mit der actorKey (oder Szene + Actor-ID) und dem Schadenswert an den Host senden.

    Der Host muss dieses Paket empfangen, den passenden Actor in seiner Linked-List suchen und den Schaden über die originale Engine-Funktion (z.B. DamageTable oder direktes Abziehen von colChkInfo.health) applizieren.

Schreibe uns den C++ Code für diesen Client-to-Host Damage-Hook."



