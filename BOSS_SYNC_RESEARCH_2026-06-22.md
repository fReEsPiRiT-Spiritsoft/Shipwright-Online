# Boss Actor Remote Sync - Modifiable Fields Research
**Date**: 2026-06-22  
**Purpose**: Determine which actor fields can be safely modified from remote event handlers (ApplyEvent()) for each boss type without breaking actor lifecycle or state machine.

---

## Executive Summary

### Key Findings
1. **All bosses use `actor.colChkInfo.health` for health** except Ganon2 (uses custom normalized float at offset 0x324)
2. **State machines are driven by work[] s16 arrays** - these CAN be modified safely to drive state transitions
3. **Subactor management uses params=-1 for boss, other params for parts** - can sync by setting part health=0
4. **Function pointers (actionFunc) must NOT be modified directly** - instead modify timers to let state machine progress
5. **Position/rotation should NOT be modified** (breaks collision sync)
6. **Animation system should NOT be touched** (breaks frame interpolation)

### Safe Modification Pattern
```c
// SAFE: Modify health and state machine via work[] timers
boss->actor.colChkInfo.health = newHealth;
boss->work[STATE_INDEX] = newStateValue;
boss->work[TIMER_INDEX] = newTimerValue;
boss->invincibilityTimer = newInvulnTimer;  // if exists

// SAFE: Kill subactors
for (Actor* node = play->actorCtx.actorLists[ACTORCAT_BOSS].head; node; node = node->next) {
    if (node->params == subactorParam && node->room == room) {
        node->colChkInfo.health = 0;
        break;
    }
}

// NOT SAFE: Don't do this
boss->actionFunc = newFunction;  // NO - breaks state machine
boss->actor.world.pos = newPos;  // NO - breaks collision
boss->skelAnime.animIdx = newAnim;  // NO - breaks interpolation
```

---

## Per-Boss Analysis

### 1. ACTOR_BOSS_VA (Barinade)
**Structure**: [z_boss_va.h](z_boss_va.h) - Size 0x03B8

#### Modifiable Fields
| Field | Type | Offset | Note |
|-------|------|--------|------|
| `actor.colChkInfo.health` | u8 | +0x1BC | Primary health |
| `timer` | s16 | +0x0198 | State machine timer |
| `timer2` | s16 | +0x019C | Variant timer |
| `burst` | u8 | +0x0195 | Visual state flag |
| `onCeiling` | u8 | +0x0194 | Position state flag |
| `invincibilityTimer` | s8 | +0x0196 | Invuln window |

#### Subactor System
- **Body**: params = -1
- **Supports**: params = 0, 1, 2 (enemies holding the boss up)
- **Zappers**: params = 3, 4, 5 (electric hazards)
- **Bari Parts**: params = 6-15 (body segments)
- **Stumps**: params = 16-18 (arena pillars)

**Kill Pattern**:
```c
// Iterate ACTORCAT_BOSS list, find subactor by params
for (Actor* sub = play->actorCtx.actorLists[ACTORCAT_BOSS].head; sub; sub = sub->next) {
    if (sub->id == ACTOR_BOSS_VA && sub->params == SUPPORT_PARAM && sub->room == room) {
        sub->colChkInfo.health = 0;  // Trigger death sequence
        break;
    }
}
```

#### DO NOT Modify
- `actionFunc` (function pointer)
- `actor.world.pos/rot` (position/rotation)
- `skelAnime` structure

---

### 2. ACTOR_BOSS_MO (Morpha)
**Structure**: [z_boss_mo.h](z_boss_mo.h) - Size 0x158C

#### Modifiable Fields
| Field | Type | Offset | Note |
|-------|------|--------|------|
| `actor.colChkInfo.health` | u8 | +0x1BC | Core health |
| `work[MO_CORE_ACTION_STATE]` | s16 | +0x0158+0 | State machine |
| `work[MO_CORE_MOVE_TIMER]` | s16 | +0x0158+2 | Move timer |
| `work[MO_CORE_INVINC_TIMER]` | s16 | +0x0158+8 | Invuln timer |
| `drawActor` | u8 | +0x01D1 | Visibility flag |

