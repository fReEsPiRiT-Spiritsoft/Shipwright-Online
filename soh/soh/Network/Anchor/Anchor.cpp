#include "Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/nametag.h"
#include "soh/ObjectExtension/ObjectExtension.h"

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

// MARK: - Overrides

void Anchor::Enable() {
    Network::Enable(CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org"),
                    CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383));
    ownClientId = CVarGetInteger(CVAR_REMOTE_ANCHOR("LastClientId"), 0);
    roomState.ownerClientId = 0;
}

void Anchor::Disable() {
    Network::Disable();

    clients.clear();
    roomAuthority.clear();
    RefreshClientActors();
}

void Anchor::OnConnected() {
    SendPacket_Handshake();
    RegisterHooks();

    if (IsSaveLoaded()) {
        SendPacket_RequestTeamState();
    }
}

void Anchor::OnDisconnected() {
    RegisterHooks();
}

void Anchor::ProcessOutgoingPackets() {
    // Copy all queued packets while holding the lock, then send them after releasing
    std::queue<nlohmann::json> packetsToSend;
    {
        std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
        packetsToSend.swap(outgoingPacketQueue);
    }

    // Send packets without holding the lock
    while (!packetsToSend.empty()) {
        nlohmann::json payload = packetsToSend.front();
        packetsToSend.pop();

        if (!payload.contains("quiet")) {
            SPDLOG_DEBUG("[Anchor] Sending payload:\n{}", payload.dump());
        }
        Network::SendJsonToRemote(payload);
    }
}

void Anchor::SendJsonToRemote(nlohmann::json payload) {
    if (!isConnected) {
        return;
    }

    payload["clientId"] = ownClientId;
    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Anchor] Queuing payload:\n{}", payload.dump());
    }

    if (payload["type"] == HANDSHAKE) {
        Network::SendJsonToRemote(payload);
        return;
    }

    // Queue the packet to be sent on the network thread
    std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
    outgoingPacketQueue.push(payload);
}

void Anchor::OnIncomingJson(nlohmann::json payload) {
    // If it doesn't contain a type, it's not a valid payload
    if (!payload.contains("type")) {
        return;
    }

    // If it's not a quiet payload, log it
    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Anchor] Received payload:\n{}", payload.dump());
    }

    std::string packetType = payload["type"].get<std::string>();

    // Ignore packets from mismatched clients, except for ALL_CLIENT_STATE, UPDATE_CLIENT_STATE, and PLAYER_UPDATE
    if (packetType != ALL_CLIENT_STATE && packetType != UPDATE_CLIENT_STATE && packetType != PLAYER_UPDATE) {
        if (payload.contains("clientId")) {
            uint32_t clientId = payload["clientId"].get<uint32_t>();
            if (clients.contains(clientId) && clients[clientId].clientVersion != clientVersion) {
                return;
            }
        }
    }

    // Queue all packets to be processed on the game thread
    std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
    incomingPacketQueue.push(payload);
}

