#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"

extern PlayState* gPlayState;
}

// When the Big Octo "kidnapped Ruto" flag (INFTABLE_146) arrives on a client
// that is currently inside Jabu-Jabu, the platform actor's Init has already
// run without the flag being set, so the Big Octo was never spawned as the
// platform's child.  Detect this and spawn it now so all clients see the fight.
static void Anchor_SpawnJabuBigOcto() {
    if (!gPlayState || gPlayState->sceneNum != SCENE_JABU_JABU) {
        return;
    }
    if (Anchor::Instance->IsEnemyAuthority()) {
        return; // master spawns it naturally through its own state machine
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head;
    while (actor != nullptr) {
        // params==0 identifies the OctoPlatform variant of BG_BDAN_OBJECTS
        if (actor->id == ACTOR_BG_BDAN_OBJECTS && actor->params == 0) {
            if (actor->child == nullptr && !Flags_GetClear(gPlayState, actor->room)) {
                Actor_SpawnAsChild(&gPlayState->actorCtx, actor, gPlayState,
                                   ACTOR_EN_BIGOKUTA,
                                   actor->home.pos.x, actor->home.pos.y, actor->home.pos.z,
                                   0, (s16)(actor->shape.rot.y + 0x8000), 0, 3);
            }
            break;
        }
        actor = actor->next;
    }
}

static void Anchor_ApplyKingZoraMovedFallback() {
    if (!gPlayState || gPlayState->sceneNum != SCENE_ZORAS_DOMAIN) {
        return;
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;
    while (actor != nullptr) {
        if (actor->id == ACTOR_EN_KZ) {
            // En_Kz encodes path index in params high byte and moves to the
            // last point when EVENTCHKINF_KING_ZORA_MOVED is set.
            if ((actor->params & 0xFF00) == 0xFF00) {
                return;
            }

            Path* path = &gPlayState->setupPathList[(actor->params & 0xFF00) >> 8];
            Vec3s* points = SEGMENTED_TO_VIRTUAL(path->points);
            Vec3s* lastPoint = points + (path->count - 1);

            actor->world.pos.x = lastPoint->x;
            actor->world.pos.y = lastPoint->y;
            actor->world.pos.z = lastPoint->z;
            actor->home.pos = actor->world.pos;
            return;
        }
        actor = actor->next;
    }
}

/**
 * SET_FLAG
 *
 * Fired when a flag is set in the save context
 */

void Anchor::SendPacket_SetFlag(s16 sceneNum, s16 flagType, s16 flag) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = SET_FLAG;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["addToQueue"] = true;
    payload["sceneNum"] = sceneNum;
    payload["flagType"] = flagType;
    payload["flag"] = flag;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_SetFlag(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    s16 sceneNum = payload.at("sceneNum").get<s16>();
    s16 flagType = payload.at("flagType").get<s16>();
    s16 flag = payload.at("flag").get<s16>();

    // Physical Item Exchange: buffer the flag until players are close enough.
    if (roomState.physicalItemExchange) {
        physicalFlagQueue.push_back({sceneNum, flagType, flag});
        return;
    }

    if (sceneNum == SCENE_ID_MAX) {
        auto effect = new GameInteractionEffect::SetFlag();
        effect->parameters[0] = flagType;
        effect->parameters[1] = flag;
        effect->Apply();

        // Special case: If King Zora moved, and the player has Ruto's Letter, convert it to an empty bottle
        if (flagType == FLAG_EVENT_CHECK_INF && flag == EVENTCHKINF_KING_ZORA_MOVED &&
            Inventory_HasSpecificBottle(ITEM_LETTER_RUTO)) {
            Inventory_ReplaceItem(gPlayState, ITEM_LETTER_RUTO, ITEM_BOTTLE);
        }

        if (flagType == FLAG_EVENT_CHECK_INF && flag == EVENTCHKINF_KING_ZORA_MOVED) {
            Anchor_ApplyKingZoraMovedFallback();
        }

        // Diving minigame completion can set the event flag without always
        // traveling through a normal item-give callback on remote peers.
        // Ensure the silver scale upgrade is present when this flag arrives.
        if (flagType == FLAG_EVENT_CHECK_INF && flag == EVENTCHKINF_OBTAINED_SILVER_SCALE &&
            CUR_UPG_VALUE(UPG_SCALE) < 1) {
            Inventory_ChangeUpgrade(UPG_SCALE, 1);
        }

        // When Ruto is kidnapped by Big Octo (INFTABLE_146), spawn the Big Octo
        // on any non-authority client that is currently inside Jabu-Jabu.
        if (flagType == FLAG_INF_TABLE && flag == INFTABLE_146) {
            Anchor_SpawnJabuBigOcto();
        }
    } else {
        // Special case: Ignore water temple water level flags, stored at 0x1C, 0x1D, 0x1E.
        if (sceneNum == SCENE_WATER_TEMPLE && flagType == FLAG_SCENE_SWITCH &&
            (flag == 0x1C || flag == 0x1D || flag == 0x1E)) {
            return;
        }

        // Special case: Ignore forest temple elevator flag, stored at 0x1B.
        if (sceneNum == SCENE_FOREST_TEMPLE && flagType == FLAG_SCENE_SWITCH && flag == 0x1B) {
            return;
        }

        auto effect = new GameInteractionEffect::SetSceneFlag();
        effect->parameters[0] = sceneNum;
        effect->parameters[1] = flagType;
        effect->parameters[2] = flag;
        effect->Apply();
    }
}

/**
 * Physical Item Exchange: apply all buffered SetFlag entries that were held
 * back while physicalItemExchange was active.  Call this from the proximity
 * trigger in HookHandlers.cpp (OnGameFrameUpdate) when players are close.
 */
void Anchor::FlushPhysicalFlagQueue() {
    while (!physicalFlagQueue.empty()) {
        PendingExchangeFlag f = physicalFlagQueue.front();
        physicalFlagQueue.pop_front();

        if (f.sceneNum == SCENE_ID_MAX) {
            auto effect = new GameInteractionEffect::SetFlag();
            effect->parameters[0] = f.flagType;
            effect->parameters[1] = f.flag;
            effect->Apply();

            // Special case: King Zora / Ruto's Letter
            if (f.flagType == FLAG_EVENT_CHECK_INF && f.flag == EVENTCHKINF_KING_ZORA_MOVED &&
                Inventory_HasSpecificBottle(ITEM_LETTER_RUTO)) {
                Inventory_ReplaceItem(gPlayState, ITEM_LETTER_RUTO, ITEM_BOTTLE);
            }

            if (f.flagType == FLAG_EVENT_CHECK_INF && f.flag == EVENTCHKINF_KING_ZORA_MOVED) {
                Anchor_ApplyKingZoraMovedFallback();
            }

            // Keep diving reward consistent when the completion flag was queued.
            if (f.flagType == FLAG_EVENT_CHECK_INF && f.flag == EVENTCHKINF_OBTAINED_SILVER_SCALE &&
                CUR_UPG_VALUE(UPG_SCALE) < 1) {
                Inventory_ChangeUpgrade(UPG_SCALE, 1);
            }

            // Big Octo kidnap flag queued while physicalItemExchange was active.
            if (f.flagType == FLAG_INF_TABLE && f.flag == INFTABLE_146) {
                Anchor_SpawnJabuBigOcto();
            }
        } else {
            // Skip the same temple-specific flags that HandlePacket_SetFlag ignores.
            if (f.sceneNum == SCENE_WATER_TEMPLE && f.flagType == FLAG_SCENE_SWITCH &&
                (f.flag == 0x1C || f.flag == 0x1D || f.flag == 0x1E)) {
                continue;
            }
            if (f.sceneNum == SCENE_FOREST_TEMPLE && f.flagType == FLAG_SCENE_SWITCH && f.flag == 0x1B) {
                continue;
            }

            auto effect = new GameInteractionEffect::SetSceneFlag();
            effect->parameters[0] = f.sceneNum;
            effect->parameters[1] = f.flagType;
            effect->parameters[2] = f.flag;
            effect->Apply();
        }
    }
}
