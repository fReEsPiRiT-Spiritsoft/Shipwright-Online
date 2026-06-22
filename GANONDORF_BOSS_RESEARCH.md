# Ganondorf & Ganon Boss Research - OoT Synchronization

**Date:** 2026-06-22  
**Goal:** Understand the 2-part final boss encounter for implementing a sync adapter

---

## ⚠️ Important Note on Boss Confusion

The "final boss" sequence in OoT consists of **TWO different actors**:
- **NOT** a "flying Ganondorf" → "puppet Ganondorf"
- Instead: **Ganondorf (with sword)** → **Ganon (beast form)**

Other Ganondorf-related actors:
- **ACTOR_BOSS_GANONDROF (0x0052)** = "Phantom Ganon" (Forest Temple boss, NOT final boss)
- **ACTOR_BOSS_GOMA (0x0028)** = Queen Gohma (flying insect, Deku Tree boss, NOT final boss)

---

## 1. Actor IDs & Scene Constants

### Actor IDs
```c
/* Final Boss Part 1 - Ganondorf with sword */
ACTOR_BOSS_GANON    // 0x00E8 (232 decimal)
// Scene: SCENE_GANONDORF_BOSS
// Health: 40 HP (colChkInfo.health)

/* Final Boss Part 2 - Ganon Beast Form */
ACTOR_BOSS_GANON2   // 0x017A (378 decimal)
// Scene: SCENE_GANON_BOSS
// Health: varies (tracked via unk_324 field, 0.0-1.0 float)
```

### Scene Constants
```c
#define SCENE_GANONDORF_BOSS    0x19    // Ganondorf's Chamber
#define SCENE_GANON_BOSS        0x4F    // Ganon Beast form chamber
```

**Transitions:**
- Entrance `ENTR_GANONDORF_BOSS_*` use `TRANS_TYPE_FADE_BLACK`
- Entrance `ENTR_GANON_BOSS_*` use `TRANS_TYPE_FADE_WHITE`

---

## 2. BossGanon Structure (ACTOR_BOSS_GANON - Ganondorf)

**File:** `soh/src/overlays/actors/ovl_Boss_Ganon/z_boss_ganon.h`  
**Size:** 0x71C bytes

### Key Fields
```c
typedef struct BossGanon {
    /* 0x0000 */ Actor actor;                           // Standard actor header
    /* 0x014C */ s32 animBankIndex;                     // Animation bank for swapping objects
    /* 0x0150 */ SkelAnime skelAnime;                   // Skeletal animation
    /* 0x0194 */ BossGanonActionFunc actionFunc;        // Current action function pointer
    /* 0x0198 */ u8 unk_198;                            // Animation state flag (0=GANON_ANIME2, 2=GANON_ANIME1)
    /* 0x0199 */ u8 legSwayEnabled;                     // Leg sway during floating
    /* 0x01A0 */ s8 envLightMode;                       // Lighting environment mode (0-20)
    /* 0x01A2 */ s16 unk_1A2;                           // Timer
    /* 0x01AC */ s16 unk_1AC;                           // State machine counter
    /* 0x01AE */ s16 triforceType;                      // GDF_TRIFORCE_PLAYER/ZELDA/DORF
    
    // Combat state
    /* 0x01B6 */ s16 timers[5];                         // General timers for phases
    /* 0x01C0 */ u8 startVolley;                        // Tennis volley counter
    /* 0x01C4 */ s16 screenFlashTimer;                  // Flash effect duration
    
    // Visual effects
    /* 0x01C8 */ f32 fwork[GDF_FWORK_MAX];              // Floating work variables (10 slots)
    /* 0x01D0-0x024C */ Vec3f various_positions;        // Various tracking positions
    /* 0x0254 */ f32 handLightBallScale;                // Light ball size for hands
    
    // Combat mechanics
    /* 0x025C */ u8 unk_25C;                            // Attack phase indicator
    /* 0x0260 */ Vec3f unk_260;                         // Hand position (light ball origin)
    /* 0x027C */ Vec3f unk_278;                         // Chest/magic charge position
    
    // Cutscene/camera control
    /* 0x0674 */ u32 csTimer;                           // Cutscene timer (intro/death CS)
    /* 0x0678 */ s16 csState;                           // Cutscene state (0-109 for death CS)
    /* 0x067A */ s16 csCamIndex;                        // Subcamera index
    
    // Collision
    /* 0x0610 */ ColliderCylinder collider;             // Main body cylinder collider
    
    // Special states
    /* 0x066C */ u8 lensFlareMode;                      // Lens flare effect mode
    /* 0x0714 */ f32 whiteFillAlpha;                    // Screen white fill alpha (death sequence)
    /* 0x0718 */ s16 organAlpha;                        // Organ in background alpha
    /* 0x071A */ u8 useOpenHand;                        // Open vs closed hand animation flag
    /* 0x071B */ u8 windowShatterState;                 // Window destruction state (0-2)
} BossGanon;  // Total size: 0x71C
```