#### Tentacle System
- **Core**: params = -1, acts as primary boss
- **Tentacles**: params = 100, appear as attack/grab appendages
- **Linker**: `actor.otherTent` (Actor*) at +0x014C points to tentacle

**Tentacle Kill Pattern**:
```c
// Find tentacle actors
for (Actor* tent = play->actorCtx.actorLists[ACTORCAT_BOSS].head; tent; tent = tent->next) {
    if (tent->id == ACTOR_BOSS_MO && tent->params == 100) {
        tent->actor.colChkInfo.health = 0;
    }
}
```

#### DO NOT Modify
- `actionFunc`
- `actor.world.pos/rot`
- Tentacle position arrays
- Collider structures

---

### 3. ACTOR_BOSS_SST (Bongo-Bongo)
**Structure**: [z_boss_sst.h](z_boss_sst.h) - Size 0x0A98

#### Modifiable Fields
| Field | Type | Offset | Note |
|-------|------|--------|------|
| `actor.colChkInfo.health` | u8 | +0x1BC | Health (head/hands) |
| `timer` | s16 | +0x0198 | Main state timer |
| `ready` | s8 | +0x0195 | Ready flag |
| `handAngSpeed` | s16 | +0x019A | Hand angular speed |
| `handMaxSpeed` | s16 | +0x019C | Max speed |

#### Head/Hand System
- **Head**: params = -1, main actor
- **Left Hand**: params = 0, destroyable limb
- **Right Hand**: params = 1, destroyable limb

**Hand Status**:
```c
// Check which hands are alive
for (Actor* part = play->actorCtx.actorLists[ACTORCAT_BOSS].head; part; part = part->next) {
    if (part->id == ACTOR_BOSS_SST) {
        if (part->params == 0 && part->actor.colChkInfo.health > 0) {
            // Left hand alive
        }
        if (part->params == 1 && part->actor.colChkInfo.health > 0) {
            // Right hand alive
        }
    }
}

// Destroy hand
hand->actor.colChkInfo.health = 0;
```

#### DO NOT Modify
- `actionFunc`
- Skeleton animation system
- Position/rotation

---

### 4. ACTOR_BOSS_FD / ACTOR_BOSS_FD2 (Volvagia)
**Structure**: [z_boss_fd.h](z_boss_fd.h) & [z_boss_fd2.h](z_boss_fd2.h)

#### Modifiable Fields (FD - Flying)
| Field | Type | Offset | Note |
|-------|------|--------|------|
| `actor.colChkInfo.health` | u8 | +0x1BC | Health |
| `work[BFD_ACTION_STATE]` | s16 | +0x0234+0 | State machine |
| `work[BFD_INVINC_TIMER]` | s16 | +0x0234+28 | Invuln timer |

#### Modifiable Fields (FD2 - Hole/Ground)
| Field | Type | Offset | Note |
|-------|------|--------|------|
| `actor.colChkInfo.health` | u8 | +0x1BC | Health |
| `work[FD2_ACTION_STATE]` | s16 | +0x0196+2 | State machine |
| `work[FD2_INVINC_TIMER]` | s16 | +0x0196+20 | Invuln timer |
| `work[FD2_HOLE_COUNTER]` | s16 | +0x0196+18 | Hole count |

#### State Values
```c
enum BossFdActionState {
    BOSSFD_WAIT_INTRO = -1,
    BOSSFD_FLY_MAIN = 0,
    BOSSFD_FLY_HOLE = 1,
    BOSSFD_BURROW = 2,
    BOSSFD_EMERGE = 3,
    BOSSFD_FLY_CEILING = 50,
    BOSSFD_DROP_ROCKS = 51,
    BOSSFD_FLY_CHASE = 100,
    BOSSFD_DEATH_START = 200,
    // ...
};
```

#### Arena Collapse Tracking
```c
// Check if arena collapsed
for (Actor* bg = play->actorCtx.actorLists[ACTORCAT_BG].head; bg; bg = bg->next) {
    if (bg->id == ACTOR_BG_VB_SIMA && bg->actor.colChkInfo.health == 0) {
        // Arena has collapsed
    }
}
```

