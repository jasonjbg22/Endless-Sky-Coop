/* test_coopRelay.cpp
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

#include "es-test.hpp"

#include "../../../source/PlayerInfo.h"
#include "../../../source/multiplayer/CoOpRelay.h"

#include <algorithm>
#include <limits>



namespace {
	CoOpRelay::PlayerSnapshot Snapshot(uint64_t sequence = 1)
	{
		CoOpRelay::PlayerSnapshot snapshot;
		snapshot.sequence = sequence;
		snapshot.playerId = "peer-a";
		snapshot.name = "Captain Relay";
		snapshot.system = "Sol";
		snapshot.position = Point(10., -20.);
		snapshot.velocity = Point(.5, 1.5);
		snapshot.facing = Angle(45.);
		snapshot.shipModel = "Shuttle";
		snapshot.loadoutHash = 123456789;
		snapshot.shields = .9;
		snapshot.hull = .8;
		snapshot.fuel = .7;
		snapshot.energy = .6;
		snapshot.heat = .5;
		snapshot.simulationActive = true;
		return snapshot;
	}



	CoOpRelay::SharedNPCSnapshot NPCSnapshot(uint64_t sequence = 1)
	{
		CoOpRelay::SharedNPCSnapshot snapshot;
		snapshot.sequence = sequence;
		snapshot.npcId = "npc-sol-pirate-1";
		snapshot.ownerId = "player-1";
		snapshot.system = "Sol";
		snapshot.position = Point(100., -200.);
		snapshot.velocity = Point(3., 4.);
		snapshot.facing = Angle(135.);
		snapshot.shipModel = "Sparrow";
		snapshot.government = "Pirate";
		snapshot.targetId = "player-2";
		snapshot.shields = .75;
		snapshot.hull = .5;
		snapshot.fuel = .4;
		snapshot.energy = .3;
		snapshot.heat = .2;
		return snapshot;
	}



	CoOpRelay::SharedNPCDamage NPCDamage(uint64_t sequence = 1)
	{
		CoOpRelay::SharedNPCDamage damage;
		damage.sequence = sequence;
		damage.npcId = "npc-sol-pirate-1";
		damage.ownerId = "player-1";
		damage.system = "Sol";
		damage.reporterId = "player-2";
		damage.shieldDamage = .1;
		damage.hullDamage = .2;
		damage.fuelDamage = .03;
		damage.energyDamage = .04;
		damage.heatDamage = .05;
		return damage;
	}



	CoOpRelay::SharedCombatHit CombatHit(uint64_t sequence = 1)
	{
		CoOpRelay::SharedCombatHit hit;
		hit.sequence = sequence;
		hit.attackerId = "server-npc-1";
		hit.targetPlayerId = "player-2";
		hit.system = "Sol";
		hit.attackerModel = "Sparrow";
		hit.attackerGovernment = "Pirate";
		hit.shieldDamage = .1;
		hit.hullDamage = .2;
		hit.fuelDamage = .03;
		hit.energyDamage = .04;
		hit.heatDamage = .05;
		hit.weapon = "laser";
		hit.impactPosition = Point(100., 200.);
		hit.hitVelocity = Point(3., -4.);
		hit.facing = 12.;
		hit.detail = "Shared Sparrow hit you.";
		return hit;
	}



	CoOpRelay::SharedWeaponFire WeaponFire(uint64_t sequence = 1)
	{
		CoOpRelay::SharedWeaponFire fire;
		fire.sequence = sequence;
		fire.playerId = "player-1";
		fire.system = "Sol";
		fire.from = Point(10., 20.);
		fire.to = Point(120., 140.);
		fire.velocity = Point(6., -2.);
		fire.facing = 42.;
		fire.shipPosition = Point(4., 8.);
		fire.shipVelocity = Point(1.5, -0.5);
		fire.hasShipVelocity = true;
		fire.targetNPCId = "shared-npc-1";
		fire.weapon = "laser";
		return fire;
	}



	CoOpRelay::SharedNPCBoarding NPCBoarding(uint64_t sequence = 1)
	{
		CoOpRelay::SharedNPCBoarding boarding;
		boarding.sequence = sequence;
		boarding.npcId = "npc-sol-pirate-1";
		boarding.playerId = "player-2";
		boarding.ownerId = "player-1";
		boarding.system = "Sol";
		boarding.action = CoOpRelay::BoardingAction::REQUEST;
		boarding.detail = "capture";
		return boarding;
	}



	CoOpRelay::SharedMissionEvent MissionEvent(uint64_t sequence = 1)
	{
		CoOpRelay::SharedMissionEvent event;
		event.sequence = sequence;
		event.missionId = "escort-freighter";
		event.instanceId = "coop-mission-1";
		event.playerId = "player-1";
		event.type = CoOpRelay::MissionEventType::OBJECTIVE_UPDATED;
		event.system = "Sol";
		event.npcId = "npc-sol-pirate-1";
		event.detail = "Pirate disabled";
		event.credits = 12000;
		return event;
	}



	CoOpRelay::PeerEndpoint PeerEndpoint(uint64_t sequence = 1)
	{
		CoOpRelay::PeerEndpoint endpoint;
		endpoint.sequence = sequence;
		endpoint.playerId = "player-1";
		endpoint.host = "100.64.0.10";
		endpoint.port = 5052;
		return endpoint;
	}



	CoOpRelay::SharedResourceEvent ResourceEvent(uint64_t sequence = 1)
	{
		CoOpRelay::SharedResourceEvent event;
		event.sequence = sequence;
		event.actionId = "resource-action-1";
		event.playerId = "player-1";
		event.targetPlayerId = "player-2";
		event.type = CoOpRelay::ResourceActionType::FUEL_TRANSFER;
		event.status = CoOpRelay::ResourceActionStatus::REQUEST;
		event.resource = "fuel";
		event.amount = 20.;
		event.detail = "requesting fuel transfer";
		return event;
	}



	int SnapshotDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.snapshot.has_value(); }));
	}



	int EventDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.event.has_value(); }));
	}



	int AuthorityDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.authority.has_value(); }));
	}



	int PeerEndpointDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.peerEndpoint.has_value(); }));
	}



	int NPCDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.npc.has_value(); }));
	}



	int NPCDamageDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.npcDamage.has_value(); }));
	}



	int CombatHitDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.combatHit.has_value(); }));
	}



	int WeaponFireDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.weaponFire.has_value(); }));
	}



	int NPCBoardingDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.npcBoarding.has_value(); }));
	}



	int MissionEventDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.missionEvent.has_value(); }));
	}



	int ResourceEventDeliveries(const std::vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		return static_cast<int>(std::count_if(deliveries.begin(), deliveries.end(),
			[](const CoOpRelay::RelayDelivery &delivery) { return delivery.resourceEvent.has_value(); }));
	}



	const CoOpRelay::RelayDelivery *FindAuthorityDelivery(
		const std::vector<CoOpRelay::RelayDelivery> &deliveries, const std::string &recipientId,
		const std::string &system)
	{
		auto it = std::find_if(deliveries.begin(), deliveries.end(),
			[&recipientId, &system](const CoOpRelay::RelayDelivery &delivery) {
				return delivery.recipientId == recipientId && delivery.authority
					&& delivery.authority->system == system;
			});
		return it == deliveries.end() ? nullptr : &*it;
	}



	const CoOpRelay::RelayDelivery *FindNPCDelivery(
		const std::vector<CoOpRelay::RelayDelivery> &deliveries, const std::string &recipientId,
		const std::string &npcId)
	{
		auto it = std::find_if(deliveries.begin(), deliveries.end(),
			[&recipientId, &npcId](const CoOpRelay::RelayDelivery &delivery) {
				return delivery.recipientId == recipientId && delivery.npc
					&& delivery.npc->npcId == npcId;
			});
		return it == deliveries.end() ? nullptr : &*it;
	}
}



TEST_CASE( "Co-op relay snapshots serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::PlayerSnapshot snapshot = Snapshot();
	snapshot.name = "Captain\tRelay\nTwo";
	snapshot.landedPlanet = "New Boston";

	const std::string wire = CoOpRelay::Serialize(snapshot);
	auto parsed = CoOpRelay::ParseSnapshot(wire);

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == snapshot.sequence);
	CHECK(parsed->playerId == snapshot.playerId);
	CHECK(parsed->name == snapshot.name);
	CHECK(parsed->system == snapshot.system);
	CHECK(parsed->position == snapshot.position);
	CHECK(parsed->velocity == snapshot.velocity);
	CHECK(parsed->facing == snapshot.facing);
	CHECK(parsed->landedPlanet == snapshot.landedPlanet);
	CHECK(parsed->shipModel == snapshot.shipModel);
	CHECK(parsed->loadoutHash == snapshot.loadoutHash);
	CHECK(parsed->shields == snapshot.shields);
	CHECK(parsed->hull == snapshot.hull);
	CHECK(parsed->fuel == snapshot.fuel);
	CHECK(parsed->energy == snapshot.energy);
	CHECK(parsed->heat == snapshot.heat);
	CHECK(parsed->simulationActive == snapshot.simulationActive);
	CHECK(parsed->IsLanded());
}



TEST_CASE( "Co-op relay rejects incompatible or malformed messages", "[CoOpRelay]" )
{
	CoOpRelay::PlayerSnapshot snapshot = Snapshot();
	snapshot.protocolVersion = CoOpRelay::PROTOCOL_VERSION + 1;

	CHECK_FALSE(CoOpRelay::ParseSnapshot(CoOpRelay::Serialize(snapshot)));
	CHECK_FALSE(CoOpRelay::ParseSnapshot("snapshot\t1\tbad-number"));
	CHECK_FALSE(CoOpRelay::ParseSnapshot("not-a-snapshot"));
	CHECK_FALSE(CoOpRelay::ParseEvent("event\t1\t1\tpeer-a\tunknown\tSol\t\t"));
}



TEST_CASE( "Co-op relay events serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::PlayerEvent event;
	event.sequence = 2;
	event.playerId = "peer-a";
	event.type = CoOpRelay::EventType::JUMPED;
	event.system = "Alpha Centauri";
	event.detail = "Sol";

	auto parsed = CoOpRelay::ParseEvent(CoOpRelay::Serialize(event));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == event.sequence);
	CHECK(parsed->playerId == event.playerId);
	CHECK(parsed->type == event.type);
	CHECK(parsed->system == event.system);
	CHECK(parsed->planet == event.planet);
	CHECK(parsed->detail == event.detail);
}



TEST_CASE( "Co-op relay system authority serializes and parses", "[CoOpRelay]" )
{
	CoOpRelay::SystemAuthority authority;
	authority.sequence = 3;
	authority.system = "Sol";
	authority.ownerId = "player-1";
	authority.playerCount = 2;

	auto parsed = CoOpRelay::ParseAuthority(CoOpRelay::Serialize(authority));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == authority.sequence);
	CHECK(parsed->system == authority.system);
	CHECK(parsed->ownerId == authority.ownerId);
	CHECK(parsed->playerCount == authority.playerCount);
	CHECK(parsed->IsActive());

	authority.ownerId.clear();
	authority.playerCount = 0;
	CHECK_FALSE(authority.IsActive());
	CHECK_FALSE(CoOpRelay::ParseAuthority("authority\t1\tbad-number\tSol\tplayer-1\t1"));
}



TEST_CASE( "Co-op relay peer endpoints serialize, parse, and track latest state", "[CoOpRelay]" )
{
	CoOpRelay::PeerEndpoint endpoint = PeerEndpoint(4);
	endpoint.host = "relay\tpeer";

	auto parsed = CoOpRelay::ParsePeerEndpoint(CoOpRelay::Serialize(endpoint));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == endpoint.sequence);
	CHECK(parsed->playerId == endpoint.playerId);
	CHECK(parsed->host == endpoint.host);
	CHECK(parsed->port == endpoint.port);
	CHECK_FALSE(parsed->removed);

	CoOpRelay::PeerEndpointStore store;
	REQUIRE(store.Apply(*parsed));

	CoOpRelay::PeerEndpoint stale = endpoint;
	stale.sequence = 3;
	stale.host = "stale.example";
	CHECK_FALSE(store.Apply(stale));
	REQUIRE(store.Get(endpoint.playerId));
	CHECK(store.Get(endpoint.playerId)->host == endpoint.host);

	CoOpRelay::PeerEndpoint updated = endpoint;
	updated.sequence = 5;
	updated.host = "100.64.0.11";
	REQUIRE(store.Apply(updated));
	CHECK(store.Get(endpoint.playerId)->host == updated.host);

	std::optional<CoOpRelay::PeerEndpoint> removed = store.Remove(endpoint.playerId);
	REQUIRE(removed);
	CHECK(removed->removed);
	CHECK(removed->sequence == updated.sequence + 1);
	CHECK(store.Get(endpoint.playerId) == nullptr);

	CHECK_FALSE(CoOpRelay::ParsePeerEndpoint("peer-endpoint\t1\tbad-number"));
	updated.port = 0;
	CHECK_FALSE(CoOpRelay::ParsePeerEndpoint(CoOpRelay::Serialize(updated)));
	updated.port = 5052;
	updated.removed = true;
	CHECK(CoOpRelay::ParsePeerEndpoint(CoOpRelay::Serialize(updated)));
	updated.removed = false;
	std::string malformedRemoved = CoOpRelay::Serialize(updated);
	malformedRemoved.back() = '2';
	CHECK_FALSE(CoOpRelay::ParsePeerEndpoint(malformedRemoved));
}



TEST_CASE( "Co-op relay shared NPC snapshots serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::SharedNPCSnapshot snapshot = NPCSnapshot();
	snapshot.disabled = true;
	snapshot.destroyed = true;
	snapshot.captured = true;

	auto parsed = CoOpRelay::ParseNPCSnapshot(CoOpRelay::Serialize(snapshot));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == snapshot.sequence);
	CHECK(parsed->npcId == snapshot.npcId);
	CHECK(parsed->ownerId == snapshot.ownerId);
	CHECK(parsed->system == snapshot.system);
	CHECK(parsed->position == snapshot.position);
	CHECK(parsed->velocity == snapshot.velocity);
	CHECK(parsed->facing == snapshot.facing);
	CHECK(parsed->shipModel == snapshot.shipModel);
	CHECK(parsed->government == snapshot.government);
	CHECK(parsed->targetId == snapshot.targetId);
	CHECK(parsed->shields == snapshot.shields);
	CHECK(parsed->hull == snapshot.hull);
	CHECK(parsed->fuel == snapshot.fuel);
	CHECK(parsed->energy == snapshot.energy);
	CHECK(parsed->heat == snapshot.heat);
	CHECK(parsed->disabled);
	CHECK(parsed->destroyed);
	CHECK(parsed->captured);
	CHECK_FALSE(parsed->removed);

	CHECK_FALSE(CoOpRelay::ParseNPCSnapshot("npc\t1\tbad-number"));
	snapshot.npcId.clear();
	CHECK_FALSE(CoOpRelay::ParseNPCSnapshot(CoOpRelay::Serialize(snapshot)));
}



TEST_CASE( "Co-op relay shared NPC damage reports serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::SharedNPCDamage damage = NPCDamage();
	damage.disabled = true;
	damage.destroyed = true;

	auto parsed = CoOpRelay::ParseNPCDamage(CoOpRelay::Serialize(damage));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == damage.sequence);
	CHECK(parsed->npcId == damage.npcId);
	CHECK(parsed->ownerId == damage.ownerId);
	CHECK(parsed->system == damage.system);
	CHECK(parsed->reporterId == damage.reporterId);
	CHECK(parsed->shieldDamage == damage.shieldDamage);
	CHECK(parsed->hullDamage == damage.hullDamage);
	CHECK(parsed->fuelDamage == damage.fuelDamage);
	CHECK(parsed->energyDamage == damage.energyDamage);
	CHECK(parsed->heatDamage == damage.heatDamage);
	CHECK(parsed->disabled);
	CHECK(parsed->destroyed);

	CHECK_FALSE(CoOpRelay::ParseNPCDamage("npc-damage\t1\tbad-number"));
	damage.sequence = 0;
	CHECK_FALSE(CoOpRelay::ParseNPCDamage(CoOpRelay::Serialize(damage)));
	damage.sequence = 1;
	damage.shieldDamage = -0.1;
	CHECK_FALSE(CoOpRelay::ParseNPCDamage(CoOpRelay::Serialize(damage)));
	damage.shieldDamage = 1.1;
	CHECK_FALSE(CoOpRelay::ParseNPCDamage(CoOpRelay::Serialize(damage)));
	damage.shieldDamage = 0.;
	damage.hullDamage = 0.;
	damage.fuelDamage = 0.;
	damage.energyDamage = 0.;
	damage.heatDamage = 0.;
	damage.disabled = false;
	damage.destroyed = false;
	CHECK_FALSE(CoOpRelay::ParseNPCDamage(CoOpRelay::Serialize(damage)));
	damage.shieldDamage = .1;
	damage.disabled = true;
	damage.destroyed = true;
	damage.npcId.clear();
	CHECK_FALSE(CoOpRelay::ParseNPCDamage(CoOpRelay::Serialize(damage)));
	damage.npcId = "npc-sol-pirate-1";
	damage.ownerId.clear();
	CHECK_FALSE(CoOpRelay::ParseNPCDamage(CoOpRelay::Serialize(damage)));
	damage.ownerId = "player-1";
	damage.system.clear();
	CHECK_FALSE(CoOpRelay::ParseNPCDamage(CoOpRelay::Serialize(damage)));
}



TEST_CASE( "Co-op relay combat hits serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::SharedCombatHit hit = CombatHit();
	hit.disabled = true;
	hit.destroyed = true;

	auto parsed = CoOpRelay::ParseCombatHit(CoOpRelay::Serialize(hit));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == hit.sequence);
	CHECK(parsed->attackerId == hit.attackerId);
	CHECK(parsed->targetPlayerId == hit.targetPlayerId);
	CHECK(parsed->system == hit.system);
	CHECK(parsed->attackerModel == hit.attackerModel);
	CHECK(parsed->attackerGovernment == hit.attackerGovernment);
	CHECK(parsed->shieldDamage == hit.shieldDamage);
	CHECK(parsed->hullDamage == hit.hullDamage);
	CHECK(parsed->fuelDamage == hit.fuelDamage);
	CHECK(parsed->energyDamage == hit.energyDamage);
	CHECK(parsed->heatDamage == hit.heatDamage);
	CHECK(parsed->disabled);
	CHECK(parsed->destroyed);
	CHECK(parsed->weapon == hit.weapon);
	CHECK(parsed->impactPosition == hit.impactPosition);
	CHECK(parsed->hitVelocity == hit.hitVelocity);
	CHECK(parsed->facing == hit.facing);
	CHECK(parsed->detail == hit.detail);

	auto legacy = CoOpRelay::ParseCombatHit("combat-hit\t" + std::to_string(CoOpRelay::PROTOCOL_VERSION)
		+ "\t1\tserver-npc-1\tplayer-2\tSol\tSparrow\tPirate\t0.1\t0.2\t0.03\t0.04\t0.05\t1\t1\tPvP hit");
	REQUIRE(legacy);
	CHECK(legacy->weapon.empty());
	CHECK(legacy->impactPosition == Point());

	CHECK_FALSE(CoOpRelay::ParseCombatHit("combat-hit\t1\tbad-number"));
	hit.sequence = 0;
	CHECK_FALSE(CoOpRelay::ParseCombatHit(CoOpRelay::Serialize(hit)));
	hit.sequence = 1;
	hit.shieldDamage = -0.1;
	CHECK_FALSE(CoOpRelay::ParseCombatHit(CoOpRelay::Serialize(hit)));
	hit.shieldDamage = 1.1;
	CHECK_FALSE(CoOpRelay::ParseCombatHit(CoOpRelay::Serialize(hit)));
	hit.shieldDamage = 0.;
	hit.hullDamage = 0.;
	hit.fuelDamage = 0.;
	hit.energyDamage = 0.;
	hit.heatDamage = 0.;
	hit.disabled = false;
	hit.destroyed = false;
	CHECK_FALSE(CoOpRelay::ParseCombatHit(CoOpRelay::Serialize(hit)));
	hit.shieldDamage = .1;
	hit.attackerId.clear();
	CHECK_FALSE(CoOpRelay::ParseCombatHit(CoOpRelay::Serialize(hit)));
	hit.attackerId = "server-npc-1";
	hit.targetPlayerId.clear();
	CHECK_FALSE(CoOpRelay::ParseCombatHit(CoOpRelay::Serialize(hit)));
	hit.targetPlayerId = "player-2";
	hit.system.clear();
	CHECK_FALSE(CoOpRelay::ParseCombatHit(CoOpRelay::Serialize(hit)));
}



TEST_CASE( "Co-op relay weapon fire visuals serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::SharedWeaponFire fire = WeaponFire();

	auto parsed = CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == fire.sequence);
	CHECK(parsed->playerId == fire.playerId);
	CHECK(parsed->system == fire.system);
	CHECK(parsed->from == fire.from);
	CHECK(parsed->to == fire.to);
	CHECK(parsed->velocity == fire.velocity);
	CHECK(parsed->facing == fire.facing);
	CHECK(parsed->shipPosition == fire.shipPosition);
	CHECK(parsed->shipVelocity == fire.shipVelocity);
	CHECK(parsed->hasShipVelocity);
	CHECK(parsed->targetPlayerId == fire.targetPlayerId);
	CHECK(parsed->targetNPCId == fire.targetNPCId);
	CHECK(parsed->weapon == fire.weapon);

	auto legacy = CoOpRelay::ParseWeaponFire("weapon-fire\t" + std::to_string(CoOpRelay::PROTOCOL_VERSION)
		+ "\t1\tplayer-1\tSol\t10\t20\t120\t140\tlaser");
	REQUIRE(legacy);
	CHECK(legacy->velocity == legacy->to - legacy->from);
	CHECK_FALSE(legacy->hasShipVelocity);
	CHECK(legacy->targetPlayerId.empty());

	auto motionLegacy = CoOpRelay::ParseWeaponFire("weapon-fire\t" + std::to_string(CoOpRelay::PROTOCOL_VERSION)
		+ "\t1\tplayer-1\tSol\t10\t20\t120\t140\tlaser\t6\t-2\t42\t4\t8\tplayer-2");
	REQUIRE(motionLegacy);
	CHECK(motionLegacy->velocity == Point(6., -2.));
	CHECK(motionLegacy->facing == 42.);
	CHECK(motionLegacy->shipPosition == Point(4., 8.));
	CHECK_FALSE(motionLegacy->hasShipVelocity);
	CHECK(motionLegacy->targetPlayerId == "player-2");
	CHECK(motionLegacy->targetNPCId.empty());

	auto targeted = CoOpRelay::ParseWeaponFire("weapon-fire\t" + std::to_string(CoOpRelay::PROTOCOL_VERSION)
		+ "\t1\tplayer-1\tSol\t10\t20\t120\t140\tlaser\t6\t-2\t42\t4\t8\t\t1.5\t-0.5\tshared-npc-1");
	REQUIRE(targeted);
	CHECK(targeted->hasShipVelocity);
	CHECK(targeted->targetPlayerId.empty());
	CHECK(targeted->targetNPCId == "shared-npc-1");

	auto playerTargeted = CoOpRelay::ParseWeaponFire("weapon-fire\t" + std::to_string(CoOpRelay::PROTOCOL_VERSION)
		+ "\t1\tplayer-1\tSol\t10\t20\t120\t140\tlaser\t6\t-2\t42\t4\t8\tplayer-2\t1.5\t-0.5\t");
	REQUIRE(playerTargeted);
	CHECK(playerTargeted->targetPlayerId == "player-2");
	CHECK(playerTargeted->targetNPCId.empty());

	CHECK_FALSE(CoOpRelay::ParseWeaponFire("weapon-fire\t1\tbad-number"));
	fire.sequence = 0;
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
	fire.sequence = 1;
	fire.playerId.clear();
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
	fire.playerId = "player-1";
	fire.system.clear();
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
	fire.system = "Sol";
	fire.to = fire.from;
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
	fire.to = Point(5000., 5000.);
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
	fire = WeaponFire();
	fire.weapon.clear();
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
	fire = WeaponFire();
	fire.targetPlayerId = "player-2";
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
	fire = WeaponFire();
	fire.targetPlayerId = fire.playerId;
	fire.targetNPCId.clear();
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
	fire = WeaponFire();
	fire.shipVelocity = Point(std::numeric_limits<double>::infinity(), 0.);
	CHECK_FALSE(CoOpRelay::ParseWeaponFire(CoOpRelay::Serialize(fire)));
}



TEST_CASE( "Co-op relay shared NPC boarding messages serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::SharedNPCBoarding boarding = NPCBoarding();

	auto parsed = CoOpRelay::ParseNPCBoarding(CoOpRelay::Serialize(boarding));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == boarding.sequence);
	CHECK(parsed->npcId == boarding.npcId);
	CHECK(parsed->playerId == boarding.playerId);
	CHECK(parsed->ownerId == boarding.ownerId);
	CHECK(parsed->system == boarding.system);
	CHECK(parsed->action == CoOpRelay::BoardingAction::REQUEST);
	CHECK(parsed->detail == boarding.detail);
	CHECK(parsed->IsRequest());

	boarding.action = CoOpRelay::BoardingAction::CAPTURED;
	boarding.ownerId = "player-1";
	boarding.detail = "captured";
	parsed = CoOpRelay::ParseNPCBoarding(CoOpRelay::Serialize(boarding));
	REQUIRE(parsed);
	CHECK(parsed->action == CoOpRelay::BoardingAction::CAPTURED);
	CHECK_FALSE(parsed->IsRequest());

	CHECK_FALSE(CoOpRelay::ParseNPCBoarding("npc-boarding\t1\tbad-number"));
	boarding.sequence = 0;
	CHECK_FALSE(CoOpRelay::ParseNPCBoarding(CoOpRelay::Serialize(boarding)));
	boarding.sequence = 2;
	boarding.npcId.clear();
	CHECK_FALSE(CoOpRelay::ParseNPCBoarding(CoOpRelay::Serialize(boarding)));
	boarding.npcId = "npc-sol-pirate-1";
	boarding.ownerId.clear();
	CHECK_FALSE(CoOpRelay::ParseNPCBoarding(CoOpRelay::Serialize(boarding)));
	boarding.ownerId = "player-1";
	boarding.system.clear();
	CHECK_FALSE(CoOpRelay::ParseNPCBoarding(CoOpRelay::Serialize(boarding)));
	boarding = NPCBoarding();
	boarding.playerId = boarding.ownerId;
	CHECK_FALSE(CoOpRelay::ParseNPCBoarding(CoOpRelay::Serialize(boarding)));
}



TEST_CASE( "Co-op relay shared mission events serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::SharedMissionEvent event = MissionEvent();
	event.detail = "Objective\tupdated\nReward pending";

	auto parsed = CoOpRelay::ParseMissionEvent(CoOpRelay::Serialize(event));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == event.sequence);
	CHECK(parsed->missionId == event.missionId);
	CHECK(parsed->instanceId == event.instanceId);
	CHECK(parsed->playerId == event.playerId);
	CHECK(parsed->type == CoOpRelay::MissionEventType::OBJECTIVE_UPDATED);
	CHECK(parsed->system == event.system);
	CHECK(parsed->npcId == event.npcId);
	CHECK(parsed->detail == event.detail);
	CHECK(parsed->credits == event.credits);

	event.type = CoOpRelay::MissionEventType::REWARD;
	event.detail = "credits";
	parsed = CoOpRelay::ParseMissionEvent(CoOpRelay::Serialize(event));
	REQUIRE(parsed);
	CHECK(parsed->type == CoOpRelay::MissionEventType::REWARD);

	event.type = CoOpRelay::MissionEventType::NPC_BOARDED;
	event.detail = "Shared pirate boarded";
	parsed = CoOpRelay::ParseMissionEvent(CoOpRelay::Serialize(event));
	REQUIRE(parsed);
	CHECK(parsed->type == CoOpRelay::MissionEventType::NPC_BOARDED);

	CHECK_FALSE(CoOpRelay::ParseMissionEvent("mission-event\t1\tbad-number"));
	event.sequence = 0;
	CHECK_FALSE(CoOpRelay::ParseMissionEvent(CoOpRelay::Serialize(event)));
	event.sequence = 1;
	event.instanceId.clear();
	CHECK_FALSE(CoOpRelay::ParseMissionEvent(CoOpRelay::Serialize(event)));
	event.instanceId = "coop-mission-1";
	event.credits = -1;
	CHECK_FALSE(CoOpRelay::ParseMissionEvent(CoOpRelay::Serialize(event)));
	event.credits = 1000000001;
	CHECK_FALSE(CoOpRelay::ParseMissionEvent(CoOpRelay::Serialize(event)));
}



TEST_CASE( "Co-op relay shared resource events serialize and parse", "[CoOpRelay]" )
{
	CoOpRelay::SharedResourceEvent event = ResourceEvent();
	event.detail = "Fuel\ttransfer\npending";

	auto parsed = CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event));

	REQUIRE(parsed);
	CHECK(parsed->protocolVersion == CoOpRelay::PROTOCOL_VERSION);
	CHECK(parsed->sequence == event.sequence);
	CHECK(parsed->actionId == event.actionId);
	CHECK(parsed->playerId == event.playerId);
	CHECK(parsed->targetPlayerId == event.targetPlayerId);
	CHECK(parsed->type == CoOpRelay::ResourceActionType::FUEL_TRANSFER);
	CHECK(parsed->status == CoOpRelay::ResourceActionStatus::REQUEST);
	CHECK(parsed->resource == event.resource);
	CHECK(parsed->amount == event.amount);
	CHECK(parsed->detail == event.detail);

	event.type = CoOpRelay::ResourceActionType::CREDIT_REWARD;
	event.status = CoOpRelay::ResourceActionStatus::APPLIED;
	event.resource = "credits";
	parsed = CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event));
	REQUIRE(parsed);
	CHECK(parsed->type == CoOpRelay::ResourceActionType::CREDIT_REWARD);
	CHECK(parsed->status == CoOpRelay::ResourceActionStatus::APPLIED);

	event.status = CoOpRelay::ResourceActionStatus::REJECTED;
	event.amount = 0.;
	CHECK_FALSE(CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event)));
	event.status = CoOpRelay::ResourceActionStatus::REQUEST;
	event.amount = 1.;
	CHECK_FALSE(CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event)));

	CHECK_FALSE(CoOpRelay::ParseResourceEvent("resource-event\t1\tbad-number"));
	event.sequence = 0;
	CHECK_FALSE(CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event)));
	event.sequence = 1;
	event.amount = -1.;
	CHECK_FALSE(CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event)));
	event.amount = std::numeric_limits<double>::infinity();
	CHECK_FALSE(CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event)));
	event.amount = 1000000001.;
	CHECK_FALSE(CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event)));
	event.amount = 0.;
	event.status = CoOpRelay::ResourceActionStatus::REQUEST;
	CHECK_FALSE(CoOpRelay::ParseResourceEvent(CoOpRelay::Serialize(event)));
}



TEST_CASE( "Co-op relay endpoints parse user join addresses", "[CoOpRelay]" )
{
	CoOpRelay::RelayEndpoint endpoint = CoOpRelay::ParseEndpoint("relay.example.net:24872");
	CHECK(endpoint.host == "relay.example.net");
	CHECK(endpoint.port == 24872);

	endpoint = CoOpRelay::ParseEndpoint(" relay.example.net ");
	CHECK(endpoint.host == "relay.example.net");
	CHECK(endpoint.port == CoOpRelay::DEFAULT_PORT);

	endpoint = CoOpRelay::ParseEndpoint("");
	CHECK(endpoint.host == "127.0.0.1");
	CHECK(endpoint.port == CoOpRelay::DEFAULT_PORT);

	endpoint = CoOpRelay::ParseEndpoint(":5051");
	CHECK(endpoint.host == "127.0.0.1");
	CHECK(endpoint.port == 5051);

	endpoint = CoOpRelay::ParseEndpoint("relay.example.net:not-a-port", 6000);
	CHECK(endpoint.host == "relay.example.net");
	CHECK(endpoint.port == 6000);

	endpoint = CoOpRelay::ParseEndpoint("::1", 6000);
	CHECK(endpoint.host == "::1");
	CHECK(endpoint.port == 6000);
}



TEST_CASE( "Co-op presence store keeps remote players separate and ordered", "[CoOpRelay]" )
{
	CoOpRelay::PresenceStore store;
	CoOpRelay::PlayerSnapshot first = Snapshot(10);
	CoOpRelay::PlayerSnapshot second = Snapshot(11);
	second.position = Point(20., 40.);
	CoOpRelay::PlayerSnapshot stale = Snapshot(9);
	stale.system = "Sirius";

	CHECK(store.Apply(first));
	CHECK(store.Apply(second));
	CHECK_FALSE(store.Apply(stale));
	CoOpRelay::PlayerSnapshot duplicate = second;
	duplicate.position = Point(-100., -100.);
	CHECK_FALSE(store.Apply(duplicate));

	const CoOpRelay::RemotePresence *presence = store.Get("peer-a");
	REQUIRE(presence);
	CHECK(presence->latest.sequence == 11);
	CHECK(presence->latest.system == "Sol");
	CHECK(presence->latest.position == second.position);
	CHECK(presence->hasPrevious);
	CHECK(presence->previous.sequence == 10);

	CHECK(store.InSystem("Sol").size() == 1);
	CHECK(store.InSystem("Sirius").empty());
}



TEST_CASE( "Co-op presence hides landed players from same-system flight contacts", "[CoOpRelay]" )
{
	CoOpRelay::PresenceStore store;
	CoOpRelay::PlayerSnapshot landed = Snapshot();
	landed.landedPlanet = "Earth";

	REQUIRE(store.Apply(landed));

	const CoOpRelay::RemotePresence *presence = store.Get("peer-a");
	REQUIRE(presence);
	CHECK(presence->IsLanded());
	CHECK_FALSE(presence->IsInSystem("Sol"));
	CHECK(store.InSystem("Sol").empty());

	CoOpRelay::PlayerSnapshot jumping = Snapshot(2);
	jumping.simulationActive = false;
	REQUIRE(store.Apply(jumping));
	CHECK_FALSE(store.Get(jumping.playerId)->IsInSystem("Sol"));
	CHECK(store.InSystem("Sol").empty());
}



TEST_CASE( "Co-op presence keeps roster status separate from flight contacts", "[CoOpRelay]" )
{
	CoOpRelay::PresenceStore store;
	CoOpRelay::PlayerSnapshot nearby = Snapshot(1);
	nearby.playerId = "nearby";
	nearby.system = "Sol";

	CoOpRelay::PlayerSnapshot away = Snapshot(1);
	away.playerId = "away";
	away.system = "Alpha Centauri";

	CoOpRelay::PlayerSnapshot landed = Snapshot(1);
	landed.playerId = "landed";
	landed.system = "Sol";
	landed.landedPlanet = "Earth";

	REQUIRE(store.Apply(nearby));
	REQUIRE(store.Apply(away));
	REQUIRE(store.Apply(landed));

	CHECK(store.All().size() == 3);

	std::vector<CoOpRelay::RemotePresence> solContacts = store.InSystem("Sol");
	REQUIRE(solContacts.size() == 1);
	CHECK(solContacts.front().latest.playerId == "nearby");

	const CoOpRelay::RemotePresence *awayPresence = store.Get("away");
	REQUIRE(awayPresence);
	CHECK_FALSE(awayPresence->IsInSystem("Sol"));
	CHECK_FALSE(awayPresence->IsLanded());

	const CoOpRelay::RemotePresence *landedPresence = store.Get("landed");
	REQUIRE(landedPresence);
	CHECK_FALSE(landedPresence->IsInSystem("Sol"));
	CHECK(landedPresence->IsLanded());
}



TEST_CASE( "Co-op presence interpolates same-system remote motion", "[CoOpRelay]" )
{
	CoOpRelay::PresenceStore store;
	CoOpRelay::PlayerSnapshot first = Snapshot(1);
	first.position = Point(0., 0.);
	first.velocity = Point(1., 0.);
	first.facing = Angle(0.);
	CoOpRelay::PlayerSnapshot second = Snapshot(2);
	second.position = Point(10., 20.);
	second.velocity = Point(3., 4.);
	second.facing = Angle(90.);

	REQUIRE(store.Apply(first));
	REQUIRE(store.Apply(second));

	const CoOpRelay::RemotePresence *presence = store.Get("peer-a");
	REQUIRE(presence);
	const CoOpRelay::PlayerSnapshot midpoint = presence->Interpolate(.5);

	CHECK(midpoint.position == Point(5., 10.));
	CHECK(midpoint.velocity == Point(2., 2.));
	CHECK_THAT(midpoint.facing.Degrees(), Catch::Matchers::WithinAbs(45., .0001));
}



TEST_CASE( "Co-op shared NPC store applies updates and removals", "[CoOpRelay]" )
{
	CoOpRelay::SharedNPCStore store;
	CoOpRelay::SharedNPCSnapshot first = NPCSnapshot(5);
	CoOpRelay::SharedNPCSnapshot second = NPCSnapshot(6);
	second.hull = .25;
	CoOpRelay::SharedNPCSnapshot stale = NPCSnapshot(4);
	stale.hull = 1.;

	REQUIRE(store.Apply(first));
	REQUIRE(store.Apply(second));
	CHECK_FALSE(store.Apply(stale));
	CoOpRelay::SharedNPCSnapshot duplicate = second;
	CHECK_FALSE(store.Apply(duplicate));

	const CoOpRelay::SharedNPCSnapshot *stored = store.Get(first.npcId);
	REQUIRE(stored);
	CHECK(stored->hull == .25);
	REQUIRE(store.InSystem("Sol").size() == 1);

	std::vector<CoOpRelay::SharedNPCSnapshot> reassigned = store.ReassignSystem("Sol", "player-2");
	REQUIRE(reassigned.size() == 1);
	CHECK(reassigned.front().ownerId == "player-2");
	CHECK(reassigned.front().sequence == second.sequence + 1);
	REQUIRE(store.Get(first.npcId));
	CHECK(store.Get(first.npcId)->ownerId == "player-2");
	CHECK(store.ReassignSystem("Sol", "player-2").empty());

	std::vector<CoOpRelay::SharedNPCSnapshot> removals = store.RemoveOwner("player-2");
	REQUIRE(removals.size() == 1);
	CHECK(removals.front().removed);
	CHECK(removals.front().sequence == reassigned.front().sequence + 1);
	CHECK(store.All().empty());

	REQUIRE(store.Apply(second));
	REQUIRE(store.Apply(removals.front()));
	CHECK(store.Get(first.npcId) == nullptr);
}



TEST_CASE( "Co-op server world owns authority and shared NPCs", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	server.SetServerWorldEnabled(true);
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	CHECK(SnapshotDeliveries(server.Receive(aSol)) == 1);
	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	CHECK(SnapshotDeliveries(server.Receive(bSol)) == 1);

	std::vector<CoOpRelay::RelayDelivery> worldDeliveries = server.StepServerWorld();
	REQUIRE(FindAuthorityDelivery(worldDeliveries, playerA, "Sol"));
	CHECK(FindAuthorityDelivery(worldDeliveries, playerA, "Sol")->authority->ownerId == "coop-server");
	REQUIRE(FindAuthorityDelivery(worldDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(worldDeliveries, playerB, "Sol")->authority->ownerId == "coop-server");
	CHECK(NPCDeliveries(worldDeliveries) == 12);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == "coop-server");
	CHECK(server.SharedNPCs().InSystem("Sol").size() == 6);

	const std::string playerC = server.Join("Player C");
	CoOpRelay::PlayerSnapshot cSol = Snapshot(1);
	cSol.playerId = playerC;
	CHECK(NPCDeliveries(server.Receive(cSol)) == 6);
	cSol.sequence = 2;
	CHECK(NPCDeliveries(server.Receive(cSol)) == 0);
}



TEST_CASE( "Co-op server world applies damage to server-owned NPCs", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	server.SetServerWorldEnabled(true);
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(SnapshotDeliveries(server.Receive(aSol)) == 1);
	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(SnapshotDeliveries(server.Receive(bSol)) == 1);
	REQUIRE(NPCDeliveries(server.StepServerWorld()) == 12);

	std::vector<CoOpRelay::SharedNPCSnapshot> npcs = server.SharedNPCs().InSystem("Sol");
	REQUIRE(!npcs.empty());
	CoOpRelay::SharedNPCDamage damage = NPCDamage(1);
	damage.npcId = npcs.front().npcId;
	damage.ownerId = npcs.front().ownerId;
	damage.system = npcs.front().system;
	damage.reporterId = playerA;
	damage.shieldDamage = .2;
	damage.hullDamage = .3;
	damage.fuelDamage = 0.;
	damage.energyDamage = 0.;
	damage.heatDamage = .1;

	std::vector<CoOpRelay::RelayDelivery> damageDeliveries = server.Receive(damage);
	CHECK(NPCDamageDeliveries(damageDeliveries) == 0);
	CHECK(NPCDeliveries(damageDeliveries) == 2);
	REQUIRE(server.SharedNPCs().Get(npcs.front().npcId));
	CHECK(server.SharedNPCs().Get(npcs.front().npcId)->shields == .8);
	CHECK(server.SharedNPCs().Get(npcs.front().npcId)->hull == .7);
}



TEST_CASE( "Co-op server world does not damage local-authoritative player ships", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	server.SetServerWorldEnabled(true);
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(SnapshotDeliveries(server.Receive(aSol)) == 1);
	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(SnapshotDeliveries(server.Receive(bSol)) == 1);
	REQUIRE(NPCDeliveries(server.StepServerWorld()) == 12);

	std::vector<CoOpRelay::SharedNPCSnapshot> npcs = server.SharedNPCs().InSystem("Sol");
	REQUIRE(!npcs.empty());
	aSol.sequence = 2;
	aSol.position = npcs.front().position + Point(10., 0.);
	aSol.velocity = Point();
	aSol.shields = .8;
	aSol.hull = .8;
	REQUIRE_FALSE(server.Receive(aSol).empty());

	std::vector<CoOpRelay::RelayDelivery> combatDeliveries = server.StepServerWorld(30);
	CHECK(CombatHitDeliveries(combatDeliveries) == 0);
	CHECK(server.Presence().Get(playerA)->latest.shields == .8);
	CHECK(server.Presence().Get(playerA)->latest.hull == .8);
	REQUIRE(server.SharedNPCs().Get(npcs.front().npcId));
	CHECK(server.SharedNPCs().Get(npcs.front().npcId)->targetId.empty());
}



TEST_CASE( "Co-op server world does not target inactive or jumping players", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	server.SetServerWorldEnabled(true);
	const std::string playerA = server.Join("Player A");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	aSol.simulationActive = false;
	REQUIRE(SnapshotDeliveries(server.Receive(aSol)) == 0);
	REQUIRE(NPCDeliveries(server.StepServerWorld()) == 6);

	std::vector<CoOpRelay::SharedNPCSnapshot> npcs = server.SharedNPCs().InSystem("Sol");
	REQUIRE(!npcs.empty());
	aSol.sequence = 2;
	aSol.position = npcs.front().position + Point(10., 0.);
	aSol.simulationActive = false;
	CHECK(server.Receive(aSol).empty());
	REQUIRE(server.Presence().Get(playerA));
	CHECK(server.Presence().Get(playerA)->latest.position == aSol.position);

	std::vector<CoOpRelay::RelayDelivery> combatDeliveries = server.StepServerWorld(60);
	CHECK(CombatHitDeliveries(combatDeliveries) == 0);
}



TEST_CASE( "Co-op server world handles boarding server-owned NPCs", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	server.SetServerWorldEnabled(true);
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(SnapshotDeliveries(server.Receive(aSol)) == 1);
	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(SnapshotDeliveries(server.Receive(bSol)) == 1);
	REQUIRE(NPCDeliveries(server.StepServerWorld()) == 12);

	std::vector<CoOpRelay::SharedNPCSnapshot> npcs = server.SharedNPCs().InSystem("Sol");
	REQUIRE(!npcs.empty());
	CoOpRelay::SharedNPCDamage damage = NPCDamage(1);
	damage.npcId = npcs.front().npcId;
	damage.ownerId = npcs.front().ownerId;
	damage.system = npcs.front().system;
	damage.reporterId = playerA;
	damage.shieldDamage = 0.;
	damage.hullDamage = .85;
	damage.fuelDamage = 0.;
	damage.energyDamage = 0.;
	damage.heatDamage = 0.;
	REQUIRE(NPCDeliveries(server.Receive(damage)) == 2);
	REQUIRE(server.SharedNPCs().Get(npcs.front().npcId));
	CHECK(server.SharedNPCs().Get(npcs.front().npcId)->disabled);

	CoOpRelay::SharedNPCBoarding boarding = NPCBoarding(1);
	boarding.npcId = npcs.front().npcId;
	boarding.playerId = playerB;
	boarding.ownerId = npcs.front().ownerId;
	boarding.system = npcs.front().system;

	CoOpRelay::PlayerSnapshot bLanded = bSol;
	bLanded.sequence = 2;
	bLanded.landedPlanet = "Earth";
	REQUIRE_FALSE(server.Receive(bLanded).empty());
	CoOpRelay::SharedNPCBoarding landed = boarding;
	landed.sequence = 2;
	CHECK(server.Receive(landed).empty());

	CoOpRelay::PlayerSnapshot bInactive = bSol;
	bInactive.sequence = 3;
	bInactive.simulationActive = false;
	REQUIRE_FALSE(server.Receive(bInactive).empty());
	CoOpRelay::SharedNPCBoarding inactive = boarding;
	inactive.sequence = 3;
	CHECK(server.Receive(inactive).empty());

	CoOpRelay::PlayerSnapshot bActive = bSol;
	bActive.sequence = 4;
	REQUIRE_FALSE(server.Receive(bActive).empty());

	std::vector<CoOpRelay::RelayDelivery> boardingDeliveries = server.Receive(boarding);
	CHECK(NPCBoardingDeliveries(boardingDeliveries) == 1);
	CHECK(MissionEventDeliveries(boardingDeliveries) == 4);
	CHECK(ResourceEventDeliveries(boardingDeliveries) == 2);
	CHECK(NPCDeliveries(boardingDeliveries) == 2);
	CHECK(server.SharedNPCs().Get(npcs.front().npcId) == nullptr);

	const std::vector<CoOpRelay::SharedMissionEvent> missions = server.MissionEvents().LatestStates();
	CHECK(std::any_of(missions.begin(), missions.end(), [](const CoOpRelay::SharedMissionEvent &event) {
		return event.type == CoOpRelay::MissionEventType::NPC_BOARDED;
	}));
	CHECK(std::any_of(missions.begin(), missions.end(), [](const CoOpRelay::SharedMissionEvent &event) {
		return event.type == CoOpRelay::MissionEventType::NPC_CAPTURED;
	}));
	const std::vector<CoOpRelay::SharedResourceEvent> resources = server.ResourceEvents().All();
	CHECK(std::any_of(resources.begin(), resources.end(), [&playerA](const CoOpRelay::SharedResourceEvent &event) {
		return event.targetPlayerId == playerA && event.type == CoOpRelay::ResourceActionType::CREDIT_REWARD
			&& event.status == CoOpRelay::ResourceActionStatus::APPLIED && event.amount > 0.;
	}));
	CHECK(std::any_of(resources.begin(), resources.end(), [&playerB](const CoOpRelay::SharedResourceEvent &event) {
		return event.targetPlayerId == playerB && event.type == CoOpRelay::ResourceActionType::CREDIT_REWARD
			&& event.status == CoOpRelay::ResourceActionStatus::APPLIED && event.amount > 0.;
	}));
}



TEST_CASE( "Co-op local emitter is disabled by default", "[CoOpRelay]" )
{
	PlayerInfo player;
	CoOpRelay::LocalStateEmitter emitter;
	emitter.SetIdentity("local-player");

	CHECK_FALSE(emitter.IsEnabled());
	CHECK_FALSE(emitter.Step(player));
	CHECK_FALSE(emitter.LatestSnapshot());
	CHECK(emitter.TakeEvents().empty());
}



TEST_CASE( "Co-op relay server assigns peer ids and broadcasts snapshots", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");
	CoOpRelay::PlayerSnapshot snapshot = Snapshot();
	snapshot.playerId = playerA;

	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(snapshot);

	REQUIRE(deliveries.size() == 3);
	CHECK(SnapshotDeliveries(deliveries) == 1);
	CHECK(EventDeliveries(deliveries) == 0);
	CHECK(AuthorityDeliveries(deliveries) == 2);

	auto snapshotDelivery = std::find_if(deliveries.begin(), deliveries.end(),
		[](const CoOpRelay::RelayDelivery &delivery) { return delivery.snapshot.has_value(); });
	REQUIRE(snapshotDelivery != deliveries.end());
	CHECK(snapshotDelivery->recipientId == playerB);
	CHECK(snapshotDelivery->snapshot->playerId == playerA);

	const CoOpRelay::RelayDelivery *authorityA = FindAuthorityDelivery(deliveries, playerA, "Sol");
	REQUIRE(authorityA);
	CHECK(authorityA->authority->ownerId == playerA);
	CHECK(authorityA->authority->playerCount == 1);

	const CoOpRelay::RemotePresence *presence = server.Presence().Get(playerA);
	REQUIRE(presence);
	CHECK(presence->latest.system == "Sol");
}



TEST_CASE( "Co-op relay server sends cached presence to later peers", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	CoOpRelay::PlayerSnapshot snapshot = Snapshot();
	snapshot.playerId = playerA;
	REQUIRE(AuthorityDeliveries(server.Receive(snapshot)) == 1);

	const std::string playerB = server.Join("Player B");
	std::vector<CoOpRelay::PlayerSnapshot> cached = server.LatestSnapshotsFor(playerB);
	std::vector<CoOpRelay::SystemAuthority> cachedAuthorities = server.LatestAuthoritiesFor(playerB);

	REQUIRE(cached.size() == 1);
	CHECK(cached.front().playerId == playerA);
	CHECK(cached.front().system == "Sol");
	REQUIRE(cachedAuthorities.size() == 1);
	CHECK(cachedAuthorities.front().system == "Sol");
	CHECK(cachedAuthorities.front().ownerId == playerA);
	CHECK(server.LatestSnapshotsFor("unknown").empty());
	CHECK(server.LatestAuthoritiesFor("unknown").empty());
}



TEST_CASE( "Co-op relay server broadcasts and caches peer endpoint advertisements", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PeerEndpoint endpoint = PeerEndpoint(3);
	endpoint.playerId = playerA;
	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(endpoint);

	REQUIRE(PeerEndpointDeliveries(deliveries) == 1);
	CHECK(deliveries.front().recipientId == playerB);
	REQUIRE(deliveries.front().peerEndpoint);
	CHECK(deliveries.front().peerEndpoint->host == endpoint.host);

	std::vector<CoOpRelay::PeerEndpoint> cached = server.LatestPeerEndpointsFor(playerB);
	REQUIRE(cached.size() == 1);
	CHECK(cached.front().playerId == playerA);
	CHECK(server.LatestPeerEndpointsFor(playerA).empty());
	CHECK(server.LatestPeerEndpointsFor("unknown").empty());

	CoOpRelay::PeerEndpoint stale = endpoint;
	stale.sequence = 2;
	stale.host = "stale.example";
	CHECK(server.Receive(stale).empty());
	CHECK(server.LatestPeerEndpointsFor(playerB).front().host == endpoint.host);

	const std::string playerC = server.Join("Player C");
	cached = server.LatestPeerEndpointsFor(playerC);
	REQUIRE(cached.size() == 1);
	CHECK(cached.front().playerId == playerA);

	std::vector<CoOpRelay::RelayDelivery> leaveDeliveries = server.Leave(playerA);
	REQUIRE(PeerEndpointDeliveries(leaveDeliveries) == 2);
	CHECK(server.LatestPeerEndpointsFor(playerB).empty());
	for(const CoOpRelay::RelayDelivery &delivery : leaveDeliveries)
		if(delivery.peerEndpoint)
			CHECK(delivery.peerEndpoint->removed);
}



TEST_CASE( "Co-op relay server removes cached presence when peers leave", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot snapshot = Snapshot();
	snapshot.playerId = playerA;
	REQUIRE(server.Receive(snapshot).size() == 3);
	REQUIRE(server.Presence().Get(playerA));

	std::vector<CoOpRelay::RelayDelivery> leaveDeliveries = server.Leave(playerA);

	CHECK(server.PlayerCount() == 1);
	CHECK(server.Presence().Get(playerA) == nullptr);
	CHECK(server.LatestSnapshotsFor(playerB).empty());
	CHECK(server.LatestAuthoritiesFor(playerB).empty());
	REQUIRE(leaveDeliveries.size() == 1);
	REQUIRE(leaveDeliveries.front().authority);
	CHECK_FALSE(leaveDeliveries.front().authority->IsActive());
	CHECK(server.Receive(snapshot).empty());

	const std::string playerC = server.Join("Player C");
	CHECK(server.LatestSnapshotsFor(playerC).empty());
	CHECK(server.LatestAuthoritiesFor(playerC).empty());
}



TEST_CASE( "Co-op relay server broadcasts events without changing presence", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");
	CoOpRelay::PlayerEvent event;
	event.playerId = playerA;
	event.sequence = 4;
	event.type = CoOpRelay::EventType::LANDED;
	event.system = "Sol";
	event.planet = "Earth";

	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(event);

	REQUIRE(deliveries.size() == 1);
	CHECK(deliveries.front().recipientId == playerB);
	CHECK_FALSE(deliveries.front().snapshot);
	REQUIRE(deliveries.front().event);
	CHECK(deliveries.front().event->type == CoOpRelay::EventType::LANDED);
	CHECK(server.Presence().Get(playerA) == nullptr);
}



TEST_CASE( "Co-op relay server rejects unknown peers and stale snapshots", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	server.Join("Player B");

	CoOpRelay::PlayerSnapshot newest = Snapshot(5);
	newest.playerId = playerA;
	REQUIRE(server.Receive(newest).size() == 3);

	CoOpRelay::PlayerSnapshot stale = Snapshot(4);
	stale.playerId = playerA;
	stale.system = "Sirius";
	CHECK(server.Receive(stale).empty());
	CHECK(server.Presence().Get(playerA)->latest.system == "Sol");

	CoOpRelay::PlayerSnapshot unknown = Snapshot();
	unknown.playerId = "not-joined";
	CHECK(server.Receive(unknown).empty());
}



TEST_CASE( "Co-op relay server does not infer gameplay from snapshot changes", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot first = Snapshot(1);
	first.playerId = playerA;
	REQUIRE(server.Receive(first).size() == 3);

	CoOpRelay::PlayerSnapshot changed = Snapshot(2);
	changed.playerId = playerA;
	changed.system = "Alpha Centauri";
	changed.landedPlanet = "New Boston";
	changed.fuel = .2;

	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(changed);

	REQUIRE(deliveries.size() == 5);
	CHECK(SnapshotDeliveries(deliveries) == 1);
	CHECK(EventDeliveries(deliveries) == 0);
	CHECK(AuthorityDeliveries(deliveries) == 4);
	auto snapshotDelivery = std::find_if(deliveries.begin(), deliveries.end(),
		[](const CoOpRelay::RelayDelivery &delivery) { return delivery.snapshot.has_value(); });
	REQUIRE(snapshotDelivery != deliveries.end());
	CHECK(snapshotDelivery->recipientId == playerB);
	CHECK(snapshotDelivery->snapshot->system == "Alpha Centauri");
	CHECK(snapshotDelivery->snapshot->landedPlanet == "New Boston");
	CHECK(snapshotDelivery->snapshot->fuel == .2);

	const CoOpRelay::RelayDelivery *alphaAuthority = FindAuthorityDelivery(deliveries, playerA, "Alpha Centauri");
	REQUIRE(alphaAuthority);
	CHECK(alphaAuthority->authority->ownerId == playerA);

	const CoOpRelay::RemotePresence *presence = server.Presence().Get(playerA);
	REQUIRE(presence);
	CHECK(presence->latest.system == "Alpha Centauri");
	CHECK(presence->latest.IsLanded());
}



TEST_CASE( "Co-op relay tracks active system authority", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 3);

	const CoOpRelay::SystemAuthority *sol = server.AuthorityFor("Sol");
	REQUIRE(sol);
	CHECK(sol->ownerId == playerA);
	CHECK(sol->playerCount == 1);

	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 3);

	sol = server.AuthorityFor("Sol");
	REQUIRE(sol);
	CHECK(sol->ownerId == playerA);
	CHECK(sol->playerCount == 2);

	CoOpRelay::PlayerSnapshot aAlpha = Snapshot(2);
	aAlpha.playerId = playerA;
	aAlpha.system = "Alpha Centauri";
	std::vector<CoOpRelay::RelayDelivery> aAlphaDeliveries = server.Receive(aAlpha);
	REQUIRE(aAlphaDeliveries.size() == 5);
	REQUIRE(FindAuthorityDelivery(aAlphaDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(aAlphaDeliveries, playerB, "Sol")->authority->ownerId == playerB);

	sol = server.AuthorityFor("Sol");
	REQUIRE(sol);
	CHECK(sol->ownerId == playerB);
	CHECK(sol->playerCount == 1);

	const CoOpRelay::SystemAuthority *alpha = server.AuthorityFor("Alpha Centauri");
	REQUIRE(alpha);
	CHECK(alpha->ownerId == playerA);
	CHECK(alpha->playerCount == 1);

	CoOpRelay::PlayerSnapshot bSirius = Snapshot(2);
	bSirius.playerId = playerB;
	bSirius.system = "Sirius";
	std::vector<CoOpRelay::RelayDelivery> bSiriusDeliveries = server.Receive(bSirius);
	REQUIRE(bSiriusDeliveries.size() == 5);
	REQUIRE(FindAuthorityDelivery(bSiriusDeliveries, playerA, "Sol"));
	CHECK_FALSE(FindAuthorityDelivery(bSiriusDeliveries, playerA, "Sol")->authority->IsActive());

	CHECK(server.AuthorityFor("Sol") == nullptr);
	const CoOpRelay::SystemAuthority *sirius = server.AuthorityFor("Sirius");
	REQUIRE(sirius);
	CHECK(sirius->ownerId == playerB);
	CHECK(sirius->playerCount == 1);
}



TEST_CASE( "Co-op relay removes a disconnected player's remaining authorities", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 3);

	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 3);

	CoOpRelay::PlayerSnapshot aAlpha = Snapshot(2);
	aAlpha.playerId = playerA;
	aAlpha.system = "Alpha Centauri";
	REQUIRE(server.Receive(aAlpha).size() == 5);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerB);

	std::vector<CoOpRelay::RelayDelivery> leaveDeliveries = server.Leave(playerA);

	const CoOpRelay::SystemAuthority *sol = server.AuthorityFor("Sol");
	REQUIRE(sol);
	CHECK(sol->ownerId == playerB);
	CHECK(sol->playerCount == 1);
	CHECK(server.AuthorityFor("Alpha Centauri") == nullptr);
	REQUIRE(leaveDeliveries.size() == 1);
	REQUIRE(FindAuthorityDelivery(leaveDeliveries, playerB, "Alpha Centauri"));
	CHECK_FALSE(FindAuthorityDelivery(leaveDeliveries, playerB, "Alpha Centauri")->authority->IsActive());
}



TEST_CASE( "Co-op relay transfers off-system authority to an active in-system player", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 3);

	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 3);

	CoOpRelay::SharedNPCSnapshot firstNPC = NPCSnapshot(1);
	firstNPC.ownerId = playerA;
	REQUIRE(NPCDeliveries(server.Receive(firstNPC)) == 1);

	CoOpRelay::PlayerSnapshot aAlpha = Snapshot(2);
	aAlpha.playerId = playerA;
	aAlpha.system = "Alpha Centauri";
	std::vector<CoOpRelay::RelayDelivery> aAlphaDeliveries = server.Receive(aAlpha);
	REQUIRE(FindAuthorityDelivery(aAlphaDeliveries, playerB, "Sol"));
	CHECK(NPCDeliveries(aAlphaDeliveries) == 2);
	REQUIRE(FindNPCDelivery(aAlphaDeliveries, playerB, firstNPC.npcId));
	CHECK(FindNPCDelivery(aAlphaDeliveries, playerB, firstNPC.npcId)->npc->ownerId == playerB);

	const CoOpRelay::SystemAuthority *sol = server.AuthorityFor("Sol");
	REQUIRE(sol);
	CHECK(sol->ownerId == playerB);
	CHECK(sol->playerCount == 1);
	REQUIRE(server.SharedNPCs().Get(firstNPC.npcId));
	CHECK(server.SharedNPCs().Get(firstNPC.npcId)->ownerId == playerB);

	CoOpRelay::SharedNPCSnapshot offSystemNPC = firstNPC;
	offSystemNPC.sequence = 2;
	offSystemNPC.hull = .25;
	CHECK(server.Receive(offSystemNPC).empty());
	CHECK(server.SharedNPCs().Get(offSystemNPC.npcId)->hull == firstNPC.hull);

	CoOpRelay::SharedNPCSnapshot transferredNPC = offSystemNPC;
	transferredNPC.ownerId = playerB;
	std::vector<CoOpRelay::RelayDelivery> transferredDeliveries = server.Receive(transferredNPC);
	REQUIRE(NPCDeliveries(transferredDeliveries) == 1);
	CHECK(server.SharedNPCs().Get(transferredNPC.npcId)->ownerId == playerB);
	CHECK(server.SharedNPCs().Get(transferredNPC.npcId)->hull == .25);

	CoOpRelay::PlayerSnapshot bSirius = Snapshot(2);
	bSirius.playerId = playerB;
	bSirius.system = "Sirius";
	std::vector<CoOpRelay::RelayDelivery> bSiriusDeliveries = server.Receive(bSirius);
	REQUIRE(bSiriusDeliveries.size() == 5);
	REQUIRE(FindAuthorityDelivery(bSiriusDeliveries, playerA, "Sol"));
	CHECK_FALSE(FindAuthorityDelivery(bSiriusDeliveries, playerA, "Sol")->authority->IsActive());
	CHECK(server.AuthorityFor("Sol") == nullptr);
}



TEST_CASE( "Co-op relay transfers authority when the owner stops actively simulating", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 3);

	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 3);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 1);

	CoOpRelay::PlayerSnapshot aInactive = Snapshot(2);
	aInactive.playerId = playerA;
	aInactive.simulationActive = false;
	std::vector<CoOpRelay::RelayDelivery> inactiveDeliveries = server.Receive(aInactive);
	REQUIRE(FindAuthorityDelivery(inactiveDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(inactiveDeliveries, playerB, "Sol")->authority->ownerId == playerB);
	CHECK(NPCDeliveries(inactiveDeliveries) >= 1);
	REQUIRE(FindNPCDelivery(inactiveDeliveries, playerB, npc.npcId));
	CHECK(FindNPCDelivery(inactiveDeliveries, playerB, npc.npcId)->npc->ownerId == playerB);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerB);
	CHECK(server.AuthorityFor("Sol")->playerCount == 2);
	REQUIRE(server.SharedNPCs().Get(npc.npcId));
	CHECK(server.SharedNPCs().Get(npc.npcId)->ownerId == playerB);

	CoOpRelay::SharedNPCSnapshot staleOwnerNPC = NPCSnapshot(1);
	staleOwnerNPC.ownerId = playerA;
	CHECK(server.Receive(staleOwnerNPC).empty());

	CoOpRelay::SharedNPCSnapshot activeOwnerNPC = staleOwnerNPC;
	activeOwnerNPC.ownerId = playerB;
	activeOwnerNPC.sequence = 3;
	REQUIRE(NPCDeliveries(server.Receive(activeOwnerNPC)) == 1);
	CHECK(server.SharedNPCs().Get(activeOwnerNPC.npcId)->ownerId == playerB);
}



TEST_CASE( "Co-op relay transfers authority when the owner stops sending active heartbeats", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 3);

	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 3);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 1);

	bool transferred = false;
	std::vector<CoOpRelay::RelayDelivery> transferDeliveries;
	for(uint64_t sequence = 2; sequence < 80 && !transferred; ++sequence)
	{
		bSol.sequence = sequence;
		transferDeliveries = server.Receive(bSol);
		const CoOpRelay::RelayDelivery *authority = FindAuthorityDelivery(transferDeliveries, playerB, "Sol");
		transferred = authority && authority->authority->ownerId == playerB;
	}

	REQUIRE(transferred);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerB);
	REQUIRE(server.SharedNPCs().Get(npc.npcId));
	CHECK(server.SharedNPCs().Get(npc.npcId)->ownerId == playerB);
	CHECK(NPCDeliveries(transferDeliveries) >= 1);
	REQUIRE(FindNPCDelivery(transferDeliveries, playerB, npc.npcId));
	CHECK(FindNPCDelivery(transferDeliveries, playerB, npc.npcId)->npc->ownerId == playerB);

	CoOpRelay::SharedNPCSnapshot oldOwnerUpdate = npc;
	oldOwnerUpdate.sequence = 2;
	oldOwnerUpdate.hull = .1;
	CHECK(server.Receive(oldOwnerUpdate).empty());
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == npc.hull);
}



TEST_CASE( "Co-op relay allows landed background simulation when no one is flying", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aLanded = Snapshot(1);
	aLanded.playerId = playerA;
	aLanded.landedPlanet = "Earth";
	aLanded.simulationActive = true;
	REQUIRE(server.Receive(aLanded).size() == 3);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 1);

	CoOpRelay::PlayerSnapshot bLanded = Snapshot(1);
	bLanded.playerId = playerB;
	bLanded.landedPlanet = "Earth";
	bLanded.simulationActive = true;
	std::vector<CoOpRelay::RelayDelivery> bDeliveries = server.Receive(bLanded);
	REQUIRE(FindAuthorityDelivery(bDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(bDeliveries, playerB, "Sol")->authority->ownerId == playerA);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot update = npc;
	update.sequence = 2;
	update.hull = .3;
	REQUIRE(NPCDeliveries(server.Receive(update)) == 1);
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == .3);
}



TEST_CASE( "Co-op relay keeps landed authority stable when a flying player joins", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aLanded = Snapshot(1);
	aLanded.playerId = playerA;
	aLanded.landedPlanet = "Earth";
	aLanded.simulationActive = true;
	REQUIRE(server.Receive(aLanded).size() == 3);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 1);

	CoOpRelay::PlayerSnapshot bFlying = Snapshot(1);
	bFlying.playerId = playerB;
	std::vector<CoOpRelay::RelayDelivery> bDeliveries = server.Receive(bFlying);
	REQUIRE(FindAuthorityDelivery(bDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(bDeliveries, playerB, "Sol")->authority->ownerId == playerA);
	REQUIRE(FindNPCDelivery(bDeliveries, playerB, npc.npcId));
	CHECK(FindNPCDelivery(bDeliveries, playerB, npc.npcId)->npc->ownerId == playerA);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot landedOwnerUpdate = npc;
	landedOwnerUpdate.sequence = 2;
	landedOwnerUpdate.hull = .35;
	REQUIRE(NPCDeliveries(server.Receive(landedOwnerUpdate)) == 1);
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == .35);

	CoOpRelay::SharedNPCSnapshot nonOwnerUpdate = landedOwnerUpdate;
	nonOwnerUpdate.ownerId = playerB;
	nonOwnerUpdate.sequence = 3;
	nonOwnerUpdate.hull = .1;
	CHECK(server.Receive(nonOwnerUpdate).empty());
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == .35);
}



TEST_CASE( "Co-op relay keeps authority stable when all simulators land together", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aFlying = Snapshot(1);
	aFlying.playerId = playerA;
	REQUIRE(server.Receive(aFlying).size() == 3);

	CoOpRelay::PlayerSnapshot bFlying = Snapshot(1);
	bFlying.playerId = playerB;
	REQUIRE(server.Receive(bFlying).size() == 3);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 1);

	CoOpRelay::PlayerSnapshot bLanded = bFlying;
	bLanded.sequence = 2;
	bLanded.landedPlanet = "Earth";
	bLanded.simulationActive = true;
	std::vector<CoOpRelay::RelayDelivery> bDeliveries = server.Receive(bLanded);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);
	if(const CoOpRelay::RelayDelivery *authority = FindAuthorityDelivery(bDeliveries, playerB, "Sol"))
		CHECK(authority->authority->ownerId == playerA);

	CoOpRelay::PlayerSnapshot aLanded = aFlying;
	aLanded.sequence = 2;
	aLanded.landedPlanet = "Earth";
	aLanded.simulationActive = true;
	std::vector<CoOpRelay::RelayDelivery> aDeliveries = server.Receive(aLanded);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);
	if(const CoOpRelay::RelayDelivery *authority = FindAuthorityDelivery(aDeliveries, playerA, "Sol"))
		CHECK(authority->authority->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot backgroundUpdate = npc;
	backgroundUpdate.sequence = 2;
	backgroundUpdate.hull = .42;
	REQUIRE(NPCDeliveries(server.Receive(backgroundUpdate)) == 1);
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == .42);
}



TEST_CASE( "Co-op relay transfers from a landed owner only after simulation stops", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aLanded = Snapshot(1);
	aLanded.playerId = playerA;
	aLanded.landedPlanet = "Earth";
	aLanded.simulationActive = true;
	REQUIRE(server.Receive(aLanded).size() == 3);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 1);

	CoOpRelay::PlayerSnapshot bFlying = Snapshot(1);
	bFlying.playerId = playerB;
	std::vector<CoOpRelay::RelayDelivery> bFlyingDeliveries = server.Receive(bFlying);
	REQUIRE(FindAuthorityDelivery(bFlyingDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(bFlyingDeliveries, playerB, "Sol")->authority->ownerId == playerA);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerA);

	CoOpRelay::PlayerSnapshot aInactive = aLanded;
	aInactive.sequence = 2;
	aInactive.simulationActive = false;
	std::vector<CoOpRelay::RelayDelivery> inactiveDeliveries = server.Receive(aInactive);
	REQUIRE(FindAuthorityDelivery(inactiveDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(inactiveDeliveries, playerB, "Sol")->authority->ownerId == playerB);
	REQUIRE(FindNPCDelivery(inactiveDeliveries, playerB, npc.npcId));
	CHECK(FindNPCDelivery(inactiveDeliveries, playerB, npc.npcId)->npc->ownerId == playerB);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerB);

	CoOpRelay::PlayerSnapshot bLanded = bFlying;
	bLanded.sequence = 2;
	bLanded.landedPlanet = "Earth";
	bLanded.simulationActive = true;
	std::vector<CoOpRelay::RelayDelivery> bLandedDeliveries = server.Receive(bLanded);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerB);
	if(const CoOpRelay::RelayDelivery *authority = FindAuthorityDelivery(bLandedDeliveries, playerB, "Sol"))
		CHECK(authority->authority->ownerId == playerB);

	CoOpRelay::SharedNPCSnapshot newOwnerUpdate = npc;
	newOwnerUpdate.ownerId = playerB;
	newOwnerUpdate.sequence = 3;
	newOwnerUpdate.hull = .28;
	REQUIRE(NPCDeliveries(server.Receive(newOwnerUpdate)) == 1);
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == .28);

	CoOpRelay::SharedNPCSnapshot oldOwnerUpdate = npc;
	oldOwnerUpdate.sequence = 4;
	oldOwnerUpdate.hull = .1;
	CHECK(server.Receive(oldOwnerUpdate).empty());
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == .28);
}



TEST_CASE( "Co-op relay keeps a joined lobby playable after the original player leaves", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string originalHost = server.Join("Host");
	const std::string playerB = server.Join("Player B");
	const std::string playerC = server.Join("Player C");

	CoOpRelay::PlayerSnapshot hostSol = Snapshot(1);
	hostSol.playerId = originalHost;
	REQUIRE(AuthorityDeliveries(server.Receive(hostSol)) == 3);

	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	std::vector<CoOpRelay::RelayDelivery> bEnterDeliveries = server.Receive(bSol);
	REQUIRE(AuthorityDeliveries(bEnterDeliveries) == 3);
	REQUIRE(FindAuthorityDelivery(bEnterDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(bEnterDeliveries, playerB, "Sol")->authority->ownerId == originalHost);
	CHECK(FindAuthorityDelivery(bEnterDeliveries, playerB, "Sol")->authority->playerCount == 2);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = originalHost;
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 2);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == originalHost);

	std::vector<CoOpRelay::RelayDelivery> leaveDeliveries = server.Leave(originalHost);
	REQUIRE(server.AuthorityFor("Sol"));
	CHECK(server.AuthorityFor("Sol")->ownerId == playerB);
	REQUIRE(server.SharedNPCs().Get(npc.npcId));
	CHECK(server.SharedNPCs().Get(npc.npcId)->ownerId == playerB);
	REQUIRE(FindNPCDelivery(leaveDeliveries, playerB, npc.npcId));
	CHECK(FindNPCDelivery(leaveDeliveries, playerB, npc.npcId)->npc->ownerId == playerB);
	CHECK(FindAuthorityDelivery(leaveDeliveries, playerB, "Sol"));
	CHECK(FindAuthorityDelivery(leaveDeliveries, playerC, "Sol"));

	CoOpRelay::PlayerSnapshot cSol = Snapshot(1);
	cSol.playerId = playerC;
	std::vector<CoOpRelay::RelayDelivery> cDeliveries = server.Receive(cSol);
	CHECK(SnapshotDeliveries(cDeliveries) == 1);
	CHECK(NPCDeliveries(cDeliveries) == 1);

	CoOpRelay::SharedNPCSnapshot continuedNPC = npc;
	continuedNPC.sequence = 2;
	continuedNPC.ownerId = playerB;
	continuedNPC.hull = .25;
	std::vector<CoOpRelay::RelayDelivery> npcDeliveries = server.Receive(continuedNPC);
	REQUIRE(NPCDeliveries(npcDeliveries) == 1);
	CHECK(npcDeliveries.front().recipientId == playerC);
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == .25);
}



TEST_CASE( "Co-op relay routes shared NPC snapshots from system authority only", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 3);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	std::vector<CoOpRelay::RelayDelivery> npcDeliveries = server.Receive(npc);

	REQUIRE(npcDeliveries.size() == 1);
	CHECK(NPCDeliveries(npcDeliveries) == 1);
	CHECK(npcDeliveries.front().recipientId == playerB);
	REQUIRE(npcDeliveries.front().npc);
	CHECK(npcDeliveries.front().npc->npcId == npc.npcId);
	CHECK(server.SharedNPCs().Get(npc.npcId)->ownerId == playerA);

	CoOpRelay::SharedNPCSnapshot spoof = npc;
	spoof.sequence = 2;
	spoof.ownerId = playerB;
	spoof.hull = .1;
	CHECK(server.Receive(spoof).empty());
	CHECK(server.SharedNPCs().Get(npc.npcId)->hull == npc.hull);
}



TEST_CASE( "Co-op relay routes shared NPC damage reports to the NPC authority", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");
	const std::string playerC = server.Join("Player C");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 5);
	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 5);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(server.Receive(npc).size() == 2);

	CoOpRelay::SharedNPCDamage damage = NPCDamage(1);
	damage.ownerId = playerA;
	damage.system = npc.system;
	damage.reporterId = playerB;
	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(damage);
	REQUIRE(deliveries.size() == 1);
	CHECK(NPCDamageDeliveries(deliveries) == 1);
	CHECK(deliveries.front().recipientId == playerA);
	REQUIRE(deliveries.front().npcDamage);
	CHECK(deliveries.front().npcDamage->npcId == npc.npcId);
	CHECK(deliveries.front().npcDamage->reporterId == playerB);

	CoOpRelay::SharedNPCDamage ownerDamage = damage;
	ownerDamage.sequence = 2;
	ownerDamage.reporterId = playerA;
	CHECK(server.Receive(ownerDamage).empty());

	CoOpRelay::SharedNPCDamage wrongOwner = damage;
	wrongOwner.sequence = 3;
	wrongOwner.ownerId = playerB;
	CHECK(server.Receive(wrongOwner).empty());

	CoOpRelay::SharedNPCDamage wrongSystem = damage;
	wrongSystem.sequence = 4;
	wrongSystem.system = "Alpha Centauri";
	CHECK(server.Receive(wrongSystem).empty());

	CoOpRelay::SharedNPCDamage noPresence = damage;
	noPresence.sequence = 5;
	noPresence.reporterId = playerC;
	CHECK(server.Receive(noPresence).empty());

	CoOpRelay::PlayerSnapshot cAway = Snapshot(2);
	cAway.playerId = playerC;
	cAway.system = "Alpha Centauri";
	REQUIRE(server.Receive(cAway).size() == 5);
	CoOpRelay::SharedNPCDamage away = damage;
	away.sequence = 6;
	away.reporterId = playerC;
	CHECK(server.Receive(away).empty());

	CoOpRelay::SharedNPCDamage unknown = damage;
	unknown.sequence = 7;
	unknown.reporterId = playerC;
	unknown.npcId = "missing-npc";
	CHECK(server.Receive(unknown).empty());
}



TEST_CASE( "Co-op relay validates and routes friend PvP combat hits to the target player", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");
	const std::string playerC = server.Join("Player C");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 5);
	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 5);

	CoOpRelay::SharedCombatHit hit = CombatHit(1);
	hit.attackerId = playerA;
	hit.targetPlayerId = playerB;
	hit.system = "Sol";
	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(hit);
	REQUIRE(deliveries.size() == 1);
	CHECK(CombatHitDeliveries(deliveries) == 1);
	CHECK(deliveries.front().recipientId == playerB);
	REQUIRE(deliveries.front().combatHit);
	CHECK(deliveries.front().combatHit->attackerId == playerA);
	CHECK(deliveries.front().combatHit->targetPlayerId == playerB);

	CoOpRelay::SharedCombatHit selfHit = hit;
	selfHit.sequence = 2;
	selfHit.targetPlayerId = playerA;
	CHECK(server.Receive(selfHit).empty());

	CoOpRelay::SharedCombatHit unknownAttacker = hit;
	unknownAttacker.sequence = 3;
	unknownAttacker.attackerId = "not-joined";
	CHECK(server.Receive(unknownAttacker).empty());

	CoOpRelay::SharedCombatHit unknownTarget = hit;
	unknownTarget.sequence = 4;
	unknownTarget.targetPlayerId = "not-joined";
	CHECK(server.Receive(unknownTarget).empty());

	CoOpRelay::SharedCombatHit wrongSystem = hit;
	wrongSystem.sequence = 5;
	wrongSystem.system = "Alpha Centauri";
	CHECK(server.Receive(wrongSystem).empty());

	CoOpRelay::PlayerSnapshot cAway = Snapshot(1);
	cAway.playerId = playerC;
	cAway.system = "Alpha Centauri";
	REQUIRE(server.Receive(cAway).size() == 5);
	CoOpRelay::SharedCombatHit awayTarget = hit;
	awayTarget.sequence = 6;
	awayTarget.targetPlayerId = playerC;
	CHECK(server.Receive(awayTarget).empty());

	CoOpRelay::PlayerSnapshot bLanded = bSol;
	bLanded.sequence = 2;
	bLanded.landedPlanet = "Earth";
	REQUIRE_FALSE(server.Receive(bLanded).empty());
	CoOpRelay::SharedCombatHit landedTarget = hit;
	landedTarget.sequence = 7;
	CHECK(server.Receive(landedTarget).empty());

	CoOpRelay::PlayerSnapshot bInactive = bSol;
	bInactive.sequence = 3;
	bInactive.simulationActive = false;
	REQUIRE_FALSE(server.Receive(bInactive).empty());
	CoOpRelay::SharedCombatHit inactiveTarget = hit;
	inactiveTarget.sequence = 8;
	CHECK(server.Receive(inactiveTarget).empty());

	CoOpRelay::PlayerSnapshot bActive = bSol;
	bActive.sequence = 4;
	REQUIRE_FALSE(server.Receive(bActive).empty());
	CoOpRelay::SharedCombatHit noDamage = hit;
	noDamage.sequence = 9;
	noDamage.shieldDamage = 0.;
	noDamage.hullDamage = 0.;
	noDamage.fuelDamage = 0.;
	noDamage.energyDamage = 0.;
	noDamage.heatDamage = 0.;
	CHECK(server.Receive(noDamage).empty());
}



TEST_CASE( "Co-op relay routes weapon fire visuals to same-system flying players", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");
	const std::string playerC = server.Join("Player C");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 5);
	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 5);
	CoOpRelay::PlayerSnapshot cAway = Snapshot(1);
	cAway.playerId = playerC;
	cAway.system = "Alpha Centauri";
	REQUIRE(server.Receive(cAway).size() == 5);

	CoOpRelay::SharedWeaponFire fire = WeaponFire(1);
	fire.playerId = playerA;
	fire.system = "Sol";
	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(fire);
	REQUIRE(deliveries.size() == 1);
	CHECK(WeaponFireDeliveries(deliveries) == 1);
	CHECK(deliveries.front().recipientId == playerB);
	REQUIRE(deliveries.front().weaponFire);
	CHECK(deliveries.front().weaponFire->playerId == playerA);
	CHECK(deliveries.front().weaponFire->from == fire.from);
	CHECK(deliveries.front().weaponFire->to == fire.to);
	CHECK(deliveries.front().weaponFire->targetNPCId == fire.targetNPCId);

	CoOpRelay::SharedWeaponFire unknown = fire;
	unknown.sequence = 2;
	unknown.playerId = "not-joined";
	CHECK(server.Receive(unknown).empty());

	CoOpRelay::SharedWeaponFire wrongSystem = fire;
	wrongSystem.sequence = 3;
	wrongSystem.system = "Alpha Centauri";
	CHECK(server.Receive(wrongSystem).empty());

	CoOpRelay::PlayerSnapshot bLanded = bSol;
	bLanded.sequence = 2;
	bLanded.landedPlanet = "Earth";
	REQUIRE_FALSE(server.Receive(bLanded).empty());
	CoOpRelay::SharedWeaponFire fireWhileTargetLanded = fire;
	fireWhileTargetLanded.sequence = 4;
	CHECK(server.Receive(fireWhileTargetLanded).empty());

	CoOpRelay::PlayerSnapshot bActive = bSol;
	bActive.sequence = 3;
	REQUIRE_FALSE(server.Receive(bActive).empty());
	CoOpRelay::PlayerSnapshot aInactive = aSol;
	aInactive.sequence = 2;
	aInactive.simulationActive = false;
	REQUIRE_FALSE(server.Receive(aInactive).empty());
	CoOpRelay::SharedWeaponFire inactiveShooter = fire;
	inactiveShooter.sequence = 5;
	CHECK(server.Receive(inactiveShooter).empty());
}



TEST_CASE( "Co-op relay routes shared NPC boarding requests and authority results", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 3);
	CoOpRelay::PlayerSnapshot bSol = Snapshot(1);
	bSol.playerId = playerB;
	REQUIRE(server.Receive(bSol).size() == 3);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(server.Receive(npc).size() == 1);

	CoOpRelay::SharedNPCBoarding request = NPCBoarding(1);
	request.playerId = playerB;
	request.ownerId = playerA;
	request.system = npc.system;
	std::vector<CoOpRelay::RelayDelivery> requestDeliveries = server.Receive(request);
	REQUIRE(requestDeliveries.size() == 1);
	CHECK(NPCBoardingDeliveries(requestDeliveries) == 1);
	CHECK(requestDeliveries.front().recipientId == playerA);
	REQUIRE(requestDeliveries.front().npcBoarding);
	CHECK(requestDeliveries.front().npcBoarding->IsRequest());

	const std::string playerC = server.Join("Player C");
	CoOpRelay::SharedNPCBoarding noPresence = request;
	noPresence.sequence = 4;
	noPresence.playerId = playerC;
	CHECK(server.Receive(noPresence).empty());

	CoOpRelay::SharedNPCBoarding wrongOwner = request;
	wrongOwner.sequence = 2;
	wrongOwner.ownerId = playerB;
	CHECK(server.Receive(wrongOwner).empty());

	CoOpRelay::SharedNPCBoarding wrongSystem = request;
	wrongSystem.sequence = 3;
	wrongSystem.system = "Alpha Centauri";
	CHECK(server.Receive(wrongSystem).empty());

	CoOpRelay::PlayerSnapshot cAway = Snapshot(2);
	cAway.playerId = playerC;
	cAway.system = "Alpha Centauri";
	REQUIRE(server.Receive(cAway).size() == 5);
	CoOpRelay::SharedNPCBoarding away = request;
	away.sequence = 5;
	away.playerId = playerC;
	CHECK(server.Receive(away).empty());

	CoOpRelay::PlayerSnapshot bLanded = bSol;
	bLanded.sequence = 2;
	bLanded.landedPlanet = "Earth";
	REQUIRE_FALSE(server.Receive(bLanded).empty());
	CoOpRelay::SharedNPCBoarding landed = request;
	landed.sequence = 8;
	CHECK(server.Receive(landed).empty());

	CoOpRelay::PlayerSnapshot bInactive = bSol;
	bInactive.sequence = 3;
	bInactive.simulationActive = false;
	REQUIRE_FALSE(server.Receive(bInactive).empty());
	CoOpRelay::SharedNPCBoarding inactive = request;
	inactive.sequence = 9;
	CHECK(server.Receive(inactive).empty());

	CoOpRelay::PlayerSnapshot bActive = bSol;
	bActive.sequence = 4;
	REQUIRE_FALSE(server.Receive(bActive).empty());

	CoOpRelay::SharedNPCBoarding result = request;
	result.sequence = 6;
	result.ownerId = playerA;
	result.action = CoOpRelay::BoardingAction::CAPTURED;
	result.detail = "captured";
	std::vector<CoOpRelay::RelayDelivery> resultDeliveries = server.Receive(result);
	REQUIRE(resultDeliveries.size() == 1);
	CHECK(NPCBoardingDeliveries(resultDeliveries) == 1);
	CHECK(resultDeliveries.front().recipientId == playerB);
	REQUIRE(resultDeliveries.front().npcBoarding);
	CHECK(resultDeliveries.front().npcBoarding->action == CoOpRelay::BoardingAction::CAPTURED);

	CoOpRelay::SharedNPCBoarding spoof = result;
	spoof.sequence = 7;
	spoof.ownerId = playerB;
	CHECK(server.Receive(spoof).empty());
}



TEST_CASE( "Co-op relay broadcasts shared mission events from joined peers only", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");
	const std::string playerC = server.Join("Player C");

	CoOpRelay::SharedMissionEvent event = MissionEvent(1);
	event.playerId = playerA;
	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(event);
	REQUIRE(deliveries.size() == 2);
	CHECK(MissionEventDeliveries(deliveries) == 2);
	CHECK(std::none_of(deliveries.begin(), deliveries.end(),
		[&playerA](const CoOpRelay::RelayDelivery &delivery) { return delivery.recipientId == playerA; }));
	CHECK(std::any_of(deliveries.begin(), deliveries.end(),
		[&playerB](const CoOpRelay::RelayDelivery &delivery) { return delivery.recipientId == playerB; }));
	CHECK(std::any_of(deliveries.begin(), deliveries.end(),
		[&playerC](const CoOpRelay::RelayDelivery &delivery) { return delivery.recipientId == playerC; }));

	auto missionDelivery = std::find_if(deliveries.begin(), deliveries.end(),
		[](const CoOpRelay::RelayDelivery &delivery) { return delivery.missionEvent.has_value(); });
	REQUIRE(missionDelivery != deliveries.end());
	CHECK(missionDelivery->missionEvent->missionId == event.missionId);
	CHECK(missionDelivery->missionEvent->instanceId == event.instanceId);

	CoOpRelay::SharedMissionEvent unknown = event;
	unknown.sequence = 2;
	unknown.playerId = "not-joined";
	CHECK(server.Receive(unknown).empty());

	CoOpRelay::SharedMissionEvent hijack = event;
	hijack.sequence = 2;
	hijack.playerId = playerB;
	hijack.detail = "Hijacked shared mission objective.";
	CHECK(server.Receive(hijack).empty());

	CoOpRelay::SharedMissionEvent malformed = event;
	malformed.sequence = 3;
	malformed.missionId.clear();
	CHECK(server.Receive(malformed).empty());
	malformed = event;
	malformed.sequence = 3;
	malformed.credits = 1000000001;
	CHECK(server.Receive(malformed).empty());
	malformed = event;
	malformed.sequence = 3;
	malformed.type = CoOpRelay::MissionEventType::REWARD;
	malformed.system.clear();
	CHECK(server.Receive(malformed).empty());

	CoOpRelay::SharedMissionEvent npcEvent = event;
	npcEvent.sequence = 4;
	npcEvent.missionId = "shared-npc";
	npcEvent.instanceId = "npc-sol-pirate-1";
	npcEvent.npcId = "npc-sol-pirate-1";
	npcEvent.system = "Sol";
	npcEvent.type = CoOpRelay::MissionEventType::NPC_DISABLED;
	CHECK(server.Receive(npcEvent).empty());

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	aSol.system = "Sol";
	REQUIRE_FALSE(server.Receive(aSol).empty());
	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	npc.system = "Sol";
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 2);

	CoOpRelay::SharedMissionEvent spoof = npcEvent;
	spoof.sequence = 5;
	spoof.playerId = playerB;
	CHECK(server.Receive(spoof).empty());

	npcEvent.sequence = 6;
	deliveries = server.Receive(npcEvent);
	REQUIRE(deliveries.size() == 2);
	CHECK(MissionEventDeliveries(deliveries) == 2);
}



TEST_CASE( "Co-op relay routes shared resource events from joined peers only", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");
	const std::string playerC = server.Join("Player C");

	CoOpRelay::SharedResourceEvent event = ResourceEvent(1);
	event.playerId = playerA;
	event.targetPlayerId = playerB;
	std::vector<CoOpRelay::RelayDelivery> deliveries = server.Receive(event);
	REQUIRE(deliveries.size() == 1);
	CHECK(ResourceEventDeliveries(deliveries) == 1);
	CHECK(std::none_of(deliveries.begin(), deliveries.end(),
		[&playerA](const CoOpRelay::RelayDelivery &delivery) { return delivery.recipientId == playerA; }));
	CHECK(std::none_of(deliveries.begin(), deliveries.end(),
		[&playerC](const CoOpRelay::RelayDelivery &delivery) { return delivery.recipientId == playerC; }));
	CHECK(deliveries.front().recipientId == playerB);

	auto resourceDelivery = std::find_if(deliveries.begin(), deliveries.end(),
		[](const CoOpRelay::RelayDelivery &delivery) { return delivery.resourceEvent.has_value(); });
	REQUIRE(resourceDelivery != deliveries.end());
	CHECK(resourceDelivery->resourceEvent->actionId == event.actionId);
	CHECK(resourceDelivery->resourceEvent->targetPlayerId == playerB);
	CHECK(resourceDelivery->resourceEvent->type == CoOpRelay::ResourceActionType::FUEL_TRANSFER);

	CoOpRelay::SharedResourceEvent creditRequest = event;
	creditRequest.sequence = 18;
	creditRequest.type = CoOpRelay::ResourceActionType::CREDIT_REWARD;
	creditRequest.status = CoOpRelay::ResourceActionStatus::REQUEST;
	creditRequest.resource = "credits";
	CHECK(server.Receive(creditRequest).empty());

	CoOpRelay::SharedResourceEvent forgedCredit = ResourceEvent(8);
	forgedCredit.playerId = playerA;
	forgedCredit.targetPlayerId = playerB;
	forgedCredit.type = CoOpRelay::ResourceActionType::CREDIT_REWARD;
	forgedCredit.status = CoOpRelay::ResourceActionStatus::APPLIED;
	forgedCredit.resource = "credits";
	forgedCredit.actionId = "forged-credit:1";
	CHECK(server.Receive(forgedCredit).empty());

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	aSol.system = "Sol";
	REQUIRE_FALSE(server.Receive(aSol).empty());
	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	npc.system = "Sol";
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 2);

	CoOpRelay::SharedResourceEvent captureCredit = forgedCredit;
	captureCredit.sequence = 9;
	captureCredit.actionId = "capture-reward:" + npc.npcId + ":42:" + playerB;
	deliveries = server.Receive(captureCredit);
	REQUIRE(deliveries.size() == 1);
	REQUIRE(deliveries.front().resourceEvent);
	CHECK(deliveries.front().recipientId == playerB);
	CHECK(deliveries.front().resourceEvent->type == CoOpRelay::ResourceActionType::CREDIT_REWARD);

	CoOpRelay::SharedResourceEvent spoofedCaptureCredit = captureCredit;
	spoofedCaptureCredit.sequence = 10;
	spoofedCaptureCredit.playerId = playerB;
	spoofedCaptureCredit.targetPlayerId = playerA;
	CHECK(server.Receive(spoofedCaptureCredit).empty());

	CoOpRelay::SharedResourceEvent request = ResourceEvent(6);
	request.playerId = playerA;
	request.targetPlayerId = playerB;
	request.status = CoOpRelay::ResourceActionStatus::REQUEST;
	deliveries = server.Receive(request);
	REQUIRE(deliveries.size() == 1);
	CHECK(deliveries.front().recipientId == playerB);
	REQUIRE(deliveries.front().resourceEvent);
	CHECK(deliveries.front().resourceEvent->status == CoOpRelay::ResourceActionStatus::REQUEST);

	CoOpRelay::SharedResourceEvent directApplied = request;
	directApplied.sequence = 11;
	directApplied.status = CoOpRelay::ResourceActionStatus::APPLIED;
	CHECK(server.Receive(directApplied).empty());

	CoOpRelay::SharedResourceEvent spoofedConfirmed = request;
	spoofedConfirmed.sequence = 12;
	spoofedConfirmed.playerId = playerC;
	spoofedConfirmed.targetPlayerId = playerA;
	spoofedConfirmed.status = CoOpRelay::ResourceActionStatus::CONFIRMED;
	CHECK(server.Receive(spoofedConfirmed).empty());

	CoOpRelay::SharedResourceEvent wrongAmountConfirmed = request;
	wrongAmountConfirmed.sequence = 13;
	wrongAmountConfirmed.playerId = playerB;
	wrongAmountConfirmed.targetPlayerId = playerA;
	wrongAmountConfirmed.status = CoOpRelay::ResourceActionStatus::CONFIRMED;
	wrongAmountConfirmed.amount += 1.;
	CHECK(server.Receive(wrongAmountConfirmed).empty());

	CoOpRelay::SharedResourceEvent confirmed = request;
	confirmed.sequence = 7;
	confirmed.playerId = playerB;
	confirmed.targetPlayerId = playerA;
	confirmed.status = CoOpRelay::ResourceActionStatus::CONFIRMED;
	deliveries = server.Receive(confirmed);
	REQUIRE(deliveries.size() == 1);
	CHECK(deliveries.front().recipientId == playerA);
	REQUIRE(deliveries.front().resourceEvent);
	CHECK(deliveries.front().resourceEvent->status == CoOpRelay::ResourceActionStatus::CONFIRMED);

	CoOpRelay::SharedResourceEvent duplicateConfirmed = confirmed;
	duplicateConfirmed.sequence = 16;
	CHECK(server.Receive(duplicateConfirmed).empty());
	CoOpRelay::SharedResourceEvent conflictingRejected = confirmed;
	conflictingRejected.sequence = 17;
	conflictingRejected.status = CoOpRelay::ResourceActionStatus::REJECTED;
	conflictingRejected.amount = 0.;
	CHECK(server.Receive(conflictingRejected).empty());

	CoOpRelay::SharedResourceEvent rejected = request;
	rejected.sequence = 14;
	rejected.actionId = "resource-action-rejected";
	rejected.playerId = playerA;
	rejected.targetPlayerId = playerB;
	rejected.status = CoOpRelay::ResourceActionStatus::REQUEST;
	REQUIRE(ResourceEventDeliveries(server.Receive(rejected)) == 1);
	rejected.sequence = 15;
	rejected.playerId = playerB;
	rejected.targetPlayerId = playerA;
	rejected.status = CoOpRelay::ResourceActionStatus::REJECTED;
	rejected.amount = 0.;
	deliveries = server.Receive(rejected);
	REQUIRE(deliveries.size() == 1);
	REQUIRE(deliveries.front().resourceEvent);
	CHECK(deliveries.front().resourceEvent->status == CoOpRelay::ResourceActionStatus::REJECTED);

	CoOpRelay::SharedResourceEvent broadcast = ResourceEvent(2);
	broadcast.playerId = playerA;
	broadcast.targetPlayerId.clear();
	deliveries = server.Receive(broadcast);
	REQUIRE(deliveries.size() == 2);
	CHECK(ResourceEventDeliveries(deliveries) == 2);
	CHECK(std::any_of(deliveries.begin(), deliveries.end(),
		[&playerB](const CoOpRelay::RelayDelivery &delivery) { return delivery.recipientId == playerB; }));
	CHECK(std::any_of(deliveries.begin(), deliveries.end(),
		[&playerC](const CoOpRelay::RelayDelivery &delivery) { return delivery.recipientId == playerC; }));

	CoOpRelay::SharedResourceEvent unknown = event;
	unknown.sequence = 3;
	unknown.playerId = "not-joined";
	CHECK(server.Receive(unknown).empty());

	CoOpRelay::SharedResourceEvent unknownTarget = event;
	unknownTarget.sequence = 4;
	unknownTarget.targetPlayerId = "not-joined";
	CHECK(server.Receive(unknownTarget).empty());

	CoOpRelay::SharedResourceEvent malformed = event;
	malformed.sequence = 5;
	malformed.amount = 0.;
	CHECK(server.Receive(malformed).empty());
	malformed.amount = std::numeric_limits<double>::infinity();
	CHECK(server.Receive(malformed).empty());
	malformed.amount = 1000000001.;
	CHECK(server.Receive(malformed).empty());
	malformed.sequence = 0;
	malformed.amount = event.amount;
	CHECK(server.Receive(malformed).empty());
}



TEST_CASE( "Co-op relay caches shared mission state but only post-join resource events", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::SharedMissionEvent mission = MissionEvent(1);
	mission.playerId = playerA;
	REQUIRE(MissionEventDeliveries(server.Receive(mission)) == 1);
	CHECK(server.MissionEvents().All().size() == 1);
	CHECK(server.Receive(mission).empty());
	CHECK(server.MissionEvents().All().size() == 1);

	CoOpRelay::SharedMissionEvent completed = mission;
	completed.sequence = 2;
	completed.type = CoOpRelay::MissionEventType::COMPLETED;
	completed.detail = "Shared pirate hunt completed.";
	REQUIRE(MissionEventDeliveries(server.Receive(completed)) == 1);
	CHECK(server.MissionEvents().All().size() == 2);

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	aSol.system = "Sol";
	REQUIRE_FALSE(server.Receive(aSol).empty());

	CoOpRelay::SharedMissionEvent rewardSpoof = mission;
	rewardSpoof.sequence = 3;
	rewardSpoof.playerId = playerB;
	rewardSpoof.type = CoOpRelay::MissionEventType::REWARD;
	rewardSpoof.detail = "Forged shared mission reward.";
	rewardSpoof.credits = 9000;
	CHECK(server.Receive(rewardSpoof).empty());

	CoOpRelay::SharedMissionEvent rewardMission = mission;
	rewardMission.sequence = 4;
	rewardMission.type = CoOpRelay::MissionEventType::REWARD;
	rewardMission.detail = "Shared mission reward.";
	rewardMission.credits = 9000;
	std::vector<CoOpRelay::RelayDelivery> rewardDeliveries = server.Receive(rewardMission);
	CHECK(MissionEventDeliveries(rewardDeliveries) == 1);
	CHECK(ResourceEventDeliveries(rewardDeliveries) == 1);
	auto rewardDelivery = std::find_if(rewardDeliveries.begin(), rewardDeliveries.end(),
		[](const CoOpRelay::RelayDelivery &delivery) { return delivery.resourceEvent.has_value(); });
	REQUIRE(rewardDelivery != rewardDeliveries.end());
	REQUIRE(rewardDelivery->resourceEvent);
	CHECK(rewardDelivery->recipientId == playerB);
	CHECK(rewardDelivery->resourceEvent->playerId == playerA);
	CHECK(rewardDelivery->resourceEvent->targetPlayerId == playerB);
	CHECK(rewardDelivery->resourceEvent->type == CoOpRelay::ResourceActionType::CREDIT_REWARD);
	CHECK(rewardDelivery->resourceEvent->status == CoOpRelay::ResourceActionStatus::APPLIED);
	CHECK(rewardDelivery->resourceEvent->amount == 9000.);
	CHECK(rewardDelivery->resourceEvent->detail == rewardMission.detail);

	CoOpRelay::SharedResourceEvent resource = ResourceEvent(2);
	resource.playerId = playerA;
	resource.targetPlayerId.clear();
	REQUIRE(ResourceEventDeliveries(server.Receive(resource)) == 1);
	CHECK(server.ResourceEvents().All().size() == 2);
	CHECK(server.Receive(resource).empty());
	CHECK(server.ResourceEvents().All().size() == 2);

	CoOpRelay::SharedResourceEvent targeted = ResourceEvent(3);
	targeted.playerId = playerA;
	targeted.targetPlayerId = playerB;
	REQUIRE(ResourceEventDeliveries(server.Receive(targeted)) == 1);
	CHECK(server.ResourceEvents().All().size() == 3);
	std::vector<CoOpRelay::SharedResourceEvent> playerBResources = server.LatestResourceEventsFor(playerB);
	REQUIRE(playerBResources.size() == 3);
	CHECK(playerBResources.front().actionId == rewardDelivery->resourceEvent->actionId);
	CHECK(playerBResources[1].actionId == resource.actionId);
	CHECK(playerBResources.back().actionId == targeted.actionId);

	const std::string playerC = server.Join("Player C");
	std::vector<CoOpRelay::SharedMissionEvent> cachedMissions = server.LatestMissionEventsFor(playerC);
	std::vector<CoOpRelay::SharedResourceEvent> cachedResources = server.LatestResourceEventsFor(playerC);

	REQUIRE(cachedMissions.size() == 1);
	CHECK(cachedMissions.front().instanceId == mission.instanceId);
	CHECK(cachedMissions.front().type == CoOpRelay::MissionEventType::REWARD);
	CHECK(cachedMissions.front().detail == rewardMission.detail);
	CHECK(cachedResources.empty());

	CoOpRelay::SharedResourceEvent afterJoin = ResourceEvent(4);
	afterJoin.actionId = "resource-action-after-join";
	afterJoin.playerId = playerA;
	afterJoin.targetPlayerId.clear();
	REQUIRE(ResourceEventDeliveries(server.Receive(afterJoin)) == 2);
	cachedResources = server.LatestResourceEventsFor(playerC);
	REQUIRE(cachedResources.size() == 1);
	CHECK(cachedResources.front().actionId == afterJoin.actionId);
	CHECK(cachedResources.front().targetPlayerId.empty());
	CHECK(server.LatestMissionEventsFor("unknown").empty());
	CHECK(server.LatestResourceEventsFor("unknown").empty());
}



TEST_CASE( "Co-op relay replays shared NPC mission milestones separately", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	server.Join("Player B");

	CoOpRelay::SharedMissionEvent objective = MissionEvent(1);
	objective.playerId = playerA;
	REQUIRE(MissionEventDeliveries(server.Receive(objective)) == 1);

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	aSol.system = "Sol";
	REQUIRE_FALSE(server.Receive(aSol).empty());
	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	npc.system = "Sol";
	REQUIRE(NPCDeliveries(server.Receive(npc)) == 1);

	CoOpRelay::SharedMissionEvent completed = objective;
	completed.sequence = 2;
	completed.type = CoOpRelay::MissionEventType::COMPLETED;
	completed.detail = "Shared pirate hunt completed.";
	REQUIRE(MissionEventDeliveries(server.Receive(completed)) == 1);

	CoOpRelay::SharedMissionEvent staleObjective = objective;
	staleObjective.detail = "Stale shared pirate hunt objective.";
	CHECK(server.Receive(staleObjective).empty());

	CoOpRelay::SharedMissionEvent disabled = objective;
	disabled.sequence = 3;
	disabled.missionId = "shared-npc";
	disabled.instanceId = "npc-sol-pirate-1";
	disabled.type = CoOpRelay::MissionEventType::NPC_DISABLED;
	disabled.detail = "Shared pirate disabled.";
	REQUIRE(MissionEventDeliveries(server.Receive(disabled)) == 1);

	CoOpRelay::SharedMissionEvent disabledAgain = disabled;
	disabledAgain.sequence = 4;
	disabledAgain.detail = "Shared pirate disabled again.";
	REQUIRE(MissionEventDeliveries(server.Receive(disabledAgain)) == 1);

	CoOpRelay::SharedMissionEvent staleDisabled = disabled;
	staleDisabled.detail = "Stale shared pirate disabled.";
	CHECK(server.Receive(staleDisabled).empty());

	CoOpRelay::SharedMissionEvent boarded = disabled;
	boarded.sequence = 5;
	boarded.type = CoOpRelay::MissionEventType::NPC_BOARDED;
	boarded.detail = "Shared pirate boarded.";
	REQUIRE(MissionEventDeliveries(server.Receive(boarded)) == 1);

	CoOpRelay::SharedNPCSnapshot removed = npc;
	removed.sequence = 2;
	removed.removed = true;
	REQUIRE(NPCDeliveries(server.Receive(removed)) == 1);

	CoOpRelay::SharedMissionEvent captured = disabled;
	captured.sequence = 6;
	captured.type = CoOpRelay::MissionEventType::NPC_CAPTURED;
	captured.detail = "Shared pirate captured.";
	REQUIRE(MissionEventDeliveries(server.Receive(captured)) == 1);

	const std::string playerC = server.Join("Player C");
	const std::vector<CoOpRelay::SharedMissionEvent> cached = server.LatestMissionEventsFor(playerC);
	auto hasType = [&cached](CoOpRelay::MissionEventType type) {
		return any_of(cached.begin(), cached.end(), [type](const CoOpRelay::SharedMissionEvent &event) {
			return event.type == type;
		});
	};

	REQUIRE(cached.size() == 4);
	CHECK(hasType(CoOpRelay::MissionEventType::COMPLETED));
	CHECK(hasType(CoOpRelay::MissionEventType::NPC_DISABLED));
	CHECK(hasType(CoOpRelay::MissionEventType::NPC_BOARDED));
	CHECK(hasType(CoOpRelay::MissionEventType::NPC_CAPTURED));
	CHECK_FALSE(hasType(CoOpRelay::MissionEventType::OBJECTIVE_UPDATED));

	auto disabledIt = find_if(cached.begin(), cached.end(), [](const CoOpRelay::SharedMissionEvent &event) {
		return event.type == CoOpRelay::MissionEventType::NPC_DISABLED;
	});
	REQUIRE(disabledIt != cached.end());
	CHECK(disabledIt->detail == disabledAgain.detail);
}



TEST_CASE( "Co-op relay caches shared NPC snapshots for later peers and removes owner NPCs", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;
	const std::string playerA = server.Join("Player A");
	const std::string playerB = server.Join("Player B");

	CoOpRelay::PlayerSnapshot aSol = Snapshot(1);
	aSol.playerId = playerA;
	REQUIRE(server.Receive(aSol).size() == 3);

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = playerA;
	REQUIRE(server.Receive(npc).size() == 1);

	const std::string playerC = server.Join("Player C");
	std::vector<CoOpRelay::SharedNPCSnapshot> cachedNPCs = server.LatestNPCsFor(playerC);
	REQUIRE(cachedNPCs.size() == 1);
	CHECK(cachedNPCs.front().npcId == npc.npcId);
	CHECK(server.LatestNPCsFor("unknown").empty());

	CoOpRelay::PlayerSnapshot cSol = Snapshot(2);
	cSol.playerId = playerC;
	std::vector<CoOpRelay::RelayDelivery> enterDeliveries = server.Receive(cSol);
	CHECK(NPCDeliveries(enterDeliveries) == 1);
	const auto npcDelivery = std::find_if(enterDeliveries.begin(), enterDeliveries.end(),
		[&playerC](const CoOpRelay::RelayDelivery &delivery) {
			return delivery.recipientId == playerC && delivery.npc.has_value();
		});
	REQUIRE(npcDelivery != enterDeliveries.end());
	CHECK(npcDelivery->npc->npcId == npc.npcId);

	std::vector<CoOpRelay::RelayDelivery> leaveDeliveries = server.Leave(playerA);
	REQUIRE(server.SharedNPCs().Get(npc.npcId));
	CHECK(server.SharedNPCs().Get(npc.npcId)->ownerId == playerC);
	CHECK(NPCDeliveries(leaveDeliveries) == 2);
	REQUIRE(FindNPCDelivery(leaveDeliveries, playerC, npc.npcId));
	CHECK(FindNPCDelivery(leaveDeliveries, playerC, npc.npcId)->npc->ownerId == playerC);
	for(const CoOpRelay::RelayDelivery &delivery : leaveDeliveries)
		if(delivery.npc)
		{
			CHECK(delivery.npc->ownerId == playerC);
			CHECK_FALSE(delivery.npc->removed);
		}

	std::vector<CoOpRelay::RelayDelivery> closeDeliveries = server.Leave(playerC);
	CHECK(server.SharedNPCs().All().empty());
	CHECK(NPCDeliveries(closeDeliveries) == 1);
	for(const CoOpRelay::RelayDelivery &delivery : closeDeliveries)
		if(delivery.npc)
			CHECK(delivery.npc->removed);
}



TEST_CASE( "Co-op client session joins and enables local emission only after welcome", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;

	session.StartJoin("Player\tA");
	CHECK(session.State() == CoOpRelay::ConnectionState::CONNECTING);
	CHECK(session.PlayerName() == "Player\tA");
	CHECK_FALSE(session.Emitter().IsEnabled());
	CHECK(CoOpRelay::ClientSession::JoinLine("Player\tA") == "join\tPlayer%09A");
	CHECK(CoOpRelay::ParseJoinName("join\tPlayer%09A") == "Player\tA");
	CHECK(CoOpRelay::ClientSession::JoinLine("Player\tA", "pass word") == "join\tPlayer%09A\tpass word");
	auto request = CoOpRelay::ParseJoinRequest("join\tPlayer%09A\tpass word");
	REQUIRE(request);
	CHECK(request->playerName == "Player\tA");
	CHECK(request->password == "pass word");
	CHECK_FALSE(CoOpRelay::ParseJoinName("join\t"));

	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-7"));
	CHECK(session.State() == CoOpRelay::ConnectionState::CONNECTED);
	CHECK(session.PlayerId() == "player-7");
	CHECK(session.Emitter().IsEnabled());
	CHECK(session.StatusText() == "Connected as player-7");
}



TEST_CASE( "Co-op relay server enforces optional room passwords", "[CoOpRelay]" )
{
	CoOpRelay::RelayServerCore server;

	CoOpRelay::JoinRequest openRequest;
	openRequest.playerName = "Player A";
	CoOpRelay::JoinResult open = server.TryJoin(openRequest);
	REQUIRE(open.accepted);
	CHECK(open.playerId == "player-1");

	server.SetRoomPassword("secret");

	CoOpRelay::JoinRequest rejected;
	rejected.playerName = "Player B";
	rejected.password = "wrong";
	CoOpRelay::JoinResult denied = server.TryJoin(rejected);
	CHECK_FALSE(denied.accepted);
	CHECK(denied.message == "Room password rejected");
	CHECK(server.PlayerCount() == 1);

	rejected.password = "secret";
	CoOpRelay::JoinResult accepted = server.TryJoin(rejected);
	REQUIRE(accepted.accepted);
	CHECK(accepted.playerId == "player-2");
	CHECK(server.PlayerCount() == 2);
}



TEST_CASE( "Co-op client session reports rejected joins", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A", "wrong");

	CHECK(session.Password() == "wrong");
	CHECK_FALSE(session.AcceptWelcome("reject\t3\tRoom password rejected"));
	CHECK(session.State() == CoOpRelay::ConnectionState::ERROR);
	CHECK(session.StatusText() == "Connection error: Room password rejected");
	CHECK_FALSE(session.Emitter().IsEnabled());
}



TEST_CASE( "Co-op client session receives remote presence but ignores self echo", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-a"));

	CoOpRelay::PlayerSnapshot self = Snapshot();
	self.playerId = "player-a";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(self)));
	CHECK(session.Remotes().All().empty());

	CoOpRelay::PlayerSnapshot remote = Snapshot();
	remote.playerId = "player-b";
	remote.name = "Player B";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(remote)));

	const CoOpRelay::RemotePresence *presence = session.Remotes().Get("player-b");
	REQUIRE(presence);
	CHECK(presence->latest.name == "Player B");
	CHECK(presence->latest.system == "Sol");
	CHECK(session.RecentEvents().empty());
}



TEST_CASE( "Co-op client session stores system authority updates", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-a"));

	CoOpRelay::SystemAuthority sol;
	sol.sequence = 1;
	sol.system = "Sol";
	sol.ownerId = "player-a";
	sol.playerCount = 1;

	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(sol)));
	REQUIRE(session.Authorities().Get("Sol"));
	CHECK(session.Authorities().Get("Sol")->ownerId == "player-a");
	CHECK(session.IsSystemAuthority("Sol"));

	CoOpRelay::SystemAuthority stale = sol;
	stale.sequence = 0;
	stale.ownerId = "player-b";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(stale)));
	CHECK(session.Authorities().Get("Sol")->ownerId == "player-a");

	CoOpRelay::SystemAuthority transferred = sol;
	transferred.sequence = 2;
	transferred.ownerId = "player-b";
	transferred.playerCount = 2;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(transferred)));
	CHECK_FALSE(session.IsSystemAuthority("Sol"));
	CHECK(session.Authorities().Get("Sol")->ownerId == "player-b");

	CoOpRelay::SystemAuthority closed = transferred;
	closed.sequence = 3;
	closed.ownerId.clear();
	closed.playerCount = 0;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(closed)));
	CHECK(session.Authorities().Get("Sol") == nullptr);
}



TEST_CASE( "Co-op client session tracks relayed peer endpoints", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player B");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-b"));

	CoOpRelay::PeerEndpoint endpoint = PeerEndpoint(2);
	endpoint.playerId = "player-a";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(endpoint)));
	REQUIRE(session.PeerEndpoints().Get("player-a"));
	CHECK(session.PeerEndpoints().Get("player-a")->host == endpoint.host);

	CoOpRelay::PeerEndpoint stale = endpoint;
	stale.sequence = 1;
	stale.host = "stale.example";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(stale)));
	REQUIRE(session.PeerEndpoints().Get("player-a"));
	CHECK(session.PeerEndpoints().Get("player-a")->host == endpoint.host);
	REQUIRE(session.DesyncWarnings().size() == 1);

	CoOpRelay::PeerEndpoint self = endpoint;
	self.sequence = 3;
	self.playerId = "player-b";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(self)));

	CoOpRelay::PeerEndpoint removed = endpoint;
	removed.sequence = 4;
	removed.removed = true;
	removed.host.clear();
	removed.port = 0;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(removed)));
	CHECK(session.PeerEndpoints().Get("player-a") == nullptr);
}



TEST_CASE( "Co-op client session deduplicates remote player events", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player B");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-b"));

	CoOpRelay::PlayerEvent jumped;
	jumped.playerId = "player-a";
	jumped.sequence = 2;
	jumped.type = CoOpRelay::EventType::JUMPED;
	jumped.system = "Alpha Centauri";
	jumped.detail = "Sol";

	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(jumped)));
	CHECK(session.RecentEvents().size() == 1);
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(jumped)));
	CHECK(session.RecentEvents().size() == 1);

	CoOpRelay::PlayerEvent stale = jumped;
	stale.sequence = 1;
	stale.type = CoOpRelay::EventType::LANDED;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(stale)));
	CHECK(session.RecentEvents().size() == 1);

	CoOpRelay::PlayerEvent landed = jumped;
	landed.sequence = 3;
	landed.type = CoOpRelay::EventType::LANDED;
	landed.system = "Sol";
	landed.planet = "Earth";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(landed)));
	REQUIRE(session.RecentEvents().size() == 2);
	CHECK(session.RecentEvents().back().type == CoOpRelay::EventType::LANDED);
}



TEST_CASE( "Co-op client session stores shared NPC updates and removals", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player B");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-b"));

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = "player-a";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(npc)));
	REQUIRE(session.SharedNPCs().Get(npc.npcId));
	CHECK(session.SharedNPCs().InSystem("Sol").size() == 1);

	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(npc)));
	CHECK(session.SharedNPCs().Get(npc.npcId)->hull == npc.hull);
	CHECK(session.DesyncWarnings().empty());

	CoOpRelay::SharedNPCSnapshot stale = npc;
	stale.sequence = 0;
	stale.hull = .1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(stale)));
	CHECK(session.SharedNPCs().Get(npc.npcId)->hull == npc.hull);
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("shared NPC") != std::string::npos);

	CoOpRelay::SharedNPCSnapshot removed = npc;
	removed.sequence = 2;
	removed.removed = true;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(removed)));
	CHECK(session.SharedNPCs().Get(npc.npcId) == nullptr);
}



TEST_CASE( "Co-op client session records desync warnings for stale shared state", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player B");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-b"));

	CoOpRelay::PlayerSnapshot remote = Snapshot(2);
	remote.playerId = "player-a";
	remote.name = "Player A";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(remote)));
	CoOpRelay::PlayerSnapshot staleRemote = remote;
	staleRemote.sequence = 1;
	staleRemote.system = "Sirius";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(staleRemote)));
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("presence") != std::string::npos);
	CHECK(session.Remotes().Get("player-a")->latest.system == "Sol");

	CoOpRelay::SystemAuthority authority;
	authority.sequence = 2;
	authority.system = "Sol";
	authority.ownerId = "player-a";
	authority.playerCount = 2;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(authority)));
	CoOpRelay::SystemAuthority staleAuthority = authority;
	staleAuthority.sequence = 1;
	staleAuthority.ownerId = "player-b";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(staleAuthority)));
	REQUIRE(session.DesyncWarnings().size() == 2);
	CHECK(session.DesyncWarnings().back().find("authority") != std::string::npos);
	CHECK(session.Authorities().Get("Sol")->ownerId == "player-a");

	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(2);
	npc.ownerId = "player-a";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(npc)));
	CoOpRelay::SharedNPCSnapshot staleNPC = npc;
	staleNPC.sequence = 1;
	staleNPC.hull = .1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(staleNPC)));
	REQUIRE(session.DesyncWarnings().size() == 3);
	CHECK(session.DesyncWarnings().back().find("shared NPC") != std::string::npos);
	CHECK(session.SharedNPCs().Get(npc.npcId)->hull == npc.hull);

	CHECK(session.StatusText() == "Connected as player-b - 3 desync warnings");
	session.Disconnect();
	CHECK(session.DesyncWarnings().empty());
}



TEST_CASE( "Co-op client session warns when remote presence stops updating", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player B");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-b"));

	CoOpRelay::PlayerSnapshot remote = Snapshot(2);
	remote.playerId = "player-a";
	remote.name = "Player A";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(remote)));
	CHECK(session.DesyncWarnings().empty());

	session.StepConnectionHealth(601);
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("No recent presence snapshot") != std::string::npos);
	CHECK(session.DesyncWarnings().back().find("Player A") != std::string::npos);

	session.StepConnectionHealth(601);
	REQUIRE(session.DesyncWarnings().size() == 1);

	remote.sequence = 3;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(remote)));
	session.StepConnectionHealth(601);
	REQUIRE(session.DesyncWarnings().size() == 2);
	CHECK(session.DesyncWarnings().back().find("Player A") != std::string::npos);

	session.SetError("lost relay");
	CHECK(session.DesyncWarnings().empty());
}



TEST_CASE( "Co-op client session stores shared NPC damage reports until consumed", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-a"));

	CoOpRelay::SharedNPCDamage damage = NPCDamage(1);
	damage.reporterId = "player-b";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(damage)));

	std::vector<CoOpRelay::SharedNPCDamage> reports = session.TakeNPCDamageReports();
	REQUIRE(reports.size() == 1);
	CHECK(reports.front().npcId == damage.npcId);
	CHECK(reports.front().reporterId == damage.reporterId);
	CHECK(session.TakeNPCDamageReports().empty());

	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(damage)));
	CHECK(session.TakeNPCDamageReports().empty());
	CHECK(session.DesyncWarnings().empty());

	damage.sequence = 2;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(damage)));
	reports = session.TakeNPCDamageReports();
	REQUIRE(reports.size() == 1);
	CHECK(reports.front().sequence == 2);

	damage.sequence = 1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(damage)));
	CHECK(session.TakeNPCDamageReports().empty());
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("damage report") != std::string::npos);

	damage.sequence = 3;
	damage.reporterId = "player-a";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(damage)));
	CHECK(session.TakeNPCDamageReports().empty());
}



TEST_CASE( "Co-op client session stores combat hits targeted to the local player", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-a"));

	CoOpRelay::SharedCombatHit hit = CombatHit(1);
	hit.targetPlayerId = "player-a";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(hit)));

	std::vector<CoOpRelay::SharedCombatHit> hits = session.TakeCombatHits();
	REQUIRE(hits.size() == 1);
	CHECK(hits.front().attackerId == hit.attackerId);
	CHECK(hits.front().targetPlayerId == hit.targetPlayerId);
	CHECK(session.TakeCombatHits().empty());

	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(hit)));
	CHECK(session.TakeCombatHits().empty());
	CHECK(session.DesyncWarnings().empty());

	hit.sequence = 2;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(hit)));
	hits = session.TakeCombatHits();
	REQUIRE(hits.size() == 1);
	CHECK(hits.front().sequence == 2);

	hit.sequence = 1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(hit)));
	CHECK(session.TakeCombatHits().empty());
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("combat hit") != std::string::npos);

	hit.sequence = 3;
	hit.targetPlayerId = "player-b";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(hit)));
	CHECK(session.TakeCombatHits().empty());
}



TEST_CASE( "Co-op client session stores remote weapon fire visuals until consumed", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-a"));

	CoOpRelay::SharedWeaponFire fire = WeaponFire(1);
	fire.playerId = "player-b";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(fire)));

	std::vector<CoOpRelay::SharedWeaponFire> fires = session.TakeWeaponFires();
	REQUIRE(fires.size() == 1);
	CHECK(fires.front().playerId == fire.playerId);
	CHECK(fires.front().from == fire.from);
	CHECK(fires.front().to == fire.to);
	CHECK(fires.front().targetNPCId == fire.targetNPCId);
	CHECK(session.TakeWeaponFires().empty());

	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(fire)));
	CHECK(session.TakeWeaponFires().empty());
	CHECK(session.DesyncWarnings().empty());

	fire.sequence = 2;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(fire)));
	fires = session.TakeWeaponFires();
	REQUIRE(fires.size() == 1);
	CHECK(fires.front().sequence == 2);

	fire.sequence = 1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(fire)));
	CHECK(session.TakeWeaponFires().empty());
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("weapon fire") != std::string::npos);

	fire.sequence = 3;
	fire.playerId = "player-a";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(fire)));
	CHECK(session.TakeWeaponFires().empty());
}



TEST_CASE( "Co-op client session stores shared NPC boarding reports until consumed", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-a"));

	CoOpRelay::SharedNPCBoarding request = NPCBoarding(1);
	request.playerId = "player-b";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(request)));

	std::vector<CoOpRelay::SharedNPCBoarding> reports = session.TakeNPCBoardingReports();
	REQUIRE(reports.size() == 1);
	CHECK(reports.front().npcId == request.npcId);
	CHECK(reports.front().playerId == request.playerId);
	CHECK(reports.front().IsRequest());
	CHECK(session.TakeNPCBoardingReports().empty());

	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(request)));
	CHECK(session.TakeNPCBoardingReports().empty());
	CHECK(session.DesyncWarnings().empty());

	request.sequence = 2;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(request)));
	reports = session.TakeNPCBoardingReports();
	REQUIRE(reports.size() == 1);
	CHECK(reports.front().sequence == 2);

	request.sequence = 1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(request)));
	CHECK(session.TakeNPCBoardingReports().empty());
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("boarding report") != std::string::npos);

	CoOpRelay::SharedNPCBoarding result = NPCBoarding(1);
	result.playerId = "player-a";
	result.ownerId = "player-b";
	result.action = CoOpRelay::BoardingAction::CAPTURED;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(result)));
	reports = session.TakeNPCBoardingReports();
	REQUIRE(reports.size() == 1);
	CHECK_FALSE(reports.front().IsRequest());

	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(result)));
	CHECK(session.TakeNPCBoardingReports().empty());
	REQUIRE(session.DesyncWarnings().size() == 1);

	result.sequence = 2;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(result)));
	reports = session.TakeNPCBoardingReports();
	REQUIRE(reports.size() == 1);
	CHECK(reports.front().sequence == 2);

	result.sequence = 1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(result)));
	CHECK(session.TakeNPCBoardingReports().empty());
	REQUIRE(session.DesyncWarnings().size() == 2);
	CHECK(session.DesyncWarnings().back().find("boarding report") != std::string::npos);

	request.sequence = 3;
	request.playerId = "player-a";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(request)));
	CHECK(session.TakeNPCBoardingReports().empty());
}



TEST_CASE( "Co-op client session stores shared mission events until consumed", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player B");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-b"));

	CoOpRelay::SharedMissionEvent event = MissionEvent(1);
	event.playerId = "player-a";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(event)));
	REQUIRE(session.MissionEvents().All().size() == 1);

	std::vector<CoOpRelay::SharedMissionEvent> events = session.TakeMissionEvents();
	REQUIRE(events.size() == 1);
	CHECK(events.front().missionId == event.missionId);
	CHECK(events.front().instanceId == event.instanceId);
	CHECK(events.front().type == CoOpRelay::MissionEventType::OBJECTIVE_UPDATED);
	CHECK(events.front().credits == event.credits);
	CHECK(session.TakeMissionEvents().empty());
	REQUIRE(session.MissionEvents().All().size() == 1);

	event.sequence = 2;
	event.playerId = "player-b";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(event)));
	CHECK(session.TakeMissionEvents().empty());
	REQUIRE(session.MissionEvents().All().size() == 1);

	event.playerId = "player-a";
	event.sequence = 1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(event)));
	CHECK(session.TakeMissionEvents().empty());
	REQUIRE(session.MissionEvents().All().size() == 1);
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("mission event") != std::string::npos);
	CHECK(session.DesyncWarnings().back().find(event.instanceId) != std::string::npos);
}



TEST_CASE( "Co-op client session stores shared resource events until consumed", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player B");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-b"));

	CoOpRelay::SharedResourceEvent event = ResourceEvent(1);
	event.playerId = "player-a";
	event.targetPlayerId = "player-b";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(event)));
	REQUIRE(session.ResourceEvents().All().size() == 1);

	std::vector<CoOpRelay::SharedResourceEvent> events = session.TakeResourceEvents();
	REQUIRE(events.size() == 1);
	CHECK(events.front().actionId == event.actionId);
	CHECK(events.front().targetPlayerId == event.targetPlayerId);
	CHECK(events.front().type == CoOpRelay::ResourceActionType::FUEL_TRANSFER);
	CHECK(events.front().status == CoOpRelay::ResourceActionStatus::REQUEST);
	CHECK(events.front().amount == event.amount);
	CHECK(session.TakeResourceEvents().empty());
	REQUIRE(session.ResourceEvents().All().size() == 1);

	event.sequence = 2;
	event.playerId = "player-b";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(event)));
	CHECK(session.TakeResourceEvents().empty());
	REQUIRE(session.ResourceEvents().All().size() == 1);

	event.playerId = "player-a";
	event.sequence = 1;
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(event)));
	CHECK(session.TakeResourceEvents().empty());
	REQUIRE(session.ResourceEvents().All().size() == 1);
	REQUIRE(session.DesyncWarnings().size() == 1);
	CHECK(session.DesyncWarnings().back().find("resource event") != std::string::npos);
	CHECK(session.DesyncWarnings().back().find(event.actionId) != std::string::npos);

	event.sequence = 3;
	event.targetPlayerId = "player-c";
	CHECK_FALSE(session.ReceiveRelayLine(CoOpRelay::Serialize(event)));
	CHECK(session.TakeResourceEvents().empty());
	REQUIRE(session.ResourceEvents().All().size() == 1);
	REQUIRE(session.DesyncWarnings().size() == 1);
}



TEST_CASE( "Co-op client records remote events without mutating presence", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-a"));

	CoOpRelay::PlayerSnapshot remote = Snapshot(1);
	remote.playerId = "player-b";
	remote.name = "Player B";
	remote.landedPlanet.clear();
	remote.fuel = .4;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(remote)));

	CoOpRelay::PlayerEvent landed;
	landed.playerId = "player-b";
	landed.sequence = 2;
	landed.type = CoOpRelay::EventType::LANDED;
	landed.system = "Sol";
	landed.planet = "Earth";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(landed)));

	const CoOpRelay::RemotePresence *presence = session.Remotes().Get("player-b");
	REQUIRE(presence);
	CHECK_FALSE(presence->latest.IsLanded());
	REQUIRE(session.RecentEvents().size() == 1);
	CHECK(session.RecentEvents().front().type == CoOpRelay::EventType::LANDED);

	CoOpRelay::PlayerEvent resources;
	resources.playerId = "player-b";
	resources.sequence = 3;
	resources.type = CoOpRelay::EventType::RESOURCES_CHANGED;
	resources.system = "Sol";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(resources)));
	CHECK(session.Remotes().Get("player-b")->latest.fuel == .4);
	REQUIRE(session.RecentEvents().size() == 2);

	CoOpRelay::PlayerSnapshot authoritative = remote;
	authoritative.sequence = 4;
	authoritative.landedPlanet = "Earth";
	authoritative.fuel = 1.;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(authoritative)));

	presence = session.Remotes().Get("player-b");
	REQUIRE(presence);
	CHECK(presence->latest.IsLanded());
	CHECK(presence->latest.fuel == 1.);
}



TEST_CASE( "Co-op client session clears stale presence after errors and rejoins", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");
	REQUIRE(session.AcceptWelcome("welcome\t3\tplayer-a"));

	CoOpRelay::PlayerSnapshot remote = Snapshot();
	remote.playerId = "player-b";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(remote)));

	CoOpRelay::PlayerEvent event;
	event.playerId = "player-b";
	event.sequence = 2;
	event.type = CoOpRelay::EventType::JUMPED;
	event.system = "Alpha Centauri";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(event)));
	CoOpRelay::SystemAuthority authority;
	authority.sequence = 1;
	authority.system = "Sol";
	authority.ownerId = "player-b";
	authority.playerCount = 1;
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(authority)));
	CoOpRelay::SharedNPCSnapshot npc = NPCSnapshot(1);
	npc.ownerId = "player-b";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(npc)));
	CoOpRelay::SharedNPCBoarding boarding = NPCBoarding(1);
	boarding.playerId = "player-b";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(boarding)));
	CoOpRelay::SharedMissionEvent mission = MissionEvent(1);
	mission.playerId = "player-b";
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(mission)));
	CoOpRelay::SharedResourceEvent resource = ResourceEvent(1);
	resource.playerId = "player-b";
	resource.targetPlayerId.clear();
	REQUIRE(session.ReceiveRelayLine(CoOpRelay::Serialize(resource)));
	REQUIRE_FALSE(session.Remotes().All().empty());
	REQUIRE_FALSE(session.RecentEvents().empty());
	REQUIRE_FALSE(session.Authorities().All().empty());
	REQUIRE_FALSE(session.SharedNPCs().All().empty());
	REQUIRE_FALSE(session.TakeNPCBoardingReports().empty());
	REQUIRE_FALSE(session.TakeMissionEvents().empty());
	REQUIRE_FALSE(session.TakeResourceEvents().empty());
	REQUIRE_FALSE(session.MissionEvents().All().empty());
	REQUIRE_FALSE(session.ResourceEvents().All().empty());

	session.SetError();

	CHECK(session.State() == CoOpRelay::ConnectionState::ERROR);
	CHECK(session.PlayerId().empty());
	CHECK_FALSE(session.Emitter().IsEnabled());
	CHECK(session.Remotes().All().empty());
	CHECK(session.Authorities().All().empty());
	CHECK(session.SharedNPCs().All().empty());
	CHECK(session.RecentEvents().empty());
	CHECK(session.TakeMissionEvents().empty());
	CHECK(session.TakeResourceEvents().empty());
	CHECK(session.MissionEvents().All().empty());
	CHECK(session.ResourceEvents().All().empty());

	session.StartJoin("Player C");

	CHECK(session.State() == CoOpRelay::ConnectionState::CONNECTING);
	CHECK(session.PlayerName() == "Player C");
	CHECK(session.PlayerId().empty());
	CHECK_FALSE(session.Emitter().IsEnabled());
	CHECK(session.Remotes().All().empty());
	CHECK(session.Authorities().All().empty());
	CHECK(session.SharedNPCs().All().empty());
	CHECK(session.RecentEvents().empty());
	CHECK(session.TakeMissionEvents().empty());
	CHECK(session.TakeResourceEvents().empty());
	CHECK(session.MissionEvents().All().empty());
	CHECK(session.ResourceEvents().All().empty());
}



TEST_CASE( "Co-op client session rejects malformed welcomes", "[CoOpRelay]" )
{
	CoOpRelay::ClientSession session;
	session.StartJoin("Player A");

	CHECK_FALSE(session.AcceptWelcome("welcome\t2\tplayer-a"));
	CHECK(session.State() == CoOpRelay::ConnectionState::ERROR);
	CHECK(session.StatusText() == "Connection error: Malformed relay welcome");
	CHECK_FALSE(session.Emitter().IsEnabled());

	session.Disconnect();
	CHECK(session.State() == CoOpRelay::ConnectionState::DISCONNECTED);
	CHECK(session.Remotes().All().empty());
	CHECK(session.RecentEvents().empty());
}
