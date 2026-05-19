### 🌐 Architectural Specification: Dynamic Network Sync, Multi-Mode Item Exchange & Epona Fallback

Claude, we are expanding our custom multiplayer repository `Shipwright-Online`. We need to implement a robust, performance-optimized networking architecture that supports dynamic feature gating, an automatic room reset, and a highly immersive physical item exchange system that intelligently adapts to whether players are on foot or riding Epona.

Please design the implementation architecture and provide the necessary C++ hooks for the following specifications:

---

### 1. Hybrid Server-Capability Handshake
The client must dynamically adapt its feature set based on the connected server backend without breaking compatibility with standard Anchor relay servers.
* **The Handshake Packet:** Upon connection, listen for a custom `PACKET_SERVER_CAPABILITIES` packet. If received within a 3-second timeout, set the global state variable `gIsCustomBackendActive = true`. If it times out (standard Anchor server), leave it `false`.
* **UI Gating:** In the Admin Panel UI, any persistence/advanced toggles (like "Offline Database Sync") must be automatically greyed out or disabled if `gIsCustomBackendActive == false`, displaying a label: *(Requires Custom Server Backend)*.

---

### 2. Enemy Sync Toggle & Instant Room Reset Fix
* **The Problem:** Turning the enemy sync (`gNetworkEnemySyncEnabled`) ON mid-room causes existing actors to become visually/physically desynced because they were loaded into memory without network entity assignments (`Room_Setup`).
* **The Solution:** Implement a network hook. The exact moment the user toggles `gNetworkEnemySyncEnabled` (from OFF to ON or vice versa), the code must instantly trigger a native, seamless **Room Reset / Scene Reload** command (e.g., reloading the current Room ID in the background). This forces the engine to clear, respawn, and correctly link all enemies to the host-authority network layer from frame 1.

---

### 3. Physical Item Exchange Modus (`gNetworkPhysicalItemExchange`)
When this mode is enabled, instant global item syncing is suppressed. Items found for an offline partner are queued. When two connected players physically approach each other within a **1-meter radius**, a hard transaction trigger fires, disabling manual player control and executing one of the two cinematic exchange pipelines:

#### MODE A: On-Foot Exchange (Both players are walking)
1. **The Giver:** The giving Link is forced into the native `Item_Show` animation (presenting the object on an outstretched hand) or the classic throwing animation. The giver's state is locked/frozen in this pose.
2. **The Receiver:** The receiving Link is forced into the native `Player_SetGetItem` lift-animation, the original music fanfare plays, and a dynamic game textbox opens: **'Du hast von [Spieler_Name] das Item [Item_Name] erhalten!'**
3. **Release:** Control is only restored to both players after the text dialogue is fully cleared. If multiple items are in the queue, process them sequentially to prevent animation-lock engine crashes.

#### MODE B: Epona Exchange (One or both players are riding)
Since the vanilla engine blocks standard item-throwing animations while riding Epona, implement this highly immersive "Magic Show" fallback:
1. **The Proximity Lock:** Upon entering the 1-meter trigger, both Eponas instantly stop moving, execute a native "neighing" (Wiehern) animation, and manual steering is completely blocked.
2. **The Floating Proxy:** Instead of a body animation from the giver, spawn a small 3D item model (or a glowing magical particle effect) at the giver's chin level. Programmatically move this object along a smooth sine-wave parabola through the air towards the receiver.
3. **The Impact & Celebration:** The moment the floating proxy hits the receiver's collision box, it vanishes. The receiving Link on Epona is instantly forced into the beritten-safe "Item-Gefunden" animation (the native pose used when learning Epona's Song in the saddle), the fanfare plays, and the dynamic textbox opens: **'Du hast von [Spieler_Name] das Item [Item_Name] erhalten!'**
4. **Release:** The giver remains in a passive, clean riding stance until the receiver clears the textbox, after which both horses are released back to player control.

---

### Your Task:
Please generate the comprehensive C++ structural framework, state machine logic, and engine function hooks required to achieve this system. Show how the handshake toggles the UI, how the room reload function is cleanly executed on button-press, and how the 1-meter item exchange seamlessly branches into either the On-Foot or Epona-Floating-Proxy script depending on the players' current riding states.