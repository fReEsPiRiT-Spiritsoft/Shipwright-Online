#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "soh/Enhancements/item-tables/ItemTableManager.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * GIVE_ITEM
 */

uint8_t incomingIceTrapsFromAnchor = 0;

/**
 * Returns true if the item is a consumable (HP refill, ammo refill, rupees, etc.)
 * that should NOT be synced to other players when syncHPAndCounts is disabled.
 *
 * Items with category ITEM_CATEGORY_MAJOR (story items, equipment unlocks, songs, medals...)
 * are always synced regardless of this setting.
 *
 * Rules:
 *   ITEM_CATEGORY_JUNK    → per-player (junk doesn't matter)
 *   ITEM_CATEGORY_LESSER  → per-player (ammo refills, rupees, etc.)
 *   ITEM_CATEGORY_HEALTH  → per-player (hearts, heart pieces, heart containers)
 *   ITEM_CATEGORY_MAJOR   → always sync
 *   ITEM_CATEGORY_BOSS_KEY / ITEM_CATEGORY_SMALL_KEY / ITEM_CATEGORY_SKULLTULA_TOKEN → always sync
 */
static bool IsConsumableCountItem(const GetItemEntry& entry) {
    switch (entry.getItemCategory) {
        case ITEM_CATEGORY_JUNK:
        case ITEM_CATEGORY_LESSER:
        case ITEM_CATEGORY_HEALTH:
            return true;
        default:
            return false;
    }
}

void Anchor::SendPacket_GiveItem(u16 modId, s16 getItemId) {
    if (!IsSaveLoaded() || isProcessingIncomingPacket || !roomState.syncItemsAndFlags) {
        return;
    }

    if (modId == MOD_RANDOMIZER && getItemId == RG_ICE_TRAP && incomingIceTrapsFromAnchor > 0) {
        incomingIceTrapsFromAnchor = MAX(incomingIceTrapsFromAnchor - 1, 0);
        return;
    }

    // Ignore sending master sword in final Ganon fight
    if (modId == MOD_RANDOMIZER && getItemId == RG_MASTER_SWORD && gPlayState->sceneNum == SCENE_GANON_BOSS) {
        return;
    }

    // When HP & Item Count sync is disabled, only send major (unlock/story) items.
    // Consumables (ammo refills, hearts, rupees) stay per-player.
    if (!roomState.syncHPAndCounts) {
        GetItemEntry entry;
        if (modId == MOD_NONE) {
            entry = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, getItemId);
        } else {
            entry = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(getItemId)).GetGIEntry_Copy();
        }
        if (IsConsumableCountItem(entry)) {
            return;
        }
    }

    nlohmann::json payload;
    payload["type"] = GIVE_ITEM;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["addToQueue"] = true;
    payload["modId"] = modId;
    payload["getItemId"] = getItemId;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_GiveItem(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    uint32_t clientId = payload.at("clientId").get<uint32_t>();
    AnchorClient& client = clients[clientId];
    u16 modId = payload.at("modId").get<u16>();
    u16 getItemId = payload.at("getItemId").get<u16>();

    GetItemEntry getItemEntry;
    if (modId == MOD_NONE) {
        getItemEntry = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, getItemId);
    } else {
        getItemEntry = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(getItemId)).GetGIEntry_Copy();
    }

    // When HP & Item Count sync is disabled, don't accept consumable items from other players.
    // This packet shouldn't normally arrive (sender already filters), but guard here for safety.
    if (!roomState.syncHPAndCounts && IsConsumableCountItem(getItemEntry)) {
        return;
    }

    if (getItemEntry.modIndex == MOD_NONE) {
        if (getItemEntry.getItemId == GI_SWORD_BGS) {
            gSaveContext.bgsFlag = true;
        }
        Item_Give(gPlayState, getItemEntry.itemId);
    } else if (getItemEntry.modIndex == MOD_RANDOMIZER) {
        if (getItemEntry.getItemId == RG_ICE_TRAP) {
            gSaveContext.ship.pendingIceTrapCount++;
            incomingIceTrapsFromAnchor++;
        } else {
            Randomizer_Item_Give(gPlayState, getItemEntry);
        }
    }

    // Full heal if getting a heart container or piece
    if (getItemEntry.gid == GID_HEART_CONTAINER || getItemEntry.gid == GID_HEART_PIECE) {
        gSaveContext.healthAccumulator = 0x140;
    }

    // Handle if the player gets a 4th heart piece (usually handled in z_message)
    s32 heartPieces = (s32)(gSaveContext.inventory.questItems & 0xF0000000) >> (QUEST_HEART_PIECE + 4);
    if (heartPieces >= 4) {
        gSaveContext.inventory.questItems &= ~0xF0000000;
        gSaveContext.inventory.questItems += (heartPieces % 4) << (QUEST_HEART_PIECE + 4);
        gSaveContext.healthCapacity += 0x10 * (heartPieces / 4);
        gSaveContext.health += 0x10 * (heartPieces / 4);
    }

    if (getItemEntry.getItemCategory != ITEM_CATEGORY_JUNK) {
        if (getItemEntry.modIndex == MOD_NONE) {
            Notification::Emit({
                .itemIcon = GetTextureForItemId(getItemEntry.itemId),
                .prefix = client.name,
                .message = "found",
                .suffix = SohUtils::GetItemName(getItemEntry.itemId),
            });
        } else if (getItemEntry.modIndex == MOD_RANDOMIZER) {
            Notification::Emit({
                .prefix = client.name,
                .message = "found",
                .suffix = Rando::StaticData::RetrieveItem((RandomizerGet)getItemEntry.getItemId).GetName().english,
            });
        }
    }
}