### Animation Objects Used
```c
OBJECT_GANON           // Main model
OBJECT_GANON_ANIME1    // Action animation bank 1
OBJECT_GANON_ANIME2    // Action animation bank 2 (intro cutscene)
OBJECT_GANON_ORGAN     // Background organ (intro only)
```

### Fwork Array (Special Variables)
```c
typedef enum {
    GDF_FWORK_0,                    // Floating height oscillation
    GDF_FWORK_1,                    // Animation frame counter/state
    GDF_CENTER_POS,                 // Center position radius
    GDF_TRIFORCE_PRIM_B,            // Triforce primary color B
    GDF_TRIFORCE_PRIM_A,            // Triforce primary color A
    GDF_TRIFORCE_ENV_G,             // Triforce environment green
    GDF_TRIFORCE_SCALE,             // Triforce scale
    GDF_VORTEX_ALPHA,               // Dark vortex alpha
    GDF_VORTEX_SCALE,               // Dark vortex scale
    GDF_FWORK_UNUSED_9,
    GDF_FWORK_MAX                   // = 10
} GanondorfFwork;
```

---

## 3. BossGanon2 Structure (ACTOR_BOSS_GANON2 - Ganon Beast)

**File:** `soh/src/overlays/actors/ovl_Boss_Ganon2/z_boss_ganon2.h`  
**Size:** 0x08E4 bytes

### Key Fields
```c
typedef struct BossGanon2 {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ SkelAnime skelAnime;
    /* 0x0190 */ BossGanon2ActionFunc actionFunc;
    
    // State tracking
    /* 0x019C */ s16 unk_19C;                           // Main action timer
    /* 0x01A2 */ s16 unk_1A2[5];                        // Multiple timers
    /* 0x01AC */ s16 unk_1AC;                           // State counter
    
    // Position/movement
    /* 0x01B8-0x0224 */ Vec3f unk_1B8[many];            // Position tracking arrays
    /* 0x0234 */ Vec3f unk_234[16];                     // Limb position tracking
    
    // Combat state
    /* 0x0310 */ u8 unk_310;                            // Attack phase
    /* 0x0313 */ u8 unk_313;                            // Look_on flag
    /* 0x0316 */ s16 unk_316;                           // no_hit_time (invulnerability)
    
    // Health tracking
    /* 0x0320 */ f32 unk_320;                           // Speed/current param
    /* 0x0324 */ f32 unk_324;                           // **HEALTH VALUE** (0.0 to 1.0 normalized)
                                                         // Referenced by EnZl3 when checking if defeated
    
    // Visual effects
    /* 0x032C */ f32 unk_32C;                           // Effect scale
    
    // Cutscene
    /* 0x0398 */ u32 csTimer;                           // Cutscene timer
    /* 0x039C */ s16 csState;                           // Cutscene state
    /* 0x039E */ s16 subCamId;                          // Subcamera ID
    
    // Collision (multiple colliders)
    /* 0x0424 */ ColliderJntSph unk_424;                // Joint sphere collider 1
    /* 0x0444 */ ColliderJntSph unk_444;                // Joint sphere collider 2
    /* 0x0464 */ ColliderJntSphElement unk_464[16];     // 16 collision elements
    /* 0x0864 */ ColliderJntSphElement unk_864[2];      // 2 additional elements
} BossGanon2;  // Size: 0x08E4
```

### Health Tracking
```c
// Ganon2 uses unk_324 as health (float, 0.0-1.0 range)
// This is checked by EnZl3 for phase detection:
if ((bossGanon2 != NULL) && (bossGanon2->unk_324 <= (10.0f / 81.0f))) {
    // Boss is near death
}
```

