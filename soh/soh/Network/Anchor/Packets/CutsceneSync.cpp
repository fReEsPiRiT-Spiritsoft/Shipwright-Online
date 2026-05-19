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

    // Only apply if we are still idle (don't restart a CS already running).
    if (gPlayState->csCtx.state != CS_STATE_IDLE) return;

    // Sanity: sender must be in the same scene.
    s16 senderScene = payload.value("sceneNum", (s16)-1);
    if (senderScene != (s16)gPlayState->sceneNum) return;

    u8 csState = payload.value("csState", (u8)CS_STATE_IDLE);
    if (csState == CS_STATE_SKIPPABLE_INIT) {
        func_80064520(gPlayState, &gPlayState->csCtx);
    } else if (csState == CS_STATE_UNSKIPPABLE_INIT) {
        func_80064534(gPlayState, &gPlayState->csCtx);
    }
}