void Anchor::ProcessIncomingPacketQueue() {
    // Copy all queued packets while holding the lock, then process them after releasing
    std::queue<nlohmann::json> packetsToProcess;
    {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        packetsToProcess.swap(incomingPacketQueue);
    }

    // Process packets without holding the lock
    while (!packetsToProcess.empty()) {
        nlohmann::json payload = packetsToProcess.front();
        packetsToProcess.pop();

        std::string packetType = payload["type"].get<std::string>();

        isProcessingIncomingPacket = true;

        try {
            // packetType here is a string so we can't use a switch statement
            if (packetType == ALL_CLIENT_STATE)
                HandlePacket_AllClientState(payload);
            else if (packetType == ACTOR_KILLED)
                HandlePacket_ActorKilled(payload);
            else if (packetType == ITEM_PICKUP)
                HandlePacket_ItemPickup(payload);
            else if (packetType == TIME_SYNC)
                HandlePacket_TimeSync(payload);
            else if (packetType == ACTOR_STATE_UPDATE)
                HandlePacket_ActorStateUpdate(payload);
            else if (packetType == ENEMY_POSITION_UPDATE)
                HandlePacket_EnemyPositionUpdate(payload);
            else if (packetType == BOULDER_SPAWN)
                HandlePacket_BoulderSpawn(payload);
            else if (packetType == ENEMY_DROP_ITEM)
                HandlePacket_EnemyDropItem(payload);
            else if (packetType == ROOM_KILL_SYNC)
                HandlePacket_RoomKillSync(payload);
            else if (packetType == BG_KEYFRAME_SYNC)
                HandlePacket_BgKeyframeSync(payload);
            else if (packetType == TRIGGER_CUTSCENE)
                HandlePacket_TriggerCutscene(payload);
            else if (packetType == PLAYER_ATTACK_ACTOR)
                HandlePacket_PlayerAttackActor(payload);
            else if (packetType == DAMAGE_PLAYER)
                HandlePacket_DamagePlayer(payload);
            else if (packetType == BATTLE_ROYALE_EVENT)
                HandlePacket_BattleRoyaleEvent(payload);
            else if (packetType == DISABLE_ANCHOR)
                HandlePacket_DisableAnchor(payload);
            else if (packetType == ENTRANCE_DISCOVERED)
                HandlePacket_EntranceDiscovered(payload);
            else if (packetType == GAME_COMPLETE)
                HandlePacket_GameComplete(payload);
            else if (packetType == GIVE_ITEM)
                HandlePacket_GiveItem(payload);
            else if (packetType == OCARINA_SFX)
                HandlePacket_OcarinaSfx(payload);
            else if (packetType == PLAYER_UPDATE)
                HandlePacket_PlayerUpdate(payload);
            else if (packetType == PLAYER_SFX)
                HandlePacket_PlayerSfx(payload);
            else if (packetType == UPDATE_TEAM_STATE)
                HandlePacket_UpdateTeamState(payload);
            else if (packetType == REQUEST_TEAM_STATE)
                HandlePacket_RequestTeamState(payload);
            else if (packetType == REQUEST_TELEPORT)
                HandlePacket_RequestTeleport(payload);
            else if (packetType == SERVER_MESSAGE)
                HandlePacket_ServerMessage(payload);
            else if (packetType == SET_CHECK_STATUS)
                HandlePacket_SetCheckStatus(payload);
            else if (packetType == SET_FLAG)
                HandlePacket_SetFlag(payload);
            else if (packetType == TELEPORT_TO)
                HandlePacket_TeleportTo(payload);
            else if (packetType == UNSET_FLAG)
                HandlePacket_UnsetFlag(payload);
            else if (packetType == UPDATE_BEANS_COUNT)
                HandlePacket_UpdateBeansCount(payload);
            else if (packetType == UPDATE_CLIENT_STATE)
                HandlePacket_UpdateClientState(payload);
            else if (packetType == UPDATE_ROOM_STATE)
                HandlePacket_UpdateRoomState(payload);
            else if (packetType == UPDATE_DUNGEON_ITEMS)
                HandlePacket_UpdateDungeonItems(payload);
            else if (packetType == ROOM_JOIN)
                HandlePacket_RoomJoin(payload);
            else if (packetType == ROOM_MASTER_ASSIGN)
                HandlePacket_RoomMasterAssign(payload);
            else if (packetType == ROOM_SNAPSHOT)
                HandlePacket_RoomSnapshot(payload);
            else if (packetType == ROOM_EVENT)
                HandlePacket_RoomEvent(payload);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[Anchor] Exception while processing incoming packet {}", e.what());
            SPDLOG_ERROR("[Anchor] Packet: {}", payload.dump());
        }

        isProcessingIncomingPacket = false;
    }
}

// MARK: - Misc/Helpers

// Kills all existing anchor actors and respawns them with the new client data

struct DummyPlayerClientId {
    uint32_t clientId = 0;
};
static ObjectExtension::Register<DummyPlayerClientId> DummyPlayerClientIdRegister;

uint32_t Anchor::GetDummyPlayerClientId(const Actor* actor) {
    const DummyPlayerClientId* clientId = ObjectExtension::GetInstance().Get<DummyPlayerClientId>(actor);
    return clientId != nullptr ? clientId->clientId : 0;
}

void Anchor::SetDummyPlayerClientId(const Actor* actor, uint32_t clientId) {
    ObjectExtension::GetInstance().Set<DummyPlayerClientId>(actor, DummyPlayerClientId{ clientId });
}

// MARK: - Room Authority Helpers

/**
 * Builds a stable room key from scene and room numbers.
 * Format: "{sceneNum}_{roomNum}". Deterministic across all clients because
 * every client loads the same scene/room layout from the ROM.
 */
std::string Anchor::BuildRoomKey(s16 sceneNum, s8 roomNum) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d_%d", (int)sceneNum, (int)roomNum);
    return std::string(buf);
}

