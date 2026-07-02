/* CoOpRelay.h
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

#include "../Angle.h"
#include "../Point.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class PlayerInfo;



// Shared data model for the local-authoritative co-op relay mode. This layer is
// deliberately presence-only: it stores remote player state and events without
// mutating the local PlayerInfo or entering remote players into local gameplay.
class CoOpRelay {
public:
	static constexpr uint32_t PROTOCOL_VERSION = 3;
	static constexpr uint16_t DEFAULT_PORT = 5050;

	enum class EventType {
		LAUNCHED,
		LANDED,
		JUMPED,
		RESOURCES_CHANGED,
		SHIP_CHANGED,
		DISABLED,
		DESTROYED,
	};

	struct PlayerSnapshot {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string playerId;
		std::string name;
		std::string system;
		Point position;
		Point velocity;
		Angle facing;
		std::string landedPlanet;
		std::string shipModel;
		uint64_t loadoutHash = 0;
		double shields = 0.;
		double hull = 0.;
		double fuel = 0.;
		double energy = 0.;
		double heat = 0.;
		bool simulationActive = false;

		bool IsLanded() const noexcept;
		bool IsCompatible() const noexcept;
	};

	struct PlayerEvent {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string playerId;
		EventType type = EventType::RESOURCES_CHANGED;
		std::string system;
		std::string planet;
		std::string detail;

		bool IsCompatible() const noexcept;
	};

	struct SystemAuthority {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string system;
		std::string ownerId;
		uint32_t playerCount = 0;

		bool IsCompatible() const noexcept;
		bool IsActive() const noexcept;
	};

	struct PeerEndpoint {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string playerId;
		std::string host;
		uint16_t port = 0;
		bool removed = false;

		bool IsCompatible() const noexcept;
		bool IsValid() const noexcept;
	};

	struct SharedNPCSnapshot {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string npcId;
		std::string ownerId;
		std::string system;
		Point position;
		Point velocity;
		Angle facing;
		std::string shipModel;
		std::string government;
		std::string targetId;
		double shields = 0.;
		double hull = 0.;
		double fuel = 0.;
		double energy = 0.;
		double heat = 0.;
		bool disabled = false;
		bool destroyed = false;
		bool captured = false;
		bool removed = false;

		bool IsCompatible() const noexcept;
		bool IsValid() const noexcept;
	};

	struct SharedNPCDamage {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string npcId;
		std::string ownerId;
		std::string system;
		std::string reporterId;
		double shieldDamage = 0.;
		double hullDamage = 0.;
		double fuelDamage = 0.;
		double energyDamage = 0.;
		double heatDamage = 0.;
		bool disabled = false;
		bool destroyed = false;

		bool IsCompatible() const noexcept;
		bool IsValid() const noexcept;
	};

	struct SharedCombatHit {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string attackerId;
		std::string targetPlayerId;
		std::string system;
		std::string attackerModel;
		std::string attackerGovernment;
		double shieldDamage = 0.;
		double hullDamage = 0.;
		double fuelDamage = 0.;
		double energyDamage = 0.;
		double heatDamage = 0.;
		bool disabled = false;
		bool destroyed = false;
		std::string weapon;
		Point impactPosition;
		Point hitVelocity;
		double facing = 0.;
		std::string detail;

		bool IsCompatible() const noexcept;
		bool IsValid() const noexcept;
	};

	struct SharedWeaponFire {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string playerId;
		std::string system;
		Point from;
		Point to;
		Point velocity;
		double facing = 0.;
		Point shipPosition;
		Point shipVelocity;
		bool hasShipVelocity = false;
		std::string targetPlayerId;
		std::string targetNPCId;
		std::string weapon;

		bool IsCompatible() const noexcept;
		bool IsValid() const noexcept;
	};

	enum class BoardingAction {
		REQUEST,
		CONFIRMED,
		REJECTED,
		CAPTURED,
	};

	enum class MissionEventType {
		ACCEPTED,
		OBJECTIVE_UPDATED,
		NPC_DISABLED,
		NPC_DESTROYED,
		NPC_BOARDED,
		NPC_CAPTURED,
		COMPLETED,
		FAILED,
		REWARD,
	};

	enum class ResourceActionType {
		REFUEL_ASSIST,
		REPAIR_ASSIST,
		FUEL_TRANSFER,
		ENERGY_TRANSFER,
		CARGO_TRANSFER,
		CREDIT_REWARD,
	};

	enum class ResourceActionStatus {
		REQUEST,
		CONFIRMED,
		REJECTED,
		APPLIED,
	};

	struct SharedNPCBoarding {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string npcId;
		std::string playerId;
		std::string ownerId;
		std::string system;
		BoardingAction action = BoardingAction::REQUEST;
		std::string detail;

		bool IsCompatible() const noexcept;
		bool IsValid() const noexcept;
		bool IsRequest() const noexcept;
	};

	struct SharedMissionEvent {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string missionId;
		std::string instanceId;
		std::string playerId;
		MissionEventType type = MissionEventType::OBJECTIVE_UPDATED;
		std::string system;
		std::string npcId;
		std::string detail;
		int64_t credits = 0;

		bool IsCompatible() const noexcept;
		bool IsValid() const noexcept;
	};

	struct SharedResourceEvent {
		uint32_t protocolVersion = PROTOCOL_VERSION;
		uint64_t sequence = 0;
		std::string actionId;
		std::string playerId;
		std::string targetPlayerId;
		ResourceActionType type = ResourceActionType::FUEL_TRANSFER;
		ResourceActionStatus status = ResourceActionStatus::REQUEST;
		std::string resource;
		double amount = 0.;
		std::string detail;

		bool IsCompatible() const noexcept;
		bool IsValid() const noexcept;
	};

	class AuthorityStore {
	public:
		bool Apply(const SystemAuthority &authority);
		void Clear();

		const SystemAuthority *Get(const std::string &system) const;
		std::vector<SystemAuthority> All() const;
		bool IsOwner(const std::string &system, const std::string &playerId) const;

	private:
		std::vector<SystemAuthority> systems;
	};

	class SharedNPCStore {
	public:
		bool Apply(const SharedNPCSnapshot &snapshot);
		std::vector<SharedNPCSnapshot> ReassignSystem(const std::string &system, const std::string &ownerId);
		std::vector<SharedNPCSnapshot> RemoveOwner(const std::string &ownerId);
		void Clear();

		const SharedNPCSnapshot *Get(const std::string &npcId) const;
		std::vector<SharedNPCSnapshot> All() const;
		std::vector<SharedNPCSnapshot> InSystem(const std::string &system) const;

	private:
		std::vector<SharedNPCSnapshot> npcs;
	};

	class PeerEndpointStore {
	public:
		bool Apply(const PeerEndpoint &endpoint);
		std::optional<PeerEndpoint> Remove(const std::string &playerId);
		void Clear();

		const PeerEndpoint *Get(const std::string &playerId) const;
		std::vector<PeerEndpoint> All() const;

	private:
		std::vector<PeerEndpoint> endpoints;
	};

	class SharedMissionEventLog {
	public:
		bool Apply(const SharedMissionEvent &event);
		void Clear();
		std::vector<SharedMissionEvent> All() const;
		std::vector<SharedMissionEvent> LatestStates() const;

	private:
		std::vector<SharedMissionEvent> events;
	};

	class SharedResourceEventLog {
	public:
		bool Apply(const SharedResourceEvent &event);
		void Clear();
		std::vector<SharedResourceEvent> All() const;

	private:
		std::vector<SharedResourceEvent> events;
	};

	struct RemotePresence {
		PlayerSnapshot previous;
		PlayerSnapshot latest;
		bool hasPrevious = false;

		bool IsInSystem(const std::string &system) const;
		bool IsLanded() const noexcept;
		PlayerSnapshot Interpolate(double fraction) const;
	};

	class PresenceStore {
	public:
		bool Apply(const PlayerSnapshot &snapshot);
		void Remove(const std::string &playerId);
		void Clear();

		const RemotePresence *Get(const std::string &playerId) const;
		std::vector<RemotePresence> All() const;
		std::vector<RemotePresence> InSystem(const std::string &system) const;

	private:
		std::vector<RemotePresence> players;
	};

	class SystemAuthorityStore {
	public:
		std::vector<SystemAuthority> ApplyPresence(const PlayerSnapshot &snapshot);
		std::vector<SystemAuthority> RemovePlayer(const std::string &playerId);
		std::vector<SystemAuthority> SetServerAuthority(const std::string &system, const std::string &ownerId,
			uint32_t playerCount);
		std::vector<SystemAuthority> RemoveServerAuthority(const std::string &system);
		void Clear();

		const SystemAuthority *Get(const std::string &system) const;
		std::vector<SystemAuthority> All() const;

	private:
		struct SimulatorState {
			std::string playerId;
			uint64_t lastActiveStep = 0;
			bool foreground = false;
		};
		struct SystemState {
			SystemAuthority authority;
			std::vector<std::string> players;
			std::vector<SimulatorState> simulators;
		};

		SystemAuthority Touch(SystemState &state);
		bool ExpireInactiveSimulators(SystemState &state);
		bool SelectOwner(SystemState &state);
		std::vector<SystemAuthority> RemoveFromOtherSystems(const std::string &playerId, const std::string &system);
		std::vector<SystemAuthority> RemoveFromSystem(SystemState &state, const std::string &playerId, bool playerDisconnected);

		uint64_t nextSequence = 1;
		uint64_t activityStep = 0;
		std::vector<SystemState> systems;
	};

	class LocalStateEmitter {
	public:
		void SetEnabled(bool enabled) noexcept;
		bool IsEnabled() const noexcept;
		void SetSimulationActive(bool active) noexcept;

		void SetIdentity(std::string playerId);
		const std::string &PlayerId() const noexcept;

		bool Step(const PlayerInfo &player);
		const std::optional<PlayerSnapshot> &LatestSnapshot() const noexcept;
		std::vector<PlayerEvent> TakeEvents();

	private:
		void QueueEvent(EventType type, const PlayerSnapshot &snapshot, std::string detail = {});

		bool enabled = false;
		std::string playerId;
		uint64_t nextSequence = 1;
		std::optional<PlayerSnapshot> previous;
		std::optional<PlayerSnapshot> latest;
		std::vector<PlayerEvent> events;
		bool simulationActive = false;
		bool previousDisabled = false;
		bool previousDestroyed = false;
	};

	struct RelayDelivery {
		std::string recipientId;
		std::optional<PlayerSnapshot> snapshot;
		std::optional<PlayerEvent> event;
		std::optional<SystemAuthority> authority;
		std::optional<SharedNPCSnapshot> npc;
		std::optional<SharedNPCDamage> npcDamage;
		std::optional<SharedNPCBoarding> npcBoarding;
		std::optional<SharedMissionEvent> missionEvent;
		std::optional<SharedResourceEvent> resourceEvent;
		std::optional<PeerEndpoint> peerEndpoint;
		std::optional<SharedCombatHit> combatHit;
		std::optional<SharedWeaponFire> weaponFire;
	};

	enum class ConnectionState {
		DISCONNECTED,
		CONNECTING,
		CONNECTED,
		ERROR,
	};

	struct OutgoingMessage {
		std::string line;
		bool isSnapshot = false;
		bool isEvent = false;
	};

	struct RelayEndpoint {
		std::string host = "127.0.0.1";
		uint16_t port = DEFAULT_PORT;
	};

	struct JoinRequest {
		std::string playerName;
		std::string password;
	};

	struct JoinResult {
		bool accepted = false;
		std::string playerId;
		std::string message;
	};

	class RelayServerCore {
	public:
		void SetRoomPassword(std::string password);
		bool HasRoomPassword() const noexcept;

		std::string Join(std::string name);
		JoinResult TryJoin(const JoinRequest &request);
		std::vector<RelayDelivery> Leave(const std::string &playerId);

		std::vector<PlayerSnapshot> LatestSnapshotsFor(const std::string &playerId) const;
		std::vector<SystemAuthority> LatestAuthoritiesFor(const std::string &playerId) const;
		std::vector<PeerEndpoint> LatestPeerEndpointsFor(const std::string &playerId) const;
		std::vector<SharedNPCSnapshot> LatestNPCsFor(const std::string &playerId) const;
		std::vector<SharedMissionEvent> LatestMissionEventsFor(const std::string &playerId) const;
		std::vector<SharedResourceEvent> LatestResourceEventsFor(const std::string &playerId) const;
		std::vector<RelayDelivery> Receive(const PlayerSnapshot &snapshot);
		std::vector<RelayDelivery> Receive(const PlayerEvent &event) const;
		std::vector<RelayDelivery> Receive(const PeerEndpoint &endpoint);
		std::vector<RelayDelivery> Receive(const SharedNPCSnapshot &snapshot);
		std::vector<RelayDelivery> Receive(const SharedNPCDamage &damage);
		std::vector<RelayDelivery> Receive(const SharedCombatHit &hit);
		std::vector<RelayDelivery> Receive(const SharedWeaponFire &fire);
		std::vector<RelayDelivery> Receive(const SharedNPCBoarding &boarding);
		std::vector<RelayDelivery> Receive(const SharedMissionEvent &event);
		std::vector<RelayDelivery> Receive(const SharedResourceEvent &event);
		void SetServerWorldEnabled(bool enabled) noexcept;
		bool IsServerWorldEnabled() const noexcept;
		std::vector<RelayDelivery> StepServerWorld(unsigned steps = 1);
		const SystemAuthority *AuthorityFor(const std::string &system) const;
		std::vector<SystemAuthority> Authorities() const;
		const SharedNPCStore &SharedNPCs() const noexcept;
		const SharedMissionEventLog &MissionEvents() const noexcept;
		const SharedResourceEventLog &ResourceEvents() const noexcept;

		size_t PlayerCount() const noexcept;
		const PresenceStore &Presence() const noexcept;

	private:
		struct Peer {
			std::string id;
			std::string name;
			uint64_t joinedResourceReplayIndex = 0;
		};
		struct ResourceReplayEntry {
			uint64_t index = 0;
			SharedResourceEvent event;
		};

		bool HasPeer(const std::string &playerId) const;
		const Peer *GetPeer(const std::string &playerId) const;
		std::vector<RelayDelivery> SnapshotDeliveries(const PlayerSnapshot &snapshot) const;
		std::vector<RelayDelivery> EventDeliveries(const PlayerEvent &event) const;
		std::vector<RelayDelivery> AuthorityDeliveries(const SystemAuthority &authority) const;
		std::vector<RelayDelivery> PeerEndpointDeliveries(const PeerEndpoint &endpoint) const;
		std::vector<RelayDelivery> NPCDeliveries(const SharedNPCSnapshot &snapshot, bool includeOwner = false) const;
		std::vector<RelayDelivery> NPCDamageDeliveries(const SharedNPCDamage &damage) const;
		std::vector<RelayDelivery> CombatHitDeliveries(const SharedCombatHit &hit) const;
		std::vector<RelayDelivery> WeaponFireDeliveries(const SharedWeaponFire &fire) const;
		std::vector<RelayDelivery> NPCBoardingDeliveries(const SharedNPCBoarding &boarding) const;
		std::vector<RelayDelivery> MissionEventDeliveries(const SharedMissionEvent &event) const;
		std::vector<RelayDelivery> ResourceEventDeliveries(const SharedResourceEvent &event) const;
		std::vector<RelayDelivery> ReceiveServerNPCDamage(const SharedNPCDamage &damage);
		std::vector<RelayDelivery> ReceiveServerNPCBoarding(const SharedNPCBoarding &boarding);

		uint64_t nextPlayerNumber = 1;
		std::vector<Peer> peers;
		PresenceStore presence;
		SystemAuthorityStore authorities;
		PeerEndpointStore peerEndpoints;
		SharedNPCStore sharedNPCs;
		SharedMissionEventLog missionEvents;
		SharedResourceEventLog resourceEvents;
		uint64_t nextResourceReplayIndex = 1;
		std::vector<ResourceReplayEntry> resourceReplay;
		std::string roomPassword;

		struct ServerNPC {
			SharedNPCSnapshot snapshot;
			Point home;
			Point destination;
			uint64_t courseSeed = 0;
			uint64_t lastPublishedStep = 0;
		};
		bool serverWorldEnabled = false;
		uint64_t serverWorldStep = 0;
		uint64_t nextServerNPCId = 1;
		uint64_t nextServerNPCSequence = 1;
		uint64_t nextServerBoardingSequence = 1;
		uint64_t nextServerMissionEventSequence = 1;
		uint64_t nextServerResourceEventSequence = 1;
		std::vector<ServerNPC> serverNPCs;
	};

	class ClientSession {
	public:
		void StartJoin(std::string playerName, std::string password = {});
		void Disconnect();
		void SetError(std::string reason = {});
		bool AcceptWelcome(const std::string &line);
		bool ReceiveRelayLine(const std::string &line);
		void SetSimulationActive(bool active) noexcept;
		void StepConnectionHealth(unsigned steps = 1);
		std::vector<OutgoingMessage> StepLocal(PlayerInfo &player);

		ConnectionState State() const noexcept;
		const std::string &PlayerId() const noexcept;
		const std::string &PlayerName() const noexcept;
		const std::string &Password() const noexcept;
		std::string StatusText() const;

		const PresenceStore &Remotes() const noexcept;
		const AuthorityStore &Authorities() const noexcept;
		const PeerEndpointStore &PeerEndpoints() const noexcept;
		const SharedNPCStore &SharedNPCs() const noexcept;
		const SharedMissionEventLog &MissionEvents() const noexcept;
		const SharedResourceEventLog &ResourceEvents() const noexcept;
		std::vector<SharedNPCDamage> TakeNPCDamageReports();
		std::vector<SharedCombatHit> TakeCombatHits();
		std::vector<SharedWeaponFire> TakeWeaponFires();
		std::vector<SharedNPCBoarding> TakeNPCBoardingReports();
		std::vector<SharedMissionEvent> TakeMissionEvents();
		std::vector<SharedResourceEvent> TakeResourceEvents();
		bool IsSystemAuthority(const std::string &system) const;
		const std::vector<PlayerEvent> &RecentEvents() const noexcept;
		const std::vector<std::string> &DesyncWarnings() const noexcept;
		LocalStateEmitter &Emitter() noexcept;
		const LocalStateEmitter &Emitter() const noexcept;

		static std::string JoinLine(const std::string &playerName, const std::string &password = {});

	private:
		void RecordDesyncWarning(std::string warning);
		void NoteRemoteSnapshot(const PlayerSnapshot &snapshot);
		bool AcceptEvent(const PlayerEvent &event);
		bool AcceptNPCDamageReport(const SharedNPCDamage &damage, bool *exactDuplicate = nullptr);
		bool AcceptCombatHit(const SharedCombatHit &hit, bool *exactDuplicate = nullptr);
		bool AcceptWeaponFire(const SharedWeaponFire &fire, bool *exactDuplicate = nullptr);
		bool AcceptNPCBoardingReport(const SharedNPCBoarding &boarding, bool *exactDuplicate = nullptr);

		struct DamageReportSequence {
			std::string reporterId;
			uint64_t latestSequence = 0;
		};
		struct CombatHitSequence {
			std::string attackerId;
			uint64_t latestSequence = 0;
		};
		struct WeaponFireSequence {
			std::string playerId;
			uint64_t latestSequence = 0;
		};
		struct BoardingReportSequence {
			std::string sourceId;
			bool isRequest = false;
			uint64_t latestSequence = 0;
		};
		struct EventSequence {
			std::string playerId;
			uint64_t latestSequence = 0;
		};
		struct RemoteSnapshotActivity {
			std::string playerId;
			uint64_t lastSeenStep = 0;
			uint64_t latestSequence = 0;
			bool staleWarningSent = false;
		};

		ConnectionState state = ConnectionState::DISCONNECTED;
		std::string playerId;
		std::string playerName;
		std::string password;
		std::string errorReason;
		PresenceStore remotes;
		AuthorityStore authorities;
		PeerEndpointStore peerEndpointStore;
		SharedNPCStore sharedNPCs;
		SharedMissionEventLog missionEventLog;
		SharedResourceEventLog resourceEventLog;
		std::vector<SharedNPCDamage> npcDamageReports;
		std::vector<SharedCombatHit> combatHits;
		std::vector<SharedWeaponFire> weaponFires;
		std::vector<DamageReportSequence> npcDamageSequences;
		std::vector<CombatHitSequence> combatHitSequences;
		std::vector<WeaponFireSequence> weaponFireSequences;
		std::vector<SharedNPCBoarding> npcBoardingReports;
		std::vector<BoardingReportSequence> npcBoardingSequences;
		std::vector<EventSequence> eventSequences;
		std::vector<RemoteSnapshotActivity> remoteSnapshotActivity;
		std::vector<SharedMissionEvent> missionEvents;
		std::vector<SharedResourceEvent> resourceEvents;
		std::vector<PlayerEvent> recentEvents;
		std::vector<std::string> desyncWarnings;
		uint64_t connectionStep = 0;
		LocalStateEmitter emitter;
	};

	static std::string Serialize(const PlayerSnapshot &snapshot);
	static std::optional<PlayerSnapshot> ParseSnapshot(const std::string &message);

	static std::string Serialize(const PlayerEvent &event);
	static std::optional<PlayerEvent> ParseEvent(const std::string &message);

	static std::string Serialize(const SystemAuthority &authority);
	static std::optional<SystemAuthority> ParseAuthority(const std::string &message);

	static std::string Serialize(const PeerEndpoint &endpoint);
	static std::optional<PeerEndpoint> ParsePeerEndpoint(const std::string &message);

	static std::string Serialize(const SharedNPCSnapshot &snapshot);
	static std::optional<SharedNPCSnapshot> ParseNPCSnapshot(const std::string &message);

	static std::string Serialize(const SharedNPCDamage &damage);
	static std::optional<SharedNPCDamage> ParseNPCDamage(const std::string &message);

	static std::string Serialize(const SharedCombatHit &hit);
	static std::optional<SharedCombatHit> ParseCombatHit(const std::string &message);

	static std::string Serialize(const SharedWeaponFire &fire);
	static std::optional<SharedWeaponFire> ParseWeaponFire(const std::string &message);

	static std::string Serialize(const SharedNPCBoarding &boarding);
	static std::optional<SharedNPCBoarding> ParseNPCBoarding(const std::string &message);

	static std::string Serialize(const SharedMissionEvent &event);
	static std::optional<SharedMissionEvent> ParseMissionEvent(const std::string &message);

	static std::string Serialize(const SharedResourceEvent &event);
	static std::optional<SharedResourceEvent> ParseResourceEvent(const std::string &message);

	static std::optional<std::string> ParseJoinName(const std::string &message);
	static std::optional<JoinRequest> ParseJoinRequest(const std::string &message);
	static RelayEndpoint ParseEndpoint(std::string address, uint16_t defaultPort = DEFAULT_PORT);

	static const char *ToString(EventType type) noexcept;
	static std::optional<EventType> EventTypeFromString(const std::string &type);

	static std::optional<PlayerSnapshot> SnapshotFromPlayer(const PlayerInfo &player,
		const std::string &playerId, uint64_t sequence);
};