---

## 4. Action States & Phases

### ACTOR_BOSS_GANON (Ganondorf) Action Functions

**Combat Actions:**
```c
void BossGanon_Wait(BossGanon* this, PlayState* play);
void BossGanon_ChargeLightBall(BossGanon* this, PlayState* play);
void BossGanon_PlayTennis(BossGanon* this, PlayState* play);      // Reflect tennis minigame
void BossGanon_PoundFloor(BossGanon* this, PlayState* play);      // Jump-pound attack
void BossGanon_ChargeBigMagic(BossGanon* this, PlayState* play);  // Charge big magic attack
void BossGanon_Block(BossGanon* this, PlayState* play);           // Defensive block
void BossGanon_HitByLightBall(BossGanon* this, PlayState* play); // Damage from light ball
void BossGanon_Vulnerable(BossGanon* this, PlayState* play);     // Exposed to sword attack
void BossGanon_Damaged(BossGanon* this, PlayState* play);        // Taking damage
```

**Cutscene Actions:**
```c
void BossGanon_IntroCutscene(BossGanon* this, PlayState* play);      // Intro sequence (states 0-22)
void BossGanon_DeathAndTowerCutscene(...);                           // Death/tower collapse (states 0-109)
void BossGanon_SetupTowerCutscene(...);                              // Alternate tower intro
```

**Special Properties:**
- **Initialization params:** `thisx->params < 0x64` = main boss instance
- **Light ball objects:** `params >= 0x64` = spawned light balls for attacks
- **Magic charge objects:** `params >= 0xC8` = magic charge effects

### ACTOR_BOSS_GANON2 (Ganon Beast) Action Functions
```c
// Actions not fully enumerated but include:
// - Standard attack patterns
// - Special move sequences
// - Vulnerable phase transitions
// - Death sequence
```

---

## 5. Phase Transition Mechanics

### Ganondorf (ACTOR_BOSS_GANON)

**Health System:**
```c
// Initialization
thisx->colChkInfo.health = 40;  // 40 HP maximum

// Health check
if (thisx->colChkInfo.health <= 0) {
    // Transition to death cutscene
    BossGanon_SetupDeathCutscene(this, play);
}
```

**Defeat/Death Sequence:**
1. When health reaches 0, cutscene `BossGanon_DeathAndTowerCutscene()` begins
2. Cutscene states: 0-22 for intro, 1-109 for actual death sequence
3. States include:
   - **State 0-3:** Initial positioning and animation setup
   - **States 4-20:** Ganondorf reactions and dialogue
   - **State 22:** Tower collapse begins
   - **States 100+:** In tower collapse scene (SCENE_GANONS_TOWER_COLLAPSE_*)
4. **Final transition:** White fade to next scene at csTimer == 180

**Death Detection by GenericBossHealthPhaseAdapter:**
```c
int ComputePhaseByHp(uint8_t hp, uint8_t initialHp) {
    float ratio = (float)hp / (float)std::max<uint8_t>(initialHp, 1);
    if (hp == 0) return 3;              // Phase 3 = DEFEATED
    if (ratio <= 0.33f) return 2;       // Phase 2 = Critical (~13 HP)
    if (ratio <= 0.66f) return 1;       // Phase 1 = Injured (~20 HP)
    return 0;                           // Phase 0 = Healthy (21-40 HP)
}
```

### Scene Transition to Ganon Beast

**When Ganondorf Dies:**
```c
// The scene automatically transitions from SCENE_GANONDORF_BOSS
// to SCENE_GANON_BOSS (with tower collapse intermediary scenes)

// New actor spawned: ACTOR_BOSS_GANON2
// Located at: soh/src/overlays/actors/ovl_Boss_Ganon2/

// Health type changes:
// ACTOR_BOSS_GANON:   colChkInfo.health (u8, 0-40)
// ACTOR_BOSS_GANON2:  unk_324 (f32, 0.0-1.0 normalized)
```

---

## 6. Combat Mechanics

### Ganondorf Attacks

**Attack Types:**
1. **Light Ball Charges** (magic projectiles)
   - Spawns as child actor with params 0x64-0xC7
   - Can be reflected back via tennis minigame
   - Multiple damage balls possible

