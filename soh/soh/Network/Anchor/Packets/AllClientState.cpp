#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"
#include "soh/Notification/Notification.h"

/**
 * ALL_CLIENT_STATE
 *
 * Contains a list of all clients and their CLIENT_STATE currently connected to the server
 *
 * The server itself sends this packet to all clients when a client connects or disconnects
 */

void Anchor::HandlePacket_AllClientState(nlohmann::json payload) {
    std::vector<AnchorClient> newClients = payload["state"].get<std::vector<AnchorClient>>();
    bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));

    // add new clients
    for (auto& client : newClients) {
        if (client.self) {
            ownClientId = client.clientId;
            CVarSetInteger(CVAR_REMOTE_ANCHOR("LastClientId"), ownClientId);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            clients[client.clientId].self = true;
        } else {
            clients[client.clientId].self = false;
            if (clients.contains(client.clientId)) {
                if (clients[client.clientId].online != client.online && !isGlobalRoom) {
                    Notification::Emit({
                        .prefix = client.name,
                        .message = client.online ? "Connected" : "Disconnected",
                    });
                }
            } else if (client.online && !isGlobalRoom) {
                Notification::Emit({
                    .prefix = client.name,
                    .message = "Connected",
                });
            }
        }

        clients[client.clientId].clientId = client.clientId;
        clients[client.clientId].name = client.name;
        clients[client.clientId].color = client.color;
        clients[client.clientId].clientVersion = client.clientVersion;
        clients[client.clientId].teamId = client.teamId;
        clients[client.clientId].online = client.online;
        clients[client.clientId].seed = client.seed;
        clients[client.clientId].isSaveLoaded = client.isSaveLoaded;
        clients[client.clientId].isGameComplete = client.isGameComplete;
        clients[client.clientId].sceneNum = client.sceneNum;
        clients[client.clientId].curRoomNum = client.curRoomNum;
        clients[client.clientId].entranceIndex = client.entranceIndex;
        clients[client.clientId].timeIncrement = client.timeIncrement;
    }

    // remove clients that are no longer in the list
    std::vector<uint32_t> clientsToRemove;
    for (auto& [clientId, client] : clients) {
        if (std::find_if(newClients.begin(), newClients.end(),
                         [clientId](AnchorClient& c) { return c.clientId == clientId; }) == newClients.end()) {
            clientsToRemove.push_back(clientId);
        }
    }
    // (separate loop to avoid iterator invalidation)
    for (auto& clientId : clientsToRemove) {
        clients.erase(clientId);
    }

    // Host migration: check if current owner is still online and elect a new
    // one if not.  Called here because ALL_CLIENT_STATE is the primary signal
    // that a client connected or disconnected.
    ElectNewHostIfNeeded();

    // Room-Master-Nachfolge: Wenn der Host bemerkt, dass ein Raum-Master offline
    // gegangen ist, wird automatisch ein Ersatz-Client bestimmt und per
    // ROOM_MASTER_ASSIGN an alle Clients gesendet.  Dadurch bleibt Gegner- und
    // BG-Sync auch dann aktiv, wenn der bisherige Room-Master die Szene verlässt
    // oder die Verbindung verliert.
    if (IsHostAuthority()) {
        // Aenderungen sammeln und erst danach auf roomAuthority anwenden,
        // um Iterator-Invalidierung waehrend der Iteration zu vermeiden.
        struct RoomReassignment { std::string roomKey; uint32_t newMaster; };
        std::vector<RoomReassignment> reassignments;

        for (const auto& [roomKey, masterClientId] : roomAuthority) {
            // Prüfen ob der aktuelle Master noch im Raum ist (nicht nur online).
            // Ein Master der den Raum verlassen hat gilt als abwesend und löst
            // eine Neuzuweisung an einen verbleibenden Client im selben Raum aus.
            bool masterInRoom = false;
            if (masterClientId == ownClientId) {
                // Host selbst ist Master — prüfe ob Host noch im Raum ist.
                masterInRoom = IsSaveLoaded() && gPlayState && (GetCurrentRoomKey() == roomKey);
            } else {
                auto mit = clients.find(masterClientId);
                if (mit != clients.end() && mit->second.online) {
                    masterInRoom = (BuildRoomKey((s16)mit->second.sceneNum, (s8)mit->second.curRoomNum) == roomKey);
                }
            }
            if (masterInRoom) continue;

            // Ersatz suchen: erster online + saveLoaded Client im selben Raum.
            uint32_t replacement = 0;
            for (const auto& [cid, client] : clients) {
                if (!client.online || !client.isSaveLoaded) continue;
                if (BuildRoomKey((s16)client.sceneNum, (s8)client.curRoomNum) == roomKey) {
                    replacement = cid;
                    break;
                }
            }
            if (replacement == 0) continue;  // Niemand im Raum — keine Neuzuweisung moeglich

            reassignments.push_back({ roomKey, replacement });
        }

        for (const auto& r : reassignments) {
            roomAuthority[r.roomKey] = r.newMaster;
            nlohmann::json assign;
            assign["type"]            = ROOM_MASTER_ASSIGN;
            assign["roomKey"]         = r.roomKey;
            assign["masterClientId"]  = r.newMaster;
            assign["joiningClientId"] = 0u;
            SendJsonToRemote(assign);
            SPDLOG_INFO("[Anchor] Raum-Master-Nachfolge: room={} neuerMaster={}", r.roomKey, r.newMaster);
        }
    }

    shouldRefreshActors = true;
}