/**
 * Returns the room key for the currently loaded scene + room,
 * or an empty string if no game state is available.
 */
std::string Anchor::GetCurrentRoomKey() {
    if (!gPlayState) return "";
    return BuildRoomKey(gPlayState->sceneNum, gPlayState->roomCtx.curRoom.num);
}

/**
 * True if this client is the global room admin (can change settings and
 * assign room masters). This is the original single-authority check.
 */
bool Anchor::IsHostAuthority() {
    return isConnected && ownClientId != 0 && ownClientId == roomState.ownerClientId;
}

/**
 * True if this client is the gameplay authority for the current room.
 *
 * Checks the roomAuthority map first. If no explicit assignment exists for
 * the current room key, falls back to IsHostAuthority() so rooms without the
 * new handshake behave exactly as before (backwards compatible).
 */
bool Anchor::IsRoomMaster() {
    if (!isConnected || ownClientId == 0) return false;
    std::string key = GetCurrentRoomKey();
    if (key.empty()) return false;

    auto it = roomAuthority.find(key);
    if (it != roomAuthority.end() && it->second != 0) {
        return it->second == ownClientId;
    }
    // No explicit assignment yet — fall back to host authority.
    return IsHostAuthority();
}

/**
 * True if the given actor lies within the configured sync radius around the
 * local player. When syncRadius == 0 the radius is unlimited and every actor
 * is considered in-range. Safe to call even when gPlayState is null (returns
 * true = "no filter applied").
 */
bool Anchor::IsActorInsideSyncRadius(const Actor* actor) {
    if (roomState.syncRadius == 0 || !gPlayState) return true;
    Player* localLink = GET_PLAYER(gPlayState);
    if (!localLink) return true;
    const f32 rSq = (f32)roomState.syncRadius * (f32)roomState.syncRadius;
    // Cast away const: Math3D_Vec3fDistSq takes non-const Vec3f* but does not modify.
    return Math3D_Vec3fDistSq(const_cast<Vec3f*>(&actor->world.pos), &localLink->actor.world.pos) <= rSq;
}

/**
 * Single source-of-truth for the "Marionetten-Entscheidung":
 * should this actor's AI be suppressed locally and driven by network data?
 *
 * Returns true when ALL of the following hold:
 *  - a save is loaded and enemy sync is enabled
 *  - we are NOT the room master (i.e. we are a non-authority client)
 *  - the room master is present in our current scene+room
 *  - the actor lies within the configured sync radius
 *
 * All call sites (ShouldActorUpdate gates, send-path guards, …) should go
 * through this function instead of reimplementing the guards inline.
 */
bool Anchor::ShouldActorBeNetworkDriven(const Actor* actor) {
    if (!IsSaveLoaded() || !roomState.syncEnemies) return false;
    if (IsEnemyAuthority())   return false; // We ARE the authority — run AI normally.
    if (!IsOwnerInSameRoom()) return false; // No authority present — run AI locally.
    return IsActorInsideSyncRadius(actor);  // Within sync range: suppress local AI.
}

void Anchor::RefreshClientActors() {
    if (!IsSaveLoaded()) {
        return;
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;

    while (actor != NULL) {
        if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
            NameTag_RemoveAllForActor(actor);
            Actor_Kill(actor);
        }
        actor = actor->next;
    }

    for (auto& [clientId, client] : clients) {
        if (!client.online || client.self) {
            continue;
        }

        spawningDummyPlayerForClientId = clientId;
        // We are using a hook `ShouldActorInit` to override the init/update/draw/destroy functions of the Player we
        // spawn We quickly store a mapping of "index" to clientId, then within the init function we use this to get the
        // clientId and store it on player->zTargetActiveTimer (unused s32 for the dummy) for convenience
        auto dummy =
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, client.posRot.pos.x, client.posRot.pos.y,
                        client.posRot.pos.z, client.posRot.rot.x, client.posRot.rot.y, client.posRot.rot.z, 0);
        client.player = (Player*)dummy;
    }
    spawningDummyPlayerForClientId = 0;
}

bool Anchor::IsSaveLoaded() {
    if (gPlayState == nullptr) {
        return false;
    }

    if (GET_PLAYER(gPlayState) == nullptr) {
        return false;
    }

    if (gSaveContext.fileNum < 0 || gSaveContext.fileNum > 2) {
        return false;
    }

    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }

    return true;
}