2. **Big Magic Charge** (massive spell)
   - Spawns as child actor with params >= 0xFA
   - Larger damage radius
   - Multiple spawn variants

3. **Pound Floor**
   - Jump-pound attack for area damage
   - Creates shockwave effect

4. **Tennis Volley**
   - Player must reflect light ball back at Ganondorf
   - Stuns for vulnerable phase
   - Critical damage opportunity

### Invulnerability/Vulnerability

**Ganondorf:**
- Invulnerable during most actions
- Only vulnerable after light ball is reflected back
- Vulnerable state triggered by `BossGanon_HitByLightBall()` → `BossGanon_Vulnerable()`

**Ganon Beast (ACTOR_BOSS_GANON2):**
- `unk_316` = "no_hit_time" (invulnerability frames)
- Cycles through vulnerable/invulnerable phases

### Sword vs Magic

**Ganondorf:**
- Uses sword for melee attacks
- Primary damage from light ball reflection, not sword strikes
- Sword appears as prop in hand during combat

**Ganon Beast:**
- Purely melee attacks with sword
- Direct sword combat (not magic reflection-based)

---

## 7. Form Detection & Parameters

### Actor.params Usage

**ACTOR_BOSS_GANON:**
```c
if (thisx->params < 0x64) {
    // Main Ganondorf boss
    this->actor.colChkInfo.health = 40;
} else if (thisx->params >= 0xFA) {
    // Big magic light ball (0xFA+)
    this->unk_1A2 = 520 + (-thisx->params * 2);
} else if (thisx->params >= 0xC8) {
    // Magic light ball charge (0xC8+)
} else {
    // Regular light ball (0x64-0xC7)
}
```

### No Direct "Form" Field

Ganondorf doesn't have a form indicator. Instead:
- Main instance: `actor.params < 0x64`
- Determines form by checking action function pointer
- Uses `unk_198` for animation bank selection:
  - `0` = GANON_ANIME2 (intro cutscene animations)
  - `2` = GANON_ANIME1 (combat animations)

---

## 8. Scene Constants & Entrance Points

### Ganondorf Boss Scene
```c
SCENE_GANONDORF_BOSS = 0x19
Entrances:
- ENTR_GANONDORF_BOSS_0 (0x41F)
- ENTR_GANONDORF_BOSS_0_1 (0x420)
- ENTR_GANONDORF_BOSS_0_2 (0x421)
- ENTR_GANONDORF_BOSS_0_3 (0x422)

Transition: TRANS_TYPE_FADE_BLACK
```

### Ganon Beast Scene
```c
SCENE_GANON_BOSS = 0x4F
Entrances:
- ENTR_GANON_BOSS_0 (0x517)
- ENTR_GANON_BOSS_0_1 (0x518)
- ENTR_GANON_BOSS_0_2 (0x519)
- ENTR_GANON_BOSS_0_3 (0x51A)
- ENTR_GANON_BOSS_0_4 (0x51B)

Transition: TRANS_TYPE_FADE_WHITE
```

---

## 9. Sync Adapter Architecture

### GenericBossHealthPhaseAdapter Coverage

**Handles:**
```c
case SCENE_GANONDORF_BOSS:
case SCENE_GANON_BOSS:
    // With actors:
    case ACTOR_BOSS_GANON:        // ✓ Supported
    case ACTOR_BOSS_GANONDROF:    // ✓ Supported (Phantom Ganon)
    case ACTOR_BOSS_GANON2:       // ✗ NOT supported in GenericBossHealthPhaseAdapter
```

### Health/Phase Tracking

```json
{
  "actor_key": "19_0_232_0_0_...",  // SCENE_GANONDORF_BOSS + ACTOR_BOSS_GANON
  "hp": 40,                          // colChkInfo.health
  "phase": 0,                        // ComputePhaseByHp result (0-3)
  "timestamp": 1234567890
}
```

### Phase Transitions Captured
- Phase 0 → 1 (health drops to ~26)
- Phase 1 → 2 (health drops to ~13)
- Phase 2 → 3 (health reaches 0, defeated)

---

## 10. Special Mechanics

### Cutscene Integration

**Intro Cutscene (csState 0-22):**
- Ganondorf plays organ
- Zelda heals player
- Triforce display
- Tower transformation begins
- **Final state 22:** Boss becomes active, fight starts

