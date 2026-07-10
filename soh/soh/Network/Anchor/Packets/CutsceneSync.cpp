#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "z64cutscene.h"
extern PlayState* gPlayState;
}

/**
 * TRIGGER_CUTSCENE
 *
 * Sent by whichever client first transitions from CS_STATE_IDLE to a running
 * cutscene state (SKIPPABLE_INIT or UNSKIPPABLE_INIT).
 *
 * Conditions checked before sending (all must be true):
 *   1. roomState.syncCutscenes == 1
 *   2. roomState.syncEnemies   == 1  (shares radius / room infrastructure)
 *   3. At least one connected client is in the same scene + room
 *   4. That client is within the Enemy Sync Radius of the local player
 *
 * On receive: if the local engine is still in CS_STATE_IDLE and the scene
 * numbers match, trigger the matching cutscene state.
 */

void Anchor::SendPacket_TriggerCutscene(u8 csState) {
    if (!IsSaveLoaded() || !gPlayState) return;

    nlohmann::json payload;
    payload["type"]     = TRIGGER_CUTSCENE;
    payload["csState"]  = csState;
    payload["sceneNum"] = (s16)gPlayState->sceneNum;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_TriggerCutscene(nlohmann::json payload) {
    if (!IsSaveLoaded() || !gPlayState) return;
    if (!roomState.syncCutscenes || !roomState.syncEnemies) return;

    // Sanity: sender must be in the same scene.
    s16 senderScene = payload.value("sceneNum", (s16)-1);
    if (senderScene != (s16)gPlayState->sceneNum) return;

    u8 csState = payload.value("csState", (u8)CS_STATE_IDLE);

    if (csState == CS_STATE_IDLE) {
        // Master's cutscene ended — release any client that is still stuck in a
        // non-IDLE CS state (e.g. a locally-triggered CS whose ending actor is
        // frozen by enemy sync, or any leftover state from a previous session).
        // func_8006450C cleanly resets csCtx (state → IDLE, unk_0C → 0).
        if (gPlayState->csCtx.state != CS_STATE_IDLE) {
            func_8006450C(gPlayState, &gPlayState->csCtx);
        }
    }
    // INTENTIONALLY no func_80064520 / func_80064534 calls for non-IDLE states.
    //
    // Root cause of the freeze+letterbox bug:
    //   Calling func_80064520/func_80064534 sets csCtx.state = SKIPPABLE_INIT but
    //   does NOT set csCtx.scriptList[0].script — that is done by the actor that
    //   normally triggers the cutscene.  Without a valid script pointer the CS
    //   subsystem enters an indeterminate state: letterbox appears, the player
    //   is frozen, but no commands ever execute.  The state never reaches IDLE
    //   on its own, causing a permanent softlock until func_8006450C is called.
    //
    // Correct behaviour:
    //   Actor-driven cutscenes run on ALL clients because the boss/trigger actor
    //   is NOT frozen by ShouldActorUpdate (ACTORCAT_BOSS runs normally on every
    //   client).  The actor sets csCtx.scriptList[0] and calls func_80064520
    //   itself — so the CS plays naturally without network intervention.
    //   Non-master clients that do NOT have the CS triggered locally (different
    //   local state) simply play freely while the master watches the cutscene.
    //   The CS_STATE_IDLE release above ensures they are un-stuck if needed.
}
