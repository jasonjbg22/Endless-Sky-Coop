/* CoOpRelayController.h
Copyright (c) 2026 by Endless Sky contributors

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "CoOpRelay.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Engine;
class PlayerInfo;



// Process-level owner for the opt-in Co-op Relay mode. It keeps networking and
// remote presence outside of normal PlayerInfo and Engine gameplay state.
class CoOpRelayController {
public:
	struct DrawStats {
		int sameSystemContacts = 0;
		int visibleFlightContacts = 0;
		int rosterRows = 0;
		int eventRows = 0;
	};
	struct DiscoveredRelay {
		std::string name;
		std::string host;
		std::string id;
		uint16_t port = 0;
		int ageSeconds = 0;
		int playerCount = -1;
		bool passwordProtected = false;
	};

	static CoOpRelayController &Get();

	CoOpRelayController(const CoOpRelayController &) = delete;
	CoOpRelayController &operator=(const CoOpRelayController &) = delete;
	~CoOpRelayController();

	bool StartHost(uint16_t port = 5050, std::string roomName = {}, std::string password = {},
		bool allowDetached = true);
	void SetHostRoomName(std::string roomName);
	void SetHostRoomPassword(std::string roomName, std::string password);
	void StopHost();
	void HostAndJoin(const PlayerInfo &player, uint16_t port = 5050, std::string password = {});
	void JoinLocal(const PlayerInfo &player, uint16_t port = 5050, std::string password = {});
	void Connect(std::string host, uint16_t port, std::string playerName, std::string password = {});
	void Disconnect();
	bool RequestResync();

	void StepHost();
	void SetSimulationActive(bool active);
	void Step(PlayerInfo &player);
	void DrawFlightOverlay(const PlayerInfo &player, const Engine &engine) const;
	bool ShouldSuppressLocalNPCSpawns(const PlayerInfo &player) const;

	bool IsConnected() const;
	int DirectPeerCount() const;
	bool IsHostRunning() const;
	size_t HostPlayerCount() const;
	std::string PlayerId() const;
	bool PublishPeerEndpoint(std::string host, uint16_t port);
	bool PublishSharedNPC(const CoOpRelay::SharedNPCSnapshot &snapshot);
	bool PublishSharedNPCDamage(const CoOpRelay::SharedNPCDamage &damage);
	bool PublishSharedCombatHit(const CoOpRelay::SharedCombatHit &hit);
	bool PublishSharedWeaponFire(const CoOpRelay::SharedWeaponFire &fire);
	std::vector<CoOpRelay::SharedNPCDamage> TakeNPCDamageReports();
	std::vector<CoOpRelay::SharedCombatHit> TakeCombatHits();
	std::vector<CoOpRelay::SharedWeaponFire> TakeWeaponFires();
	bool PublishSharedNPCBoarding(const CoOpRelay::SharedNPCBoarding &boarding);
	bool PublishSharedNPCBoardingRequest(std::string npcId, std::string detail = "capture");
	std::vector<CoOpRelay::SharedNPCBoarding> TakeNPCBoardingReports();
	bool PublishSharedMissionEvent(const CoOpRelay::SharedMissionEvent &event);
	std::vector<CoOpRelay::SharedMissionEvent> TakeMissionEvents();
	bool PublishSharedResourceEvent(const CoOpRelay::SharedResourceEvent &event);
	std::vector<CoOpRelay::SharedResourceEvent> TakeResourceEvents();
	bool PublishResourceAssist(CoOpRelay::ResourceActionType type, double amount = 1.,
		std::string targetPlayerId = {});
	std::vector<CoOpRelay::SharedResourceEvent> PendingResourceRequests() const;
	bool RespondToResourceRequest(PlayerInfo &player, const std::string &actionId, bool accept);
	std::string StatusText() const;
	std::string HostStatusText() const;
	const CoOpRelay::PresenceStore &Remotes() const;
	const CoOpRelay::AuthorityStore &Authorities() const;
	const CoOpRelay::SharedNPCStore &SharedNPCs() const;
	const CoOpRelay::SharedMissionEventLog &MissionEvents() const;
	const CoOpRelay::SharedResourceEventLog &ResourceEvents() const;
	bool IsSystemAuthority(const std::string &system) const;
	const std::vector<CoOpRelay::PlayerEvent> &RecentEvents() const;
	const std::vector<std::string> &DesyncWarnings() const;
	DrawStats LastDrawStats() const;
	std::vector<DiscoveredRelay> DiscoveredRelays() const;

private:
	CoOpRelayController();

	class LocalRelayHost;
	class DetachedRelayHost;
	class NetworkClient;
	class DiscoveryService;

	std::unique_ptr<LocalRelayHost> host;
	std::unique_ptr<DetachedRelayHost> detachedHost;
	std::unique_ptr<NetworkClient> client;
	std::unique_ptr<DiscoveryService> discovery;
	mutable DrawStats lastDrawStats;
	std::vector<std::string> appliedResourceActions;
	uint64_t nextPeerEndpointSequence = 1;
	uint64_t nextBoardingRequestSequence = 1;
	uint64_t nextResourceAssistSequence = 1;
};