**Death Cutscene (csState 0-109):**
- Ganondorf's demise animations
- Zelda reactions
- Tower collapse sequences
- Zelda rescue sequence
- Scene transition to tower collapse

### Environmental Effects

**Window Shattering:**
```c
typedef enum {
    GDF_WINDOW_SHATTER_OFF,      // No shatter
    GDF_WINDOW_SHATTER_PARTIAL,  // Partial destruction
    GDF_WINDOW_SHATTER_FULL      // Complete destruction
} WindowShatterState;
// Progressive texture damage during death sequence
```

**Lighting Modes:**
```c
envLightMode values (0-20):
// 0 = Normal
// 1 = Combat highlight
// 2 = Magic charge glow
// 3-20 = Various cutscene lighting states
```

### Special Effects Management

**GanondorfEffect Array:**
```c
#define GDF_EFF_NONE              0
#define GDF_EFF_SPARKLE           1
#define GDF_EFF_LIGHT_RAY         2
#define GDF_EFF_SHOCK             3
#define GDF_EFF_LIGHTNING         4
#define GDF_EFF_IMPACT_DUST_DARK  5
#define GDF_EFF_IMPACT_DUST_LIGHT 6
#define GDF_EFF_SHOCKWAVE         7
#define GDF_EFF_BLACK_DOT         8
#define GDF_EFF_WINDOW_SHARD      9

// Managed in play->specialEffects (GanondorfEffect sBossGanonEffectBuf[200])
```

---

## 11. Key Sync Considerations

### For Implementation

**What to Sync:**
- ✓ `colChkInfo.health` (Ganondorf HP 0-40)
- ✓ `actionFunc` pointer (current action state)
- ✓ Position and rotation
- ✓ Animation frame progress
- ✓ Cutscene state (`csState`) during intro/death

**What NOT to Sync (Client-Side):**
- Visual-only: `envLightMode`, `windowShatterState`, effect particles
- Audio: sound effects, music cues
- Animation-only: skelAnime frame data

**Scene Transitions:**
- Scene change happens automatically when health reaches 0
- New actor (ACTOR_BOSS_GANON2) spawns in SCENE_GANON_BOSS
- May require separate adapter for phase 2 (beast form)

### Multi-Client Considerations

1. **Intro Cutscene:** Should skip on subsequent clients if `EVENTCHKINF_BEGAN_GANONDORF_BATTLE` set
2. **Health Sync:** Use `colChkInfo.health` directly (0-40 range)
3. **Phase Transitions:** Track via `ComputePhaseByHp()` internally
4. **Scene Transition:** Automatic; new adapter needed for SCENE_GANON_BOSS

---

## 12. File Locations

```
Source Files:
- soh/src/overlays/actors/ovl_Boss_Ganon/z_boss_ganon.h
- soh/src/overlays/actors/ovl_Boss_Ganon/z_boss_ganon.c
- soh/src/overlays/actors/ovl_Boss_Ganon2/z_boss_ganon2.h
- soh/src/overlays/actors/ovl_Boss_Ganon2/z_boss_ganon2.c (not found, likely in assets)

Sync Reference:
- soh/soh/Network/Anchor/BossSync/GenericBossHealthPhaseAdapter.cpp

Definitions:
- soh/include/tables/scene_table.h
- soh/include/tables/entrance_table.h
- soh/include/tables/actor_table.h
```

---

## Summary

**Final Boss Encounter Structure:**
- **Part 1:** ACTOR_BOSS_GANON in SCENE_GANONDORF_BOSS (40 HP, 4 phases)
- **Part 2:** ACTOR_BOSS_GANON2 in SCENE_GANON_BOSS (float-based HP, separate mechanics)

**Sync Challenges:**
- Phase detection via HP thresholds (working model exists)
- Scene transition requires new actor tracking
- Cutscene state must be considered (intro skipping on rejoins)
- Two different health systems (u8 vs f32)

**Recommendations:**
- Extend GenericBossHealthPhaseAdapter OR create specialized GanondorfAdapter
- Handle SCENE_GANON_BOSS separately for Ganon2
- Detect scene transition to initialize new boss sync state
- Track cutscene states for intro sequence synchronization