#### DO NOT Modify
- `actionFunc`
- Mane position arrays
- Skeleton animation

---

### 5. ACTOR_BOSS_TW (Twinrova)
**Structure**: [z_boss_tw.h](z_boss_tw.h) - Size 0x0564+

#### Modifiable Fields
| Field | Type | Offset | Note |
|-------|------|--------|------|
| `actor.colChkInfo.health` | u8 | +0x1BC | Health |
| `work[INVINC_TIMER]` | s16 | +0x0150+10 | Invuln timer |
| `work[YAW_TGT]` | s16 | +0x0150+20 | Yaw target |
| `twinrovaStun` | u8 | +0x04F4 | Stun state flag |
| `eyeTexIdx` | s16 | +0x04CC | Eye texture (visual) |

#### Form System
```c
// Form stored in actor.params
// 0 = Koume (fire)
// 1 = Kotake (ice)  
// 2 = Twinrova (merged)

// Form affects vulnerability:
// - Koume: Ice attacks heal, Fire damages
// - Kotake: Fire attacks heal, Ice damages
// - Twinrova: Both attacks damage
```

#### Stun Window
```c
// When twinrovaStun > 0, boss is vulnerable to beam reflect
twinrova->twinrovaStun = 0;  // No longer stunned
twinrova->twinrovaStun = 1;  // Stunned
```

#### DO NOT Modify
- `actionFunc`
- Actor position/rotation
- Animation system

---

### 6. ACTOR_BOSS_GANON2 (Ganon Beast)
**Structure**: [z_boss_ganon2.h](z_boss_ganon2.h) - Size 0x08E4

#### CUSTOM Health System
**IMPORTANT**: Ganon2 does NOT use `actor.colChkInfo.health`!

| Field | Type | Offset | Note |
|-------|------|--------|------|
| **Health (Custom)** | f32 | **+0x324** | **Normalized 0.0-1.0** |
| `unk_316` | s16 | +0x0316 | "no_hit_time" invuln timer |
| `unk_19C` | s16 | +0x019C | Phase state |
| `unk_1A2[0]` | s16 | +0x01A2 | State timer |

#### Health Modification Pattern
```c
// SPECIAL: Ganon2 uses custom normalized health float
const char* actorPtr = (const char*)ganon2_actor;
float* healthPtr = (float*)(actorPtr + 0x324);

// Read current health
float currentHealth = *healthPtr;

// Modify health
*healthPtr = 0.75f;  // 75% health
*healthPtr = 0.0f;   // Death

// Phase detection (from adapter)
if (healthNorm <= 0.0f) phaseId = 3;      // Dead
else if (healthNorm <= 0.33f) phaseId = 2;  // Low
else if (healthNorm <= 0.66f) phaseId = 1;  // Mid
else phaseId = 0;                           // High
```

#### DO NOT Modify
- `actionFunc`
- Position/rotation
- Animation

---

### 7. ACTOR_EN_BIGOKUTA (BigOcto Miniboss)
**Structure**: [z_en_bigokuta.h](z_en_bigokuta.h) - Size 0x0384

#### Modifiable Fields
| Field | Type | Offset | Note |
|-------|------|--------|------|
| `actor.colChkInfo.health` | u8 | +0x1BC | Health |
| `unk_196` | s16 | +0x0196 | Main timer |
| `unk_198` | s16 | +0x0198 | Variant timer |
| `unk_19A` | s16 | +0x019A | Sub-timer |

#### Two-Phase System
```c
// BigOcto has 2 attack phases
if (hp > 50%) {
    // Phase 1: Initial attacks
    phaseId = 0;
} else {
    // Phase 2: Weakened, fewer attacks
    phaseId = 1;
}

// Death
if (hp == 0) phaseId = 2;
```

#### DO NOT Modify
- `actionFunc`
- Skeleton animation
- Position/rotation

---

## Implementation Guide for ApplyEvent()

