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
#include "src/overlays/actors/ovl_En_Horse/z_en_horse.h"
#include "objects/gameplay_keep/gameplay_keep.h"
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
    // Suppress the echo that fires when Item_Give is called at the end of a physical
    // exchange animation — otherwise both players would loop items back and forth.
    if (physicalExchangeGivePending > 0) {
        physicalExchangeGivePending--;
        return;
    }

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

    // Physical Item Exchange: park the item in the queue instead of giving it immediately.
    // Only applies to one-time story items (ITEM_CATEGORY_MAJOR: stones, medallions,
    // weapons, equipment, songs, ...).  Consumables such as Rupees, Deku Nuts, arrows,
    // hearts, and small/boss keys are given instantly as usual.
    // The OnGameFrameUpdate proximity check will call GiveNextPhysicalExchangeItem() when
    // players are close enough.
    if (roomState.physicalItemExchange && !IsConsumableCountItem(getItemEntry) &&
        getItemEntry.getItemCategory == ITEM_CATEGORY_MAJOR) {
        std::string itemName;
        if (modId == MOD_NONE) {
            itemName = SohUtils::GetItemName(getItemEntry.itemId);
        } else {
            itemName = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(getItemId)).GetName().english;
        }
        // Deduplication: don't queue the same item twice (belt-and-suspenders guard
        // against the feedback loop even if physicalExchangeGivePending is stale).
        bool alreadyQueued = std::any_of(
            physicalItemQueue.begin(), physicalItemQueue.end(),
            [&](const PendingExchangeItem& q) {
                return q.modId == modId && q.getItemId == getItemId;
            });
        if (!alreadyQueued) {
            physicalItemQueue.push_back({modId, getItemId, client.name, itemName});
        }
        return;
    }

    if (getItemEntry.modIndex == MOD_NONE) {
        if (getItemEntry.getItemId == GI_SWORD_BGS) {
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

/**
 * Physical Item Exchange: pop the next queued item from physicalItemQueue and
 * give it to the local player using GiveItemEntryWithoutActor so the proper
 * "hold item above head + fanfare" animation plays.  The item's native textId
 * is used for the in-game textbox; a Notification overlay shows who sent it.
 * Must only be called when the player is in a state to receive items (checked
 * by caller).
 */
void Anchor::GiveNextPhysicalExchangeItem() {
    if (physicalItemQueue.empty() || !IsSaveLoaded() || !gPlayState) return;

    // Don't pop a new item while the Epona exchange animation is in progress.
    if (eponaExchange.phase != EponaExchangePhase::IDLE) return;

    Player* player = GET_PLAYER(gPlayState);
    if (player->stateFlags1 & (PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_ITEM_CS |
                                PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD)) {
        return;
    }

    PendingExchangeItem item = physicalItemQueue.front();
    physicalItemQueue.pop_front();
    physicalExchangeGivePending++; // will be consumed by SendPacket_GiveItem when Item_Give fires

    // Mode B — player is mounted: route to the Epona proxy-flight exchange.
    if (player->stateFlags1 & PLAYER_STATE1_ON_HORSE) {
        StartEponaExchange(item);
        return;
    }

    GetItemEntry entry;
    if (item.modId == MOD_NONE) {
        entry = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, item.getItemId);
    } else {
        entry = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(item.getItemId)).GetGIEntry_Copy();
    }

    if (!GiveItemEntryWithoutActor(gPlayState, entry)) {
        // Player wasn't ready (e.g., in midair) — put the item back.
        physicalExchangeGivePending--; // give didn't happen, undo the counter
        physicalItemQueue.push_front(item);
        return;
    }

    // Show who sent the item as a notification overlay.
    // The vanilla item textbox (entry.textId) is used for the in-game message.
    Notification::Emit({
        .itemIcon = (item.modId == MOD_NONE) ? GetTextureForItemId(entry.itemId) : nullptr,
        .prefix = item.senderName,
        .message = "hat dir übergeben:",
        .suffix = item.itemName,
    });

    // Play the throw animation on the sender's dummy player.
    for (auto& [cid, client] : clients) {
        if (!client.self && client.online && client.player &&
            client.name == item.senderName) {
            Player* dummy = (Player*)client.player;
            LinkAnimation_PlayOnce(gPlayState, &dummy->skelAnime,
                                   (LinkAnimationHeader*)gPlayerAnim_link_normal_throw);
            client.giverAnimTimer = 30;
            break;
        }
    }
}

/**
 * Epona Mode B: put the horse into the whinneying idle and set up the state
 * machine so the frame hook can fly the proxy item from the sender's position
 * to the local player.  The GetItemEntry is pre-resolved here while
 * ItemTableManager is in scope.
 */
void Anchor::StartEponaExchange(const PendingExchangeItem& item) {
    if (!gPlayState) return;
    Player* player = GET_PLAYER(gPlayState);
    if (!(player->stateFlags1 & PLAYER_STATE1_ON_HORSE) || !player->rideActor) return;

    EnHorse* horse = (EnHorse*)player->rideActor;
    // ENHORSE_ACT_MOUNTED_IDLE_WHINNEYING keeps speedXZ at 0 every frame
    // and plays the neigh sound internally via the horse's own update function.
    horse->action = ENHORSE_ACT_MOUNTED_IDLE_WHINNEYING;

    // Pre-resolve the GetItemEntry so HookHandlers.cpp can populate the proxy's
    // visual without needing its own ItemTableManager / randomizer includes.
    eponaExchange.entry = (item.modId == MOD_NONE)
        ? ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, item.getItemId)
        : Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(item.getItemId)).GetGIEntry_Copy();

    eponaExchange.item       = item;
    eponaExchange.phase      = EponaExchangePhase::WHINNEYING;
    eponaExchange.frameStart = gPlayState->state.frames;
    eponaExchange.proxyActor = nullptr;
}