### Template Function
```cpp
void ApplyEvent(PlayState* play, const nlohmann::json& payload) override {
    // 1. Find actor
    Actor* actor = FindActorInScene(play, payload);
    if (!actor) return;
    
    // 2. Extract event data
    std::string eventType = payload["eventType"];
    
    // 3. Apply changes based on event type
    if (eventType == "BOSS_WEAKPOINT_HIT") {
        uint8_t damage = payload["damage"];
        uint8_t currentHp = actor->colChkInfo.health;
        actor->colChkInfo.health = (currentHp > damage) ? (currentHp - damage) : 0;
        
    } else if (eventType == "BOSS_SUBACTOR_KILL") {
        // Find and kill subactor
        std::string targetKey = payload["targetActorKey"];
        KillSubactor(play, targetKey);
        
    } else if (eventType == "BOSS_STAGE_ENTER") {
        int phaseId = payload["phaseId"];
        // Adjust timers based on phase
        if (HasWorkArray(actor)) {
            boss->work[STATE_INDEX] = DeriveStateFromPhase(phaseId);
        }
    }
    
    // 4. Handle special cases
    if (actor->id == ACTOR_BOSS_GANON2) {
        // Custom health field
        float* healthPtr = (float*)((char*)actor + 0x324);
        *healthPtr = payload["healthNormalized"];
    }
}
```

### Critical Checks
```cpp
// Before modifying any field, verify:
if (actor->id != EXPECTED_BOSS_ID) return;  // Type check
if (actor->flags & ACTOR_FLAG_ALIVE == 0) return;  // Ensure alive
if (play->sceneNum != EXPECTED_SCENE) return;  // Scene check

// For subactors: verify params match
if (actor->params != EXPECTED_PARAM) return;  // Part type check
```

---

## Safe vs Unsafe Matrix

### Safe to Modify ✓
- ✓ `actor.colChkInfo.health` (u8)
- ✓ `work[*_TIMER]` indices in work[] array
- ✓ `work[*_ACTION_STATE]` for state values
- ✓ `invincibilityTimer` or similar timer fields
- ✓ Visual flags (`burst`, `onCeiling`, `ready`, `drawActor`)
- ✓ Stun states (`twinrovaStun`)
- ✓ Eye texture indices (visual only)
- ✓ Custom health fields (Ganon2 at +0x324)

### Unsafe to Modify ✗
- ✗ `actionFunc` (function pointer)
- ✗ `actor.update` (function pointer)
- ✗ `actor.draw` (function pointer)
- ✗ `actor.world.pos` (position vector)
- ✗ `actor.world.rot` (rotation)
- ✗ `actor.shape.rot` (shape rotation)
- ✗ `skelAnime` structure
- ✗ Animation indices/playback directly
- ✗ Collider structures
- ✗ Actor linked list pointers
- ✗ `actor.flags` with ALIVE bit

---

## Testing Checklist

### Before Deployment
- [ ] Health modification works for each boss
- [ ] Subactor kill events destroy correct parts
- [ ] State machine timers advance correctly
- [ ] Invulnerability windows respected
- [ ] Stun states prevent damage appropriately
- [ ] No actor lifecycle breakage
- [ ] Late-join clients receive correct health/phase state
- [ ] Desync recovery doesn't create visual glitches

### Per-Boss Verification
- [ ] Barinade: Support removal, Zapper kill working
- [ ] Morpha: Tentacle health sync, core phase transitions
- [ ] Bongo-Bongo: Hand destruction, eye window timing
- [ ] Volvagia: Hole state transitions, arena collapse tracking
- [ ] Twinrova: Form tracking, stun window accuracy
- [ ] Ganon2: Normalized health modification, phase transitions
- [ ] BigOcto: Two-phase timing correct

---

## References

- Header files: `soh/src/overlays/actors/ovl_Boss_*/z_boss_*.h`
- Existing adapters: `soh/soh/Network/Anchor/BossSync/*.cpp`
- Actor base structure: `libultraship/libultra.h` (Actor definition)
- Collision system: `actor.colChkInfo` fields

---

## Next Steps

1. Implement ApplyEvent() for each adapter using this research
2. Create comprehensive test cases for each boss
3. Validate with multiplayer testing
4. Roll out to live with feature flags
5. Monitor for edge cases and desync reports
