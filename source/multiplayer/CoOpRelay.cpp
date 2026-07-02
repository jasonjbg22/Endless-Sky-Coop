/* CoOpRelay.cpp
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

#include "CoOpRelay.h"

#include "../Fleet.h"
#include "../GameData.h"
#include "../Government.h"
#include "../Hasher.h"
#include "../Logger.h"
#include "../Outfit.h"
#include "../Planet.h"
#include "../PlayerInfo.h"
#include "../Random.h"
#include "../Ship.h"
#include "../System.h"
#include "../Weapon.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <list>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

using namespace std;

namespace {
	const string MESSAGE_SNAPSHOT = "snapshot";
	const string MESSAGE_EVENT = "event";
	const string MESSAGE_AUTHORITY = "authority";
	const string MESSAGE_PEER_ENDPOINT = "peer-endpoint";
	const string MESSAGE_NPC = "npc";
	const string MESSAGE_NPC_DAMAGE = "npc-damage";
	const string MESSAGE_COMBAT_HIT = "combat-hit";
	const string MESSAGE_WEAPON_FIRE = "weapon-fire";
	const string MESSAGE_NPC_BOARDING = "npc-boarding";
	const string MESSAGE_MISSION_EVENT = "mission-event";
	const string MESSAGE_RESOURCE_EVENT = "resource-event";
	const string MESSAGE_JOIN = "join";
	const string MESSAGE_REJECT = "reject";
	const string SERVER_WORLD_OWNER_ID = "coop-server";
	constexpr size_t MAX_SESSION_EVENTS = 100;
	constexpr uint64_t STALE_REMOTE_SNAPSHOT_STEPS = 600;
	constexpr uint64_t SYSTEM_AUTHORITY_LEASE_STEPS = 15;
	constexpr int64_t MAX_SHARED_MISSION_REWARD_CREDITS = 1000000000;
	constexpr double MAX_SHARED_RESOURCE_AMOUNT = 1000000000.;
	constexpr int SERVER_WORLD_NPCS_PER_SYSTEM = 6;
	constexpr uint64_t SERVER_WORLD_PUBLISH_INTERVAL = 2;
	constexpr double SERVER_WORLD_MAX_NPC_SPEED = 5.5;
	constexpr double SERVER_WORLD_NPC_ACCELERATION = .18;
	constexpr double SERVER_WORLD_WAYPOINT_REACHED = 180.;
	constexpr double SERVER_WORLD_PLAYER_ENGAGE_RADIUS = 2400.;
	constexpr double SERVER_WORLD_PLAYER_AVOID_RADIUS = 450.;
	constexpr uint64_t SERVER_WORLD_WEAPON_FIRE_INTERVAL = 45;



	bool CoopDiagnosticsLoggingEnabled()
	{
#ifdef _MSC_VER
		char *value = nullptr;
		size_t size = 0;
		if(_dupenv_s(&value, &size, "ES_COOP_DIAGNOSTICS") || !value)
			return false;
		const bool enabled = string_view(value) == "1";
		free(value);
		return enabled;
#else
		const char *value = std::getenv("ES_COOP_DIAGNOSTICS");
		return value && string_view(value) == "1";
#endif
	}



	void LogCoopDiagnostics(const string &message)
	{
		if(CoopDiagnosticsLoggingEnabled())
			Logger::Log(message, Logger::Level::INFO);
	}



	string EscapeField(const string &field)
	{
		string result;
		for(unsigned char ch : field)
		{
			switch(ch)
			{
				case '%':
					result += "%25";
					break;
				case '\t':
					result += "%09";
					break;
				case '\n':
					result += "%0A";
					break;
				case '\r':
					result += "%0D";
					break;
				default:
					result += static_cast<char>(ch);
					break;
			}
		}
		return result;
	}



	int HexValue(char ch)
	{
		if(ch >= '0' && ch <= '9')
			return ch - '0';
		if(ch >= 'A' && ch <= 'F')
			return 10 + ch - 'A';
		if(ch >= 'a' && ch <= 'f')
			return 10 + ch - 'a';
		return -1;
	}



	optional<string> UnescapeField(string_view field)
	{
		string result;
		for(size_t i = 0; i < field.size(); ++i)
		{
			if(field[i] != '%')
			{
				result += field[i];
				continue;
			}
			if(i + 2 >= field.size())
				return nullopt;
			const int high = HexValue(field[i + 1]);
			const int low = HexValue(field[i + 2]);
			if(high < 0 || low < 0)
				return nullopt;
			result += static_cast<char>((high << 4) + low);
			i += 2;
		}
		return result;
	}



	vector<string> SplitFields(const string &message)
	{
		vector<string> fields;
		size_t start = 0;
		while(start <= message.size())
		{
			size_t end = message.find('\t', start);
			if(end == string::npos)
				end = message.size();
			auto field = UnescapeField(string_view(message).substr(start, end - start));
			if(!field)
				return {};
			fields.push_back(*field);
			start = end + 1;
			if(end == message.size())
				break;
		}
		return fields;
	}



	template<typename Type>
	bool ParseNumber(const string &field, Type &value)
	{
		const char *begin = field.data();
		const char *end = begin + field.size();
		auto [ptr, ec] = from_chars(begin, end, value);
		return ec == errc() && ptr == end;
	}



	bool ParseNumber(const string &field, double &value)
	{
	char *end = nullptr;
	value = strtod(field.c_str(), &end);
	return end && end != field.c_str() && *end == '\0';
	}



	template<typename Type>
	void AppendField(ostringstream &out, const Type &value)
	{
		out << '\t' << value;
	}



	void AppendField(ostringstream &out, const string &value)
	{
		out << '\t' << EscapeField(value);
	}



	Point Interpolate(const Point &from, const Point &to, double fraction)
	{
		return from + (to - from) * fraction;
	}



	Angle InterpolateAngle(const Angle &from, const Angle &to, double fraction)
	{
		double delta = to.Degrees() - from.Degrees();
		while(delta > 180.)
			delta -= 360.;
		while(delta < -180.)
			delta += 360.;
		return Angle(from.Degrees() + delta * fraction);
	}



	double ClampFraction(double fraction)
	{
		if(!isfinite(fraction))
			return 0.;
		return clamp(fraction, 0., 1.);
	}



	string PlayerName(const PlayerInfo &player)
	{
		string name = player.FirstName();
		if(!player.LastName().empty())
		{
			if(!name.empty())
				name += ' ';
			name += player.LastName();
		}
		return name;
	}



	string Trim(string value)
	{
		size_t first = value.find_first_not_of(" \t\r\n");
		if(first == string::npos)
			return {};
		size_t last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}



	uint64_t LoadoutHash(const Ship &ship)
	{
		size_t hash = 0;
		Hasher::Hash(hash, ship.TrueModelName());
		for(const auto &[outfit, count] : ship.Outfits())
		{
			if(outfit)
				Hasher::Hash(hash, outfit->TrueName());
			Hasher::Hash(hash, count);
		}
		return static_cast<uint64_t>(hash);
	}



	bool ResourcesChanged(const CoOpRelay::PlayerSnapshot &first, const CoOpRelay::PlayerSnapshot &second)
	{
		return first.shields != second.shields
			|| first.hull != second.hull
			|| first.fuel != second.fuel
			|| first.energy != second.energy
			|| first.heat != second.heat;
	}



	bool SameNPCSnapshot(const CoOpRelay::SharedNPCSnapshot &first, const CoOpRelay::SharedNPCSnapshot &second)
	{
		return first.protocolVersion == second.protocolVersion
			&& first.sequence == second.sequence
			&& first.npcId == second.npcId
			&& first.ownerId == second.ownerId
			&& first.system == second.system
			&& first.position == second.position
			&& first.velocity == second.velocity
			&& first.facing == second.facing
			&& first.shipModel == second.shipModel
			&& first.government == second.government
			&& first.targetId == second.targetId
			&& first.shields == second.shields
			&& first.hull == second.hull
			&& first.fuel == second.fuel
			&& first.energy == second.energy
			&& first.heat == second.heat
			&& first.disabled == second.disabled
			&& first.destroyed == second.destroyed
			&& first.captured == second.captured
			&& first.removed == second.removed;
	}



	bool IsNormalizedDamage(double value)
	{
		return isfinite(value) && value >= 0. && value <= 1.;
	}



	bool IsFinitePoint(const Point &point)
	{
		return isfinite(point.X()) && isfinite(point.Y());
	}



	const Ship *ServerNPCModel(const CoOpRelay::SharedNPCSnapshot &snapshot)
	{
		if(snapshot.shipModel.empty())
			return nullptr;
		const Ship *model = GameData::Ships().Find(snapshot.shipModel);
		return (model && model->IsValid()) ? model : nullptr;
	}



	double ServerNPCMaxVelocity(const CoOpRelay::SharedNPCSnapshot &snapshot)
	{
		const Ship *model = ServerNPCModel(snapshot);
		return model ? clamp(model->MaxVelocity(), 1.5, 12.) : SERVER_WORLD_MAX_NPC_SPEED;
	}



	double ServerNPCAcceleration(const CoOpRelay::SharedNPCSnapshot &snapshot)
	{
		const Ship *model = ServerNPCModel(snapshot);
		return model ? clamp(model->Acceleration() / max(1., model->MaxVelocity()), .04, .35)
			: SERVER_WORLD_NPC_ACCELERATION;
	}



	bool ServerNPCIsHostile(const CoOpRelay::SharedNPCSnapshot &snapshot)
	{
		if(const Government *government = GameData::Governments().Find(snapshot.government))
			return government->IsEnemy();
		return snapshot.government == "Pirate";
	}



	double ServerNPCDisabledHull(const CoOpRelay::SharedNPCSnapshot &snapshot)
	{
		const Ship *model = ServerNPCModel(snapshot);
		return model ? clamp(model->DisabledHull(), .01, .99) : .2;
	}



	const Outfit *ServerNPCWeaponOutfit(const CoOpRelay::SharedNPCSnapshot &snapshot)
	{
		if(const Ship *model = ServerNPCModel(snapshot))
			for(const Hardpoint &hardpoint : model->Weapons())
				if(const Outfit *outfit = hardpoint.GetOutfit())
					if(outfit->GetWeapon())
						return outfit;
		const Outfit *fallback = GameData::Outfits().Find("Laser Turret");
		if(fallback && fallback->GetWeapon())
			return fallback;
		fallback = GameData::Outfits().Find("Blaster Turret");
		return (fallback && fallback->GetWeapon()) ? fallback : nullptr;
	}



	optional<CoOpRelay::SharedWeaponFire> ServerNPCWeaponFire(
		const CoOpRelay::SharedNPCSnapshot &npc, const CoOpRelay::PlayerSnapshot &target, uint64_t sequence)
	{
		const Outfit *outfit = ServerNPCWeaponOutfit(npc);
		const Weapon *weapon = outfit ? outfit->GetWeapon().get() : nullptr;
		const string weaponName = outfit ? outfit->TrueName() : "Laser Turret";
		const double weaponVelocity = weapon ? weapon->Velocity() : 24.;
		const int weaponLifetime = weapon ? weapon->Lifetime() : 60;
		const Point aim = target.position + target.velocity * 12.;
		Point direction = aim - npc.position;
		if(direction.LengthSquared() <= .0001)
			direction = npc.facing.Unit();
		const Angle facing(direction);
		const double range = clamp(weaponVelocity * max(1, weaponLifetime), 240., 1800.);

		CoOpRelay::SharedWeaponFire fire;
		fire.sequence = sequence;
		fire.playerId = SERVER_WORLD_OWNER_ID;
		fire.system = npc.system;
		fire.from = npc.position;
		fire.to = npc.position + facing.Unit() * range;
		fire.velocity = npc.velocity + facing.Unit() * max(weaponVelocity, 12.);
		fire.facing = facing.Degrees();
		fire.shipPosition = npc.position;
		fire.shipVelocity = npc.velocity;
		fire.hasShipVelocity = true;
		fire.targetPlayerId = target.playerId;
		fire.weapon = weaponName;
		return fire.IsValid() ? optional<CoOpRelay::SharedWeaponFire>(fire) : nullopt;
	}



	Point ServerNPCWaypoint(const Point &home, uint64_t seed, uint64_t step)
	{
		const uint64_t phase = step / 600 + seed;
		const double angle = static_cast<double>((phase * 73) % 360);
		const double radius = 900. + 120. * static_cast<double>(phase % 8);
		return home + Angle(angle).Unit() * radius;
	}



	Point InitialServerNPCDestination(const Point &home, const CoOpRelay::SharedNPCSnapshot &snapshot, uint64_t seed,
		uint64_t step)
	{
		if(snapshot.velocity.LengthSquared() > .0001)
			return snapshot.position + snapshot.velocity.Unit() * 1200.;
		return ServerNPCWaypoint(home, seed, step);
	}



	bool IsUsefulServerNPCShip(const shared_ptr<Ship> &ship)
	{
		return ship && ship->IsValid() && !ship->TrueModelName().empty() && ship->GetGovernment()
			&& ship->Zoom() == 1. && !ship->IsDestroyed() && !ship->IsDisabled();
	}



	const char *BoardingActionName(CoOpRelay::BoardingAction action) noexcept
	{
		switch(action)
		{
			case CoOpRelay::BoardingAction::REQUEST:
				return "request";
			case CoOpRelay::BoardingAction::CONFIRMED:
				return "confirmed";
			case CoOpRelay::BoardingAction::REJECTED:
				return "rejected";
			case CoOpRelay::BoardingAction::CAPTURED:
				return "captured";
		}
		return "request";
	}



	optional<CoOpRelay::BoardingAction> BoardingActionFromString(const string &action)
	{
		if(action == "request")
			return CoOpRelay::BoardingAction::REQUEST;
		if(action == "confirmed")
			return CoOpRelay::BoardingAction::CONFIRMED;
		if(action == "rejected")
			return CoOpRelay::BoardingAction::REJECTED;
		if(action == "captured")
			return CoOpRelay::BoardingAction::CAPTURED;
		return nullopt;
	}



	const char *MissionEventTypeName(CoOpRelay::MissionEventType type) noexcept
	{
		switch(type)
		{
			case CoOpRelay::MissionEventType::ACCEPTED:
				return "accepted";
			case CoOpRelay::MissionEventType::OBJECTIVE_UPDATED:
				return "objective-updated";
			case CoOpRelay::MissionEventType::NPC_DISABLED:
				return "npc-disabled";
			case CoOpRelay::MissionEventType::NPC_DESTROYED:
				return "npc-destroyed";
			case CoOpRelay::MissionEventType::NPC_BOARDED:
				return "npc-boarded";
			case CoOpRelay::MissionEventType::NPC_CAPTURED:
				return "npc-captured";
			case CoOpRelay::MissionEventType::COMPLETED:
				return "completed";
			case CoOpRelay::MissionEventType::FAILED:
				return "failed";
			case CoOpRelay::MissionEventType::REWARD:
				return "reward";
		}
		return "objective-updated";
	}



	optional<CoOpRelay::MissionEventType> MissionEventTypeFromString(const string &type)
	{
		if(type == "accepted")
			return CoOpRelay::MissionEventType::ACCEPTED;
		if(type == "objective-updated")
			return CoOpRelay::MissionEventType::OBJECTIVE_UPDATED;
		if(type == "npc-disabled")
			return CoOpRelay::MissionEventType::NPC_DISABLED;
		if(type == "npc-destroyed")
			return CoOpRelay::MissionEventType::NPC_DESTROYED;
		if(type == "npc-boarded")
			return CoOpRelay::MissionEventType::NPC_BOARDED;
		if(type == "npc-captured")
			return CoOpRelay::MissionEventType::NPC_CAPTURED;
		if(type == "completed")
			return CoOpRelay::MissionEventType::COMPLETED;
		if(type == "failed")
			return CoOpRelay::MissionEventType::FAILED;
		if(type == "reward")
			return CoOpRelay::MissionEventType::REWARD;
		return nullopt;
	}



	bool IsNPCMissionMilestone(CoOpRelay::MissionEventType type) noexcept
	{
		switch(type)
		{
			case CoOpRelay::MissionEventType::NPC_DISABLED:
			case CoOpRelay::MissionEventType::NPC_DESTROYED:
			case CoOpRelay::MissionEventType::NPC_BOARDED:
			case CoOpRelay::MissionEventType::NPC_CAPTURED:
				return true;
			default:
				return false;
		}
	}



	string MissionReplayKey(const CoOpRelay::SharedMissionEvent &event)
	{
		string key = event.missionId + '\t' + event.instanceId;
		if(IsNPCMissionMilestone(event.type))
		{
			key += '\t';
			key += MissionEventTypeName(event.type);
		}
		return key;
	}



	const char *ResourceActionTypeName(CoOpRelay::ResourceActionType type) noexcept
	{
		switch(type)
		{
			case CoOpRelay::ResourceActionType::REFUEL_ASSIST:
				return "refuel-assist";
			case CoOpRelay::ResourceActionType::REPAIR_ASSIST:
				return "repair-assist";
			case CoOpRelay::ResourceActionType::FUEL_TRANSFER:
				return "fuel-transfer";
			case CoOpRelay::ResourceActionType::ENERGY_TRANSFER:
				return "energy-transfer";
			case CoOpRelay::ResourceActionType::CARGO_TRANSFER:
				return "cargo-transfer";
			case CoOpRelay::ResourceActionType::CREDIT_REWARD:
				return "credit-reward";
		}
		return "fuel-transfer";
	}



	optional<CoOpRelay::ResourceActionType> ResourceActionTypeFromString(const string &type)
	{
		if(type == "refuel-assist")
			return CoOpRelay::ResourceActionType::REFUEL_ASSIST;
		if(type == "repair-assist")
			return CoOpRelay::ResourceActionType::REPAIR_ASSIST;
		if(type == "fuel-transfer")
			return CoOpRelay::ResourceActionType::FUEL_TRANSFER;
		if(type == "energy-transfer")
			return CoOpRelay::ResourceActionType::ENERGY_TRANSFER;
		if(type == "cargo-transfer")
			return CoOpRelay::ResourceActionType::CARGO_TRANSFER;
		if(type == "credit-reward")
			return CoOpRelay::ResourceActionType::CREDIT_REWARD;
		return nullopt;
	}



	const char *ResourceActionStatusName(CoOpRelay::ResourceActionStatus status) noexcept
	{
		switch(status)
		{
			case CoOpRelay::ResourceActionStatus::REQUEST:
				return "request";
			case CoOpRelay::ResourceActionStatus::CONFIRMED:
				return "confirmed";
			case CoOpRelay::ResourceActionStatus::REJECTED:
				return "rejected";
			case CoOpRelay::ResourceActionStatus::APPLIED:
				return "applied";
		}
		return "request";
	}



	optional<CoOpRelay::ResourceActionStatus> ResourceActionStatusFromString(const string &status)
	{
		if(status == "request")
			return CoOpRelay::ResourceActionStatus::REQUEST;
		if(status == "confirmed")
			return CoOpRelay::ResourceActionStatus::CONFIRMED;
		if(status == "rejected")
			return CoOpRelay::ResourceActionStatus::REJECTED;
		if(status == "applied")
			return CoOpRelay::ResourceActionStatus::APPLIED;
		return nullopt;
	}



	bool ResourceEventVisibleTo(const CoOpRelay::SharedResourceEvent &event, const string &playerId)
	{
		return event.targetPlayerId.empty() || event.targetPlayerId == playerId || event.playerId == playerId;
	}



	optional<string> CaptureRewardNPCId(const CoOpRelay::SharedResourceEvent &event)
	{
		const string prefix = "capture-reward:";
		if(event.actionId.rfind(prefix, 0) != 0)
			return nullopt;

		const size_t start = prefix.size();
		const size_t end = event.actionId.find(':', start);
		if(end == string::npos || end == start)
			return nullopt;
		return event.actionId.substr(start, end - start);
	}



	bool ResourceResponseMatchesRequest(const vector<CoOpRelay::SharedResourceEvent> &events,
		const CoOpRelay::SharedResourceEvent &response)
	{
		auto it = find_if(events.begin(), events.end(), [&response](const CoOpRelay::SharedResourceEvent &request) {
			return request.actionId == response.actionId
				&& request.status == CoOpRelay::ResourceActionStatus::REQUEST
				&& request.playerId == response.targetPlayerId
				&& (request.targetPlayerId.empty() || request.targetPlayerId == response.playerId)
				&& request.type == response.type
				&& request.resource == response.resource;
		});
		if(it == events.end())
			return false;
		if(any_of(events.begin(), events.end(), [&response](const CoOpRelay::SharedResourceEvent &current) {
			return current.actionId == response.actionId
				&& current.playerId == response.playerId
				&& (current.status == CoOpRelay::ResourceActionStatus::CONFIRMED
					|| current.status == CoOpRelay::ResourceActionStatus::REJECTED);
		}))
			return false;
		if(response.status == CoOpRelay::ResourceActionStatus::CONFIRMED)
			return response.amount == it->amount;
		return response.status == CoOpRelay::ResourceActionStatus::REJECTED && response.amount == 0.;
	}
}



bool CoOpRelay::PlayerSnapshot::IsLanded() const noexcept
{
	return !landedPlanet.empty();
}



bool CoOpRelay::PlayerSnapshot::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::PlayerEvent::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SystemAuthority::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SystemAuthority::IsActive() const noexcept
{
	return !system.empty() && !ownerId.empty() && playerCount;
}



bool CoOpRelay::PeerEndpoint::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::PeerEndpoint::IsValid() const noexcept
{
	return IsCompatible() && sequence && !playerId.empty()
		&& (removed || (!host.empty() && port));
}



bool CoOpRelay::SharedNPCSnapshot::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SharedNPCSnapshot::IsValid() const noexcept
{
	return IsCompatible() && !npcId.empty() && !ownerId.empty() && !system.empty();
}



bool CoOpRelay::SharedNPCDamage::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SharedNPCDamage::IsValid() const noexcept
{
	return IsCompatible() && sequence && !npcId.empty() && !ownerId.empty() && !system.empty() && !reporterId.empty()
		&& IsNormalizedDamage(shieldDamage) && IsNormalizedDamage(hullDamage)
		&& IsNormalizedDamage(fuelDamage) && IsNormalizedDamage(energyDamage)
		&& IsNormalizedDamage(heatDamage)
		&& (shieldDamage > 0. || hullDamage > 0. || fuelDamage > 0. || energyDamage > 0.
			|| heatDamage > 0. || disabled || destroyed);
}



bool CoOpRelay::SharedCombatHit::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SharedCombatHit::IsValid() const noexcept
{
	return IsCompatible() && sequence && !attackerId.empty() && !targetPlayerId.empty() && !system.empty()
		&& IsNormalizedDamage(shieldDamage) && IsNormalizedDamage(hullDamage)
		&& IsNormalizedDamage(fuelDamage) && IsNormalizedDamage(energyDamage)
		&& IsNormalizedDamage(heatDamage)
		&& IsFinitePoint(impactPosition) && IsFinitePoint(hitVelocity) && isfinite(facing)
		&& (shieldDamage > 0. || hullDamage > 0. || fuelDamage > 0. || energyDamage > 0.
			|| heatDamage > 0. || disabled || destroyed);
}



bool CoOpRelay::SharedWeaponFire::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SharedWeaponFire::IsValid() const noexcept
{
	return IsCompatible() && sequence && !playerId.empty() && !system.empty()
		&& !weapon.empty()
		&& (targetPlayerId.empty() || targetPlayerId != playerId)
		&& (targetPlayerId.empty() || targetNPCId.empty())
		&& IsFinitePoint(from) && IsFinitePoint(to) && IsFinitePoint(velocity) && IsFinitePoint(shipPosition)
		&& IsFinitePoint(shipVelocity)
		&& isfinite(facing) && from != to
		&& from.Distance(to) <= 2000.;
}



bool CoOpRelay::SharedNPCBoarding::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SharedNPCBoarding::IsValid() const noexcept
{
	return IsCompatible() && sequence && !npcId.empty() && !playerId.empty()
		&& !ownerId.empty() && playerId != ownerId && !system.empty();
}



bool CoOpRelay::SharedNPCBoarding::IsRequest() const noexcept
{
	return action == BoardingAction::REQUEST;
}



bool CoOpRelay::SharedMissionEvent::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SharedMissionEvent::IsValid() const noexcept
{
	return IsCompatible() && sequence && !missionId.empty() && !instanceId.empty() && !playerId.empty()
		&& credits >= 0 && credits <= MAX_SHARED_MISSION_REWARD_CREDITS;
}



bool CoOpRelay::SharedResourceEvent::IsCompatible() const noexcept
{
	return protocolVersion == PROTOCOL_VERSION;
}



bool CoOpRelay::SharedResourceEvent::IsValid() const noexcept
{
	return IsCompatible() && sequence && !actionId.empty() && !playerId.empty() && !resource.empty()
		&& isfinite(amount) && amount >= 0. && amount <= MAX_SHARED_RESOURCE_AMOUNT
		&& (type != ResourceActionType::CREDIT_REWARD || status == ResourceActionStatus::APPLIED)
		&& (status == ResourceActionStatus::REJECTED || amount > 0.);
}



bool CoOpRelay::AuthorityStore::Apply(const SystemAuthority &authority)
{
	if(authority.system.empty() || !authority.IsCompatible())
		return false;

	auto it = find_if(systems.begin(), systems.end(), [&authority](const SystemAuthority &current) {
		return current.system == authority.system;
	});
	if(it == systems.end())
	{
		if(!authority.IsActive())
			return false;
		systems.push_back(authority);
		return true;
	}

	if(authority.sequence < it->sequence)
		return false;
	if(!authority.IsActive())
	{
		systems.erase(it);
		return true;
	}

	*it = authority;
	return true;
}



void CoOpRelay::AuthorityStore::Clear()
{
	systems.clear();
}



const CoOpRelay::SystemAuthority *CoOpRelay::AuthorityStore::Get(const string &system) const
{
	auto it = find_if(systems.begin(), systems.end(), [&system](const SystemAuthority &authority) {
		return authority.system == system;
	});
	return it == systems.end() ? nullptr : &*it;
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::AuthorityStore::All() const
{
	return systems;
}



bool CoOpRelay::AuthorityStore::IsOwner(const string &system, const string &playerId) const
{
	const SystemAuthority *authority = Get(system);
	return authority && authority->ownerId == playerId;
}



bool CoOpRelay::SharedNPCStore::Apply(const SharedNPCSnapshot &snapshot)
{
	if(!snapshot.IsValid())
		return false;

	auto it = find_if(npcs.begin(), npcs.end(), [&snapshot](const SharedNPCSnapshot &current) {
		return current.npcId == snapshot.npcId;
	});
	if(it == npcs.end())
	{
		if(snapshot.removed)
			return false;
		npcs.push_back(snapshot);
		LogCoopDiagnostics("Co-op: created shared NPC proxy " + snapshot.npcId + " in " + snapshot.system + ".");
		return true;
	}

	if(snapshot.sequence < it->sequence || SameNPCSnapshot(snapshot, *it))
		return false;
	if(snapshot.removed)
	{
		LogCoopDiagnostics("Co-op: removed shared NPC proxy " + snapshot.npcId + " from " + snapshot.system + ".");
		npcs.erase(it);
		return true;
	}

	*it = snapshot;
	LogCoopDiagnostics("Co-op: updated shared NPC proxy " + snapshot.npcId + " in " + snapshot.system + ".");
	return true;
}



vector<CoOpRelay::SharedNPCSnapshot> CoOpRelay::SharedNPCStore::ReassignSystem(
	const string &system, const string &ownerId)
{
	vector<SharedNPCSnapshot> reassigned;
	if(system.empty() || ownerId.empty())
		return reassigned;

	for(SharedNPCSnapshot &snapshot : npcs)
	{
		if(snapshot.system != system || snapshot.ownerId == ownerId || snapshot.removed)
			continue;

		++snapshot.sequence;
		snapshot.ownerId = ownerId;
		reassigned.push_back(snapshot);
	}
	return reassigned;
}



vector<CoOpRelay::SharedNPCSnapshot> CoOpRelay::SharedNPCStore::RemoveOwner(const string &ownerId)
{
	vector<SharedNPCSnapshot> removed;
	for(auto it = npcs.begin(); it != npcs.end(); )
	{
		if(it->ownerId != ownerId)
		{
			++it;
			continue;
		}

		SharedNPCSnapshot snapshot = *it;
		++snapshot.sequence;
		snapshot.removed = true;
		removed.push_back(std::move(snapshot));
		it = npcs.erase(it);
	}
	return removed;
}



void CoOpRelay::SharedNPCStore::Clear()
{
	npcs.clear();
}



const CoOpRelay::SharedNPCSnapshot *CoOpRelay::SharedNPCStore::Get(const string &npcId) const
{
	auto it = find_if(npcs.begin(), npcs.end(), [&npcId](const SharedNPCSnapshot &snapshot) {
		return snapshot.npcId == npcId;
	});
	return it == npcs.end() ? nullptr : &*it;
}



vector<CoOpRelay::SharedNPCSnapshot> CoOpRelay::SharedNPCStore::All() const
{
	return npcs;
}



vector<CoOpRelay::SharedNPCSnapshot> CoOpRelay::SharedNPCStore::InSystem(const string &system) const
{
	vector<SharedNPCSnapshot> result;
	for(const SharedNPCSnapshot &snapshot : npcs)
		if(snapshot.system == system && !snapshot.removed)
			result.push_back(snapshot);
	return result;
}



bool CoOpRelay::PeerEndpointStore::Apply(const PeerEndpoint &endpoint)
{
	if(!endpoint.IsValid())
		return false;

	auto it = find_if(endpoints.begin(), endpoints.end(), [&endpoint](const PeerEndpoint &current) {
		return current.playerId == endpoint.playerId;
	});
	if(it == endpoints.end())
	{
		if(endpoint.removed)
			return false;
		endpoints.push_back(endpoint);
		return true;
	}

	if(endpoint.sequence < it->sequence)
		return false;
	if(endpoint.removed)
	{
		endpoints.erase(it);
		return true;
	}

	*it = endpoint;
	return true;
}



optional<CoOpRelay::PeerEndpoint> CoOpRelay::PeerEndpointStore::Remove(const string &playerId)
{
	auto it = find_if(endpoints.begin(), endpoints.end(), [&playerId](const PeerEndpoint &endpoint) {
		return endpoint.playerId == playerId;
	});
	if(it == endpoints.end())
		return nullopt;

	PeerEndpoint removed = *it;
	++removed.sequence;
	removed.removed = true;
	removed.host.clear();
	removed.port = 0;
	endpoints.erase(it);
	return removed;
}



void CoOpRelay::PeerEndpointStore::Clear()
{
	endpoints.clear();
}



const CoOpRelay::PeerEndpoint *CoOpRelay::PeerEndpointStore::Get(const string &playerId) const
{
	auto it = find_if(endpoints.begin(), endpoints.end(), [&playerId](const PeerEndpoint &endpoint) {
		return endpoint.playerId == playerId;
	});
	return it == endpoints.end() ? nullptr : &*it;
}



vector<CoOpRelay::PeerEndpoint> CoOpRelay::PeerEndpointStore::All() const
{
	return endpoints;
}



bool CoOpRelay::SharedMissionEventLog::Apply(const SharedMissionEvent &event)
{
	if(!event.IsValid())
		return false;
	const string eventKey = MissionReplayKey(event);
	for(const SharedMissionEvent &current : events)
	{
		if(current.playerId == event.playerId && current.sequence == event.sequence)
			return false;
		if(MissionReplayKey(current) != eventKey)
			continue;
		if(current.playerId != event.playerId)
			return false;
		if(event.sequence <= current.sequence)
			return false;
	}

	events.push_back(event);
	if(events.size() > MAX_SESSION_EVENTS)
		events.erase(events.begin());
	return true;
}



void CoOpRelay::SharedMissionEventLog::Clear()
{
	events.clear();
}



vector<CoOpRelay::SharedMissionEvent> CoOpRelay::SharedMissionEventLog::All() const
{
	return events;
}



vector<CoOpRelay::SharedMissionEvent> CoOpRelay::SharedMissionEventLog::LatestStates() const
{
	vector<SharedMissionEvent> latest;
	for(const SharedMissionEvent &event : events)
	{
		const string key = MissionReplayKey(event);
		auto it = find_if(latest.begin(), latest.end(), [&key](const SharedMissionEvent &current) {
			return MissionReplayKey(current) == key;
		});
		if(it == latest.end())
			latest.push_back(event);
		else if(event.sequence >= it->sequence)
			*it = event;
	}
	return latest;
}



bool CoOpRelay::SharedResourceEventLog::Apply(const SharedResourceEvent &event)
{
	if(!event.IsValid())
		return false;
	if(any_of(events.begin(), events.end(), [&event](const SharedResourceEvent &current) {
		return current.playerId == event.playerId && current.sequence == event.sequence;
	}))
		return false;

	events.push_back(event);
	if(events.size() > MAX_SESSION_EVENTS)
		events.erase(events.begin());
	return true;
}



void CoOpRelay::SharedResourceEventLog::Clear()
{
	events.clear();
}



vector<CoOpRelay::SharedResourceEvent> CoOpRelay::SharedResourceEventLog::All() const
{
	return events;
}



bool CoOpRelay::RemotePresence::IsInSystem(const string &system) const
{
	return latest.system == system && !latest.IsLanded() && latest.simulationActive;
}



bool CoOpRelay::RemotePresence::IsLanded() const noexcept
{
	return latest.IsLanded();
}



CoOpRelay::PlayerSnapshot CoOpRelay::RemotePresence::Interpolate(double fraction) const
{
	if(!hasPrevious)
		return latest;

	fraction = ClampFraction(fraction);
	PlayerSnapshot result = latest;
	result.position = ::Interpolate(previous.position, latest.position, fraction);
	result.velocity = ::Interpolate(previous.velocity, latest.velocity, fraction);
	result.facing = InterpolateAngle(previous.facing, latest.facing, fraction);
	return result;
}



bool CoOpRelay::PresenceStore::Apply(const PlayerSnapshot &snapshot)
{
	if(snapshot.playerId.empty() || !snapshot.IsCompatible())
		return false;

	auto it = find_if(players.begin(), players.end(), [&snapshot](const RemotePresence &presence) {
		return presence.latest.playerId == snapshot.playerId;
	});
	if(it == players.end())
	{
		players.push_back({{}, snapshot, false});
		return true;
	}

	if(snapshot.sequence <= it->latest.sequence)
		return false;

	it->previous = it->latest;
	it->latest = snapshot;
	it->hasPrevious = true;
	return true;
}



void CoOpRelay::PresenceStore::Remove(const string &playerId)
{
	players.erase(remove_if(players.begin(), players.end(), [&playerId](const RemotePresence &presence) {
		return presence.latest.playerId == playerId;
	}), players.end());
}



void CoOpRelay::PresenceStore::Clear()
{
	players.clear();
}



const CoOpRelay::RemotePresence *CoOpRelay::PresenceStore::Get(const string &playerId) const
{
	auto it = find_if(players.begin(), players.end(), [&playerId](const RemotePresence &presence) {
		return presence.latest.playerId == playerId;
	});
	return it == players.end() ? nullptr : &*it;
}



vector<CoOpRelay::RemotePresence> CoOpRelay::PresenceStore::All() const
{
	return players;
}



vector<CoOpRelay::RemotePresence> CoOpRelay::PresenceStore::InSystem(const string &system) const
{
	vector<RemotePresence> result;
	for(const RemotePresence &presence : players)
		if(presence.IsInSystem(system))
			result.push_back(presence);
	return result;
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::SystemAuthorityStore::ApplyPresence(const PlayerSnapshot &snapshot)
{
	const string &playerId = snapshot.playerId;
	const string &system = snapshot.system;
	vector<SystemAuthority> changed = RemoveFromOtherSystems(playerId, system);
	if(playerId.empty() || system.empty())
		return changed;
	++activityStep;

	auto it = find_if(systems.begin(), systems.end(), [&system](const SystemState &state) {
		return state.authority.system == system;
	});
	if(it == systems.end())
	{
		SystemState state;
		state.authority.system = system;
		state.players.push_back(playerId);
		if(snapshot.simulationActive)
			state.simulators.push_back({playerId, activityStep, !snapshot.IsLanded()});
		SelectOwner(state);
		changed.push_back(Touch(state));
		systems.push_back(std::move(state));
		return changed;
	}

	bool changedMembership = false;
	if(find(it->players.begin(), it->players.end(), playerId) == it->players.end())
	{
		it->players.push_back(playerId);
		changedMembership = true;
	}
	auto simulator = find_if(it->simulators.begin(), it->simulators.end(), [&playerId](const SimulatorState &state) {
		return state.playerId == playerId;
	});
	if(snapshot.simulationActive && simulator == it->simulators.end())
	{
		it->simulators.push_back({playerId, activityStep, !snapshot.IsLanded()});
		changedMembership = true;
	}
	else if(!snapshot.simulationActive && simulator != it->simulators.end())
	{
		it->simulators.erase(simulator);
		changedMembership = true;
	}
	else if(snapshot.simulationActive && simulator != it->simulators.end())
	{
		simulator->lastActiveStep = activityStep;
		simulator->foreground = !snapshot.IsLanded();
	}

	const bool expiredSimulators = ExpireInactiveSimulators(*it);
	if(SelectOwner(*it) || changedMembership || expiredSimulators)
		changed.push_back(Touch(*it));
	return changed;
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::SystemAuthorityStore::RemovePlayer(const string &playerId)
{
	vector<SystemAuthority> changed;
	if(playerId.empty())
		return changed;

	for(auto it = systems.begin(); it != systems.end(); )
	{
		vector<SystemAuthority> removed = RemoveFromSystem(*it, playerId, true);
		changed.insert(changed.end(), removed.begin(), removed.end());
		if(!it->authority.IsActive())
			it = systems.erase(it);
		else
			++it;
	}
	return changed;
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::SystemAuthorityStore::SetServerAuthority(
	const string &system, const string &ownerId, uint32_t playerCount)
{
	if(system.empty() || ownerId.empty())
		return {};

	auto it = find_if(systems.begin(), systems.end(), [&system](const SystemState &state) {
		return state.authority.system == system;
	});
	if(it == systems.end())
	{
		SystemState state;
		state.authority.system = system;
		state.authority.ownerId = ownerId;
		state.authority.playerCount = playerCount;
		SystemAuthority authority = Touch(state);
		systems.push_back(std::move(state));
		return {authority};
	}

	if(it->authority.ownerId == ownerId && it->authority.playerCount == playerCount)
		return {};

	it->authority.ownerId = ownerId;
	it->authority.playerCount = playerCount;
	it->players.clear();
	it->simulators.clear();
	return {Touch(*it)};
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::SystemAuthorityStore::RemoveServerAuthority(const string &system)
{
	auto it = find_if(systems.begin(), systems.end(), [&system](const SystemState &state) {
		return state.authority.system == system;
	});
	if(it == systems.end())
		return {};

	it->authority.ownerId.clear();
	it->authority.playerCount = 0;
	SystemAuthority removed = Touch(*it);
	systems.erase(it);
	return {removed};
}



void CoOpRelay::SystemAuthorityStore::Clear()
{
	systems.clear();
	nextSequence = 1;
	activityStep = 0;
}



const CoOpRelay::SystemAuthority *CoOpRelay::SystemAuthorityStore::Get(const string &system) const
{
	auto it = find_if(systems.begin(), systems.end(), [&system](const SystemState &state) {
		return state.authority.system == system;
	});
	return it == systems.end() ? nullptr : &it->authority;
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::SystemAuthorityStore::All() const
{
	vector<SystemAuthority> result;
	for(const SystemState &state : systems)
		result.push_back(state.authority);
	return result;
}



CoOpRelay::SystemAuthority CoOpRelay::SystemAuthorityStore::Touch(SystemState &state)
{
	state.authority.sequence = nextSequence++;
	state.authority.playerCount = static_cast<uint32_t>(state.players.size());
	return state.authority;
}



bool CoOpRelay::SystemAuthorityStore::SelectOwner(SystemState &state)
{
	const string previousOwner = state.authority.ownerId;
	auto current = find_if(state.simulators.begin(), state.simulators.end(), [&state](const SimulatorState &simulator) {
			return simulator.playerId == state.authority.ownerId;
		});
	if(current == state.simulators.end())
	{
		const bool hasForeground = any_of(state.simulators.begin(), state.simulators.end(),
			[](const SimulatorState &simulator) { return simulator.foreground; });
		const auto best = max_element(state.simulators.begin(), state.simulators.end(),
			[hasForeground](const SimulatorState &left, const SimulatorState &right) {
				const uint64_t leftScore = left.foreground == hasForeground ? left.lastActiveStep : 0;
				const uint64_t rightScore = right.foreground == hasForeground ? right.lastActiveStep : 0;
				return leftScore < rightScore;
			});
		state.authority.ownerId = (best == state.simulators.end()) ? string() : best->playerId;
	}
	return state.authority.ownerId != previousOwner;
}



bool CoOpRelay::SystemAuthorityStore::ExpireInactiveSimulators(SystemState &state)
{
	const size_t oldSize = state.simulators.size();
	state.simulators.erase(remove_if(state.simulators.begin(), state.simulators.end(),
		[this](const SimulatorState &simulator) {
			return activityStep > simulator.lastActiveStep
				&& activityStep - simulator.lastActiveStep > SYSTEM_AUTHORITY_LEASE_STEPS;
		}), state.simulators.end());
	return state.simulators.size() != oldSize;
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::SystemAuthorityStore::RemoveFromOtherSystems(
	const string &playerId, const string &system)
{
	vector<SystemAuthority> changed;
	if(playerId.empty())
		return changed;

	for(auto it = systems.begin(); it != systems.end(); )
	{
		if(it->authority.system == system)
		{
			++it;
			continue;
		}

		vector<SystemAuthority> removed = RemoveFromSystem(*it, playerId, false);
		changed.insert(changed.end(), removed.begin(), removed.end());
		if(!it->authority.IsActive())
			it = systems.erase(it);
		else
			++it;
	}
	return changed;
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::SystemAuthorityStore::RemoveFromSystem(
	SystemState &state, const string &playerId, bool playerDisconnected)
{
	auto player = find(state.players.begin(), state.players.end(), playerId);
	if(player == state.players.end() && (!playerDisconnected || state.authority.ownerId != playerId))
		return {};

	if(player != state.players.end())
		state.players.erase(player);
	state.simulators.erase(remove_if(state.simulators.begin(), state.simulators.end(),
		[&playerId](const SimulatorState &simulator) {
			return simulator.playerId == playerId;
		}), state.simulators.end());

	if(state.players.empty())
	{
		state.authority.ownerId.clear();
		return {Touch(state)};
	}

	SelectOwner(state);
	return {Touch(state)};
}



void CoOpRelay::LocalStateEmitter::SetEnabled(bool enabled) noexcept
{
	this->enabled = enabled;
	if(!enabled)
	{
		previous.reset();
		latest.reset();
		events.clear();
		simulationActive = false;
		previousDisabled = false;
		previousDestroyed = false;
	}
}



bool CoOpRelay::LocalStateEmitter::IsEnabled() const noexcept
{
	return enabled;
}



void CoOpRelay::LocalStateEmitter::SetSimulationActive(bool active) noexcept
{
	simulationActive = active;
}



void CoOpRelay::LocalStateEmitter::SetIdentity(string playerId)
{
	this->playerId = std::move(playerId);
}



const string &CoOpRelay::LocalStateEmitter::PlayerId() const noexcept
{
	return playerId;
}



bool CoOpRelay::LocalStateEmitter::Step(const PlayerInfo &player)
{
	if(!enabled || playerId.empty())
		return false;

	auto snapshot = SnapshotFromPlayer(player, playerId, nextSequence++);
	if(!snapshot)
		return false;

	const Ship *flagship = player.Flagship();
	snapshot->simulationActive = simulationActive && !player.IsDead()
		&& flagship && !flagship->IsDestroyed()
		&& !flagship->IsEnteringHyperspace() && !flagship->IsHyperspacing();
	previous = latest;
	latest = *snapshot;

	if(previous)
	{
		if(previous->system != latest->system)
			QueueEvent(EventType::JUMPED, *latest, previous->system);
		if(!previous->IsLanded() && latest->IsLanded())
			QueueEvent(EventType::LANDED, *latest);
		else if(previous->IsLanded() && !latest->IsLanded())
			QueueEvent(EventType::LAUNCHED, *latest, previous->landedPlanet);
		if(previous->shipModel != latest->shipModel || previous->loadoutHash != latest->loadoutHash)
			QueueEvent(EventType::SHIP_CHANGED, *latest);
		if(ResourcesChanged(*previous, *latest))
			QueueEvent(EventType::RESOURCES_CHANGED, *latest);
	}

	const bool disabled = flagship && flagship->IsDisabled();
	const bool destroyed = flagship && flagship->IsDestroyed();
	if(disabled && !previousDisabled)
		QueueEvent(EventType::DISABLED, *latest);
	if(destroyed && !previousDestroyed)
		QueueEvent(EventType::DESTROYED, *latest);
	previousDisabled = disabled;
	previousDestroyed = destroyed;

	return true;
}



const optional<CoOpRelay::PlayerSnapshot> &CoOpRelay::LocalStateEmitter::LatestSnapshot() const noexcept
{
	return latest;
}



vector<CoOpRelay::PlayerEvent> CoOpRelay::LocalStateEmitter::TakeEvents()
{
	vector<PlayerEvent> result;
	result.swap(events);
	return result;
}



void CoOpRelay::LocalStateEmitter::QueueEvent(EventType type, const PlayerSnapshot &snapshot, string detail)
{
	PlayerEvent event;
	event.sequence = snapshot.sequence;
	event.playerId = snapshot.playerId;
	event.type = type;
	event.system = snapshot.system;
	event.planet = snapshot.landedPlanet;
	event.detail = std::move(detail);
	events.push_back(std::move(event));
}



void CoOpRelay::RelayServerCore::SetRoomPassword(string password)
{
	roomPassword = std::move(password);
}



bool CoOpRelay::RelayServerCore::HasRoomPassword() const noexcept
{
	return !roomPassword.empty();
}



string CoOpRelay::RelayServerCore::Join(string name)
{
	string id = "player-" + to_string(nextPlayerNumber++);
	peers.push_back({id, std::move(name), nextResourceReplayIndex});
	return id;
}



CoOpRelay::JoinResult CoOpRelay::RelayServerCore::TryJoin(const JoinRequest &request)
{
	JoinResult result;
	if(request.playerName.empty())
	{
		result.message = "Player name is required";
		return result;
	}
	if(HasRoomPassword() && request.password != roomPassword)
	{
		result.message = "Room password rejected";
		return result;
	}

	result.accepted = true;
	result.playerId = Join(request.playerName);
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Leave(const string &playerId)
{
	peers.erase(remove_if(peers.begin(), peers.end(), [&playerId](const Peer &peer) {
		return peer.id == playerId;
	}), peers.end());
	presence.Remove(playerId);
	vector<RelayDelivery> deliveries;
	if(optional<PeerEndpoint> removed = peerEndpoints.Remove(playerId))
	{
		vector<RelayDelivery> endpointDeliveries = PeerEndpointDeliveries(*removed);
		deliveries.insert(deliveries.end(), endpointDeliveries.begin(), endpointDeliveries.end());
	}
	for(const SystemAuthority &authority : authorities.RemovePlayer(playerId))
	{
		vector<RelayDelivery> authorityDeliveries = AuthorityDeliveries(authority);
		deliveries.insert(deliveries.end(), authorityDeliveries.begin(), authorityDeliveries.end());
		if(authority.IsActive())
			for(const SharedNPCSnapshot &snapshot : sharedNPCs.ReassignSystem(authority.system, authority.ownerId))
			{
				vector<RelayDelivery> npcDeliveries = NPCDeliveries(snapshot, true);
				deliveries.insert(deliveries.end(), npcDeliveries.begin(), npcDeliveries.end());
			}
	}
	for(const SharedNPCSnapshot &snapshot : sharedNPCs.RemoveOwner(playerId))
	{
		vector<RelayDelivery> npcDeliveries = NPCDeliveries(snapshot);
		deliveries.insert(deliveries.end(), npcDeliveries.begin(), npcDeliveries.end());
	}
	return deliveries;
}



vector<CoOpRelay::PlayerSnapshot> CoOpRelay::RelayServerCore::LatestSnapshotsFor(const string &playerId) const
{
	vector<PlayerSnapshot> result;
	if(!HasPeer(playerId))
		return result;

	for(const RemotePresence &remote : presence.All())
		if(remote.latest.playerId != playerId)
			result.push_back(remote.latest);
	return result;
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::RelayServerCore::LatestAuthoritiesFor(const string &playerId) const
{
	return HasPeer(playerId) ? authorities.All() : vector<SystemAuthority>();
}



vector<CoOpRelay::PeerEndpoint> CoOpRelay::RelayServerCore::LatestPeerEndpointsFor(const string &playerId) const
{
	vector<PeerEndpoint> result;
	if(!HasPeer(playerId))
		return result;

	for(const PeerEndpoint &endpoint : peerEndpoints.All())
		if(endpoint.playerId != playerId)
			result.push_back(endpoint);
	return result;
}



vector<CoOpRelay::SharedNPCSnapshot> CoOpRelay::RelayServerCore::LatestNPCsFor(const string &playerId) const
{
	return HasPeer(playerId) ? sharedNPCs.All() : vector<SharedNPCSnapshot>();
}



vector<CoOpRelay::SharedMissionEvent> CoOpRelay::RelayServerCore::LatestMissionEventsFor(const string &playerId) const
{
	return HasPeer(playerId) ? missionEvents.LatestStates() : vector<SharedMissionEvent>();
}



vector<CoOpRelay::SharedResourceEvent> CoOpRelay::RelayServerCore::LatestResourceEventsFor(const string &playerId) const
{
	const Peer *peer = GetPeer(playerId);
	if(!peer)
		return {};

	vector<SharedResourceEvent> result;
	for(const ResourceReplayEntry &entry : resourceReplay)
	{
		if(entry.index < peer->joinedResourceReplayIndex)
			continue;
		const SharedResourceEvent &event = entry.event;
		if(ResourceEventVisibleTo(event, playerId))
			result.push_back(event);
	}
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const PlayerSnapshot &snapshot)
{
	if(!HasPeer(snapshot.playerId))
		return {};

	const RemotePresence *previousPresence = presence.Get(snapshot.playerId);
	const bool enteredSystem = !previousPresence || previousPresence->latest.system != snapshot.system;
	if(!presence.Apply(snapshot))
	{
		if(previousPresence && snapshot.sequence == previousPresence->latest.sequence)
			++duplicatePreventionCount;
		else
			++staleProxyCount;
		return {};
	}

	vector<RelayDelivery> deliveries = SnapshotDeliveries(snapshot);
	const vector<SystemAuthority> authorityChanges = serverWorldEnabled
		? vector<SystemAuthority>() : authorities.ApplyPresence(snapshot);
	for(const SystemAuthority &authority : authorityChanges)
	{
		vector<RelayDelivery> authorityDeliveries = AuthorityDeliveries(authority);
		deliveries.insert(deliveries.end(), authorityDeliveries.begin(), authorityDeliveries.end());
		if(authority.IsActive())
			for(const SharedNPCSnapshot &npc : sharedNPCs.ReassignSystem(authority.system, authority.ownerId))
			{
				vector<RelayDelivery> npcDeliveries = NPCDeliveries(npc, true);
				deliveries.insert(deliveries.end(), npcDeliveries.begin(), npcDeliveries.end());
			}
	}
	if(enteredSystem)
		for(const SharedNPCSnapshot &npc : sharedNPCs.InSystem(snapshot.system))
			if(npc.ownerId != snapshot.playerId)
				deliveries.push_back({snapshot.playerId, nullopt, nullopt, nullopt, npc, nullopt, nullopt});
	return deliveries;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const PlayerEvent &event) const
{
	if(!HasPeer(event.playerId) || !event.IsCompatible())
		return {};
	return EventDeliveries(event);
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const PeerEndpoint &endpoint)
{
	if(!HasPeer(endpoint.playerId))
		return {};
	if(!peerEndpoints.Apply(endpoint))
	{
		++staleProxyCount;
		return {};
	}
	return PeerEndpointDeliveries(endpoint);
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const SharedNPCSnapshot &snapshot)
{
	const SystemAuthority *authority = AuthorityFor(snapshot.system);
	if(!snapshot.IsValid() || !HasPeer(snapshot.ownerId) || !authority || authority->ownerId != snapshot.ownerId)
		return {};
	if(!sharedNPCs.Apply(snapshot))
	{
		if(const SharedNPCSnapshot *stored = sharedNPCs.Get(snapshot.npcId))
		{
			if(SameNPCSnapshot(snapshot, *stored))
				++duplicatePreventionCount;
			else
				++staleProxyCount;
		}
		else
			++staleProxyCount;
		return {};
	}
	return NPCDeliveries(snapshot);
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const SharedNPCDamage &damage)
{
	const SharedNPCSnapshot *snapshot = sharedNPCs.Get(damage.npcId);
	if(!damage.IsValid() || !HasPeer(damage.reporterId) || !snapshot || snapshot->removed)
		return {};
	if(damage.ownerId != snapshot->ownerId || damage.system != snapshot->system)
		return {};

	const SystemAuthority *authority = AuthorityFor(snapshot->system);
	if(!authority || authority->ownerId != snapshot->ownerId || damage.reporterId == snapshot->ownerId)
		return {};
	const RemotePresence *reporter = presence.Get(damage.reporterId);
	if(!reporter || reporter->latest.system != snapshot->system)
		return {};
	if(serverWorldEnabled && snapshot->ownerId == SERVER_WORLD_OWNER_ID)
		return ReceiveServerNPCDamage(damage);
	return NPCDamageDeliveries(damage);
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const SharedCombatHit &hit)
{
	if(!hit.IsValid() || !HasPeer(hit.attackerId) || !HasPeer(hit.targetPlayerId)
			|| hit.attackerId == hit.targetPlayerId)
		return {};

	const RemotePresence *attacker = presence.Get(hit.attackerId);
	const RemotePresence *target = presence.Get(hit.targetPlayerId);
	if(!attacker || !target)
		return {};
	if(attacker->latest.IsLanded() || target->latest.IsLanded())
		return {};
	if(attacker->latest.system.empty() || attacker->latest.system != target->latest.system
			|| hit.system != attacker->latest.system)
		return {};
	if(!attacker->latest.simulationActive || !target->latest.simulationActive)
		return {};

	const double totalDamage = hit.shieldDamage + hit.hullDamage + hit.fuelDamage + hit.energyDamage + hit.heatDamage;
	if(totalDamage <= 0. || totalDamage > 5.)
		return {};

	return CombatHitDeliveries(hit);
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const SharedWeaponFire &fire)
{
	if(!fire.IsValid() || !HasPeer(fire.playerId))
		return {};

	const RemotePresence *shooter = presence.Get(fire.playerId);
	if(!shooter || shooter->latest.IsLanded() || shooter->latest.system.empty()
			|| fire.system != shooter->latest.system || !shooter->latest.simulationActive)
		return {};

	return WeaponFireDeliveries(fire);
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const SharedNPCBoarding &boarding)
{
	const SharedNPCSnapshot *snapshot = sharedNPCs.Get(boarding.npcId);
	if(!boarding.IsValid() || !snapshot || snapshot->removed)
		return {};

	const SystemAuthority *authority = AuthorityFor(snapshot->system);
	if(!authority || authority->ownerId != snapshot->ownerId)
		return {};
	if(boarding.ownerId != snapshot->ownerId || boarding.system != snapshot->system)
		return {};
	if(serverWorldEnabled && snapshot->ownerId == SERVER_WORLD_OWNER_ID)
		return ReceiveServerNPCBoarding(boarding);

	if(boarding.IsRequest())
	{
		if(!HasPeer(boarding.playerId) || boarding.playerId == snapshot->ownerId)
			return {};
		const RemotePresence *boarder = presence.Get(boarding.playerId);
		if(!boarder || boarder->latest.IsLanded() || boarder->latest.system != snapshot->system
				|| !boarder->latest.simulationActive)
			return {};
	}
	else if(!HasPeer(boarding.ownerId) || boarding.ownerId != snapshot->ownerId || !HasPeer(boarding.playerId))
		return {};

	return NPCBoardingDeliveries(boarding);
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const SharedMissionEvent &event)
{
	if(!HasPeer(event.playerId) || !event.IsValid())
		return {};
	if(event.type == MissionEventType::REWARD && event.credits > 0)
	{
		const SystemAuthority *authority = AuthorityFor(event.system);
		if(event.system.empty() || !authority || authority->ownerId != event.playerId)
			return {};
	}
	if(IsNPCMissionMilestone(event.type))
	{
		if(event.npcId.empty() || event.system.empty())
			return {};

		const SystemAuthority *authority = AuthorityFor(event.system);
		if(!authority || authority->ownerId != event.playerId)
			return {};

		const SharedNPCSnapshot *snapshot = sharedNPCs.Get(event.npcId);
		if(!snapshot && event.type != MissionEventType::NPC_CAPTURED)
			return {};
		if(snapshot && (snapshot->ownerId != event.playerId || snapshot->system != event.system))
			return {};
	}
	if(!missionEvents.Apply(event))
		return {};

	vector<RelayDelivery> deliveries = MissionEventDeliveries(event);
	if(event.type == MissionEventType::REWARD && event.credits > 0)
	{
		for(const Peer &peer : peers)
		{
			if(peer.id == event.playerId)
				continue;

			SharedResourceEvent reward;
			reward.sequence = nextServerResourceEventSequence++;
			reward.actionId = "mission-reward:" + event.instanceId + ":" + peer.id;
			reward.playerId = event.playerId;
			reward.targetPlayerId = peer.id;
			reward.type = ResourceActionType::CREDIT_REWARD;
			reward.status = ResourceActionStatus::APPLIED;
			reward.resource = "credits";
			reward.amount = static_cast<double>(event.credits);
			reward.detail = event.detail.empty() ? "Co-op mission reward." : event.detail;
			if(!resourceEvents.Apply(reward))
				continue;

			resourceReplay.push_back({nextResourceReplayIndex++, reward});
			if(resourceReplay.size() > MAX_SESSION_EVENTS)
				resourceReplay.erase(resourceReplay.begin());

			vector<RelayDelivery> resourceDeliveries = ResourceEventDeliveries(reward);
			deliveries.insert(deliveries.end(), resourceDeliveries.begin(), resourceDeliveries.end());
		}
	}
	return deliveries;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::Receive(const SharedResourceEvent &event)
{
	if(!HasPeer(event.playerId) || (!event.targetPlayerId.empty() && !HasPeer(event.targetPlayerId))
			|| !event.IsValid())
		return {};
	if(event.status == ResourceActionStatus::APPLIED)
	{
		if(event.type != ResourceActionType::CREDIT_REWARD)
			return {};
		optional<string> npcId = CaptureRewardNPCId(event);
		const SharedNPCSnapshot *snapshot = npcId ? sharedNPCs.Get(*npcId) : nullptr;
		const SystemAuthority *authority = snapshot ? AuthorityFor(snapshot->system) : nullptr;
		if(event.targetPlayerId.empty() || event.resource != "credits" || !snapshot || snapshot->removed
				|| snapshot->ownerId != event.playerId || !authority || authority->ownerId != event.playerId)
			return {};
	}
	else if((event.status == ResourceActionStatus::CONFIRMED || event.status == ResourceActionStatus::REJECTED)
			&& !ResourceResponseMatchesRequest(resourceEvents.All(), event))
		return {};
	if(!resourceEvents.Apply(event))
		return {};
	resourceReplay.push_back({nextResourceReplayIndex++, event});
	if(resourceReplay.size() > MAX_SESSION_EVENTS)
		resourceReplay.erase(resourceReplay.begin());
	return ResourceEventDeliveries(event);
}



void CoOpRelay::RelayServerCore::SetServerWorldEnabled(bool enabled) noexcept
{
	serverWorldEnabled = enabled;
	if(enabled)
		return;

	for(const ServerNPC &npc : serverNPCs)
	{
		SharedNPCSnapshot removed = npc.snapshot;
		removed.sequence = nextServerNPCSequence++;
		removed.removed = true;
		sharedNPCs.Apply(removed);
	}
	serverNPCs.clear();
}



bool CoOpRelay::RelayServerCore::IsServerWorldEnabled() const noexcept
{
	return serverWorldEnabled;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::StepServerWorld(unsigned steps)
{
	vector<RelayDelivery> deliveries;
	if(!serverWorldEnabled)
		return deliveries;

	for(unsigned i = 0; i < steps; ++i)
		++serverWorldStep;

	const vector<RemotePresence> currentPresence = presence.All();
	vector<string> activeSystems;
	for(const RemotePresence &remote : currentPresence)
	{
		const string &system = remote.latest.system;
		if(!system.empty() && find(activeSystems.begin(), activeSystems.end(), system) == activeSystems.end())
			activeSystems.push_back(system);
	}

	for(const string &system : activeSystems)
	{
		const uint32_t playerCount = static_cast<uint32_t>(count_if(currentPresence.begin(), currentPresence.end(),
			[&system](const RemotePresence &remote) {
				return remote.latest.system == system;
			}));
		for(const SystemAuthority &authority : authorities.SetServerAuthority(system, SERVER_WORLD_OWNER_ID, playerCount))
		{
			vector<RelayDelivery> authorityDeliveries = AuthorityDeliveries(authority);
			deliveries.insert(deliveries.end(), authorityDeliveries.begin(), authorityDeliveries.end());
		}
	}

	for(auto it = serverNPCs.begin(); it != serverNPCs.end(); )
	{
		if(find(activeSystems.begin(), activeSystems.end(), it->snapshot.system) != activeSystems.end())
		{
			++it;
			continue;
		}

		SharedNPCSnapshot removed = it->snapshot;
		removed.sequence = nextServerNPCSequence++;
		removed.removed = true;
		sharedNPCs.Apply(removed);
		vector<RelayDelivery> npcDeliveries = NPCDeliveries(removed, true);
		deliveries.insert(deliveries.end(), npcDeliveries.begin(), npcDeliveries.end());
		it = serverNPCs.erase(it);
	}

	for(const SystemAuthority &authority : authorities.All())
	{
		if(authority.ownerId != SERVER_WORLD_OWNER_ID)
			continue;
		if(find(activeSystems.begin(), activeSystems.end(), authority.system) != activeSystems.end())
			continue;
		for(const SystemAuthority &removed : authorities.RemoveServerAuthority(authority.system))
		{
			vector<RelayDelivery> authorityDeliveries = AuthorityDeliveries(removed);
			deliveries.insert(deliveries.end(), authorityDeliveries.begin(), authorityDeliveries.end());
		}
	}

	for(const string &system : activeSystems)
	{
		int systemNPCs = static_cast<int>(count_if(serverNPCs.begin(), serverNPCs.end(),
			[&system](const ServerNPC &npc) {
				return npc.snapshot.system == system && !npc.snapshot.removed;
			}));
		while(systemNPCs < SERVER_WORLD_NPCS_PER_SYSTEM)
		{
			int spawned = 0;
			const System *gameSystem = GameData::Systems().Find(system);
			if(gameSystem && gameSystem->IsValid())
			{
				vector<const RandomEvent<Fleet> *> candidates;
				int totalWeight = 0;
				for(const RandomEvent<Fleet> &fleet : gameSystem->Fleets())
				{
					const Fleet *fleetDefinition = fleet.Get();
					if(!fleetDefinition || !fleetDefinition->IsValid() || !fleet.CanTrigger())
						continue;
					int weight = max(1, 100000 / max(1, fleet.Period()));
					if(fleetDefinition->GetGovernment() && fleetDefinition->GetGovernment()->IsEnemy())
						weight *= 4;
					totalWeight += weight;
					candidates.push_back(&fleet);
				}

				const Fleet *selectedFleet = nullptr;
				if(!candidates.empty())
				{
					int choice = Random::Int(totalWeight);
					for(const RandomEvent<Fleet> *candidate : candidates)
					{
						int weight = max(1, 100000 / max(1, candidate->Period()));
						if(candidate->Get()->GetGovernment() && candidate->Get()->GetGovernment()->IsEnemy())
							weight *= 4;
						if(choice < weight)
						{
							selectedFleet = candidate->Get();
							break;
						}
						choice -= weight;
					}
					if(!selectedFleet)
						selectedFleet = candidates.back()->Get();
				}

				list<shared_ptr<Ship>> placedShips;
				if(selectedFleet)
					selectedFleet->Place(*gameSystem, placedShips, false, false);
				for(const shared_ptr<Ship> &ship : placedShips)
				{
					if(systemNPCs >= SERVER_WORLD_NPCS_PER_SYSTEM)
						break;
					if(!IsUsefulServerNPCShip(ship))
						continue;

					const uint64_t npcNumber = nextServerNPCId++;
					ServerNPC npc;
					npc.courseSeed = npcNumber;
					npc.home = ship->Position();
					npc.snapshot.sequence = nextServerNPCSequence++;
					npc.snapshot.npcId = "server-npc-" + to_string(npcNumber);
					npc.snapshot.ownerId = SERVER_WORLD_OWNER_ID;
					npc.snapshot.system = system;
					npc.snapshot.position = ship->Position();
					npc.snapshot.velocity = ship->Velocity();
					npc.snapshot.facing = ship->Facing();
					npc.snapshot.shipModel = ship->TrueModelName();
					npc.snapshot.government = ship->GetGovernment()->TrueName();
					npc.snapshot.shields = ship->Shields();
					npc.snapshot.hull = ship->Hull();
					npc.snapshot.fuel = ship->Fuel();
					npc.snapshot.energy = ship->Energy();
					npc.snapshot.heat = ship->Heat();
					npc.lastPublishedStep = serverWorldStep;
					npc.destination = InitialServerNPCDestination(npc.home, npc.snapshot, npc.courseSeed, serverWorldStep);
					sharedNPCs.Apply(npc.snapshot);
					vector<RelayDelivery> npcDeliveries = NPCDeliveries(npc.snapshot, true);
					deliveries.insert(deliveries.end(), npcDeliveries.begin(), npcDeliveries.end());
					serverNPCs.push_back(std::move(npc));
					++systemNPCs;
					++spawned;
				}
			}

			if(spawned)
				continue;

			const double angle = (nextServerNPCId * 53) % 360;
			const double radius = 500. + 90. * (nextServerNPCId % 5);
			const Point home(Angle(angle).Unit() * radius);

			const uint64_t npcNumber = nextServerNPCId++;
			ServerNPC npc;
			npc.courseSeed = npcNumber;
			npc.home = home;
			npc.snapshot.sequence = nextServerNPCSequence++;
			npc.snapshot.npcId = "server-npc-" + to_string(npcNumber);
			npc.snapshot.ownerId = SERVER_WORLD_OWNER_ID;
			npc.snapshot.system = system;
			npc.snapshot.position = home;
			npc.snapshot.velocity = Angle(angle + 90.).Unit() * 1.5;
			npc.snapshot.facing = Angle(npc.snapshot.velocity);
			npc.snapshot.shipModel = "Sparrow";
			npc.snapshot.government = "Pirate";
			npc.snapshot.shields = 1.;
			npc.snapshot.hull = 1.;
			npc.snapshot.fuel = 1.;
			npc.snapshot.energy = 1.;
			npc.snapshot.heat = 0.;
			npc.lastPublishedStep = serverWorldStep;
			npc.destination = InitialServerNPCDestination(npc.home, npc.snapshot, npc.courseSeed, serverWorldStep);
			sharedNPCs.Apply(npc.snapshot);
			vector<RelayDelivery> npcDeliveries = NPCDeliveries(npc.snapshot, true);
			deliveries.insert(deliveries.end(), npcDeliveries.begin(), npcDeliveries.end());
			serverNPCs.push_back(std::move(npc));
			++systemNPCs;
		}
	}

	for(ServerNPC &npc : serverNPCs)
	{
		if(npc.snapshot.destroyed || npc.snapshot.captured || npc.snapshot.removed)
			continue;

		const RemotePresence *nearestPlayer = nullptr;
		double bestDistance = (numeric_limits<double>::max)();
		for(const RemotePresence &remote : currentPresence)
		{
			if(remote.latest.system != npc.snapshot.system)
				continue;
			if(!remote.latest.simulationActive || remote.latest.IsLanded() || remote.latest.hull <= 0.)
				continue;
			const double distance = npc.snapshot.position.Distance(remote.latest.position);
			if(distance < bestDistance)
			{
				bestDistance = distance;
				nearestPlayer = &remote;
			}
		}

		if(npc.snapshot.position.Distance(npc.destination) < SERVER_WORLD_WAYPOINT_REACHED)
			npc.destination = ServerNPCWaypoint(npc.home, npc.courseSeed, serverWorldStep);
		npc.snapshot.targetId.clear();

		const Point toTarget = npc.destination - npc.snapshot.position;
		const double maxVelocity = ServerNPCMaxVelocity(npc.snapshot);
		Point desiredVelocity = toTarget.Unit() * maxVelocity;
		const bool hostile = ServerNPCIsHostile(npc.snapshot);
		if(hostile && nearestPlayer && bestDistance < SERVER_WORLD_PLAYER_ENGAGE_RADIUS)
		{
			npc.snapshot.targetId = nearestPlayer->latest.playerId;
			const Point intercept = nearestPlayer->latest.position + nearestPlayer->latest.velocity * 12.;
			const Point pursuit = intercept - npc.snapshot.position;
			if(pursuit.LengthSquared() > .0001)
				desiredVelocity = pursuit.Unit() * maxVelocity;
		}
		if(nearestPlayer && bestDistance < SERVER_WORLD_PLAYER_AVOID_RADIUS)
		{
			const Point away = (npc.snapshot.position - nearestPlayer->latest.position).Unit();
			const double avoidWeight = hostile ? .45 : 1.4;
			const Point blended = desiredVelocity + away * maxVelocity * avoidWeight;
			if(blended.LengthSquared() > .0001)
				desiredVelocity = blended.Unit() * maxVelocity;
		}
		npc.snapshot.velocity = npc.snapshot.velocity.Lerp(desiredVelocity, ServerNPCAcceleration(npc.snapshot));
		if(npc.snapshot.velocity.Length() > maxVelocity)
			npc.snapshot.velocity = npc.snapshot.velocity.Unit() * maxVelocity;
		npc.snapshot.position += npc.snapshot.velocity;
		if(npc.snapshot.velocity.LengthSquared() > .0001)
			npc.snapshot.facing = Angle(npc.snapshot.velocity);
		npc.snapshot.energy = min(1., npc.snapshot.energy + .01);
		npc.snapshot.heat = max(0., npc.snapshot.heat - .005);
		if(!npc.snapshot.targetId.empty() && nearestPlayer
				&& serverWorldStep - npc.lastWeaponFireStep >= SERVER_WORLD_WEAPON_FIRE_INTERVAL)
		{
			if(optional<SharedWeaponFire> fire = ServerNPCWeaponFire(
					npc.snapshot, nearestPlayer->latest, nextServerWeaponFireSequence++))
			{
				vector<RelayDelivery> fireDeliveries = WeaponFireDeliveries(*fire);
				deliveries.insert(deliveries.end(), fireDeliveries.begin(), fireDeliveries.end());
				npc.lastWeaponFireStep = serverWorldStep;
			}
		}

		if(serverWorldStep - npc.lastPublishedStep < SERVER_WORLD_PUBLISH_INTERVAL)
			continue;
		npc.lastPublishedStep = serverWorldStep;
		npc.snapshot.sequence = nextServerNPCSequence++;
		sharedNPCs.Apply(npc.snapshot);
		vector<RelayDelivery> npcDeliveries = NPCDeliveries(npc.snapshot, true);
		deliveries.insert(deliveries.end(), npcDeliveries.begin(), npcDeliveries.end());
	}

	return deliveries;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::ReceiveServerNPCDamage(const SharedNPCDamage &damage)
{
	const SharedNPCSnapshot *stored = sharedNPCs.Get(damage.npcId);
	if(!stored || stored->ownerId != SERVER_WORLD_OWNER_ID)
		return {};

	const string shipModel = stored->shipModel.empty() ? "ship" : stored->shipModel;
	const Ship *model = ServerNPCModel(*stored);
	SharedNPCSnapshot updated = *stored;
	updated.sequence = nextServerNPCSequence++;
	updated.shields = max(0., updated.shields - clamp(damage.shieldDamage, 0., 1.));
	updated.hull = max(0., updated.hull - clamp(damage.hullDamage, 0., 1.));
	updated.fuel = max(0., updated.fuel - clamp(damage.fuelDamage, 0., 1.));
	updated.energy = max(0., updated.energy - clamp(damage.energyDamage, 0., 1.));
	updated.heat = min(1., updated.heat + clamp(damage.heatDamage, 0., 1.));
	updated.disabled = damage.disabled || updated.hull <= ServerNPCDisabledHull(updated);
	updated.destroyed = damage.destroyed || updated.hull <= 0.;
	updated.removed = updated.destroyed;
	if(!sharedNPCs.Apply(updated))
		return {};

	for(ServerNPC &npc : serverNPCs)
	{
		if(npc.snapshot.npcId != updated.npcId)
			continue;
		npc.snapshot = updated;
		npc.lastPublishedStep = serverWorldStep;
		break;
	}
	if(updated.removed)
		serverNPCs.erase(remove_if(serverNPCs.begin(), serverNPCs.end(),
			[&updated](const ServerNPC &npc) {
				return npc.snapshot.npcId == updated.npcId;
			}), serverNPCs.end());

	vector<RelayDelivery> deliveries = NPCDeliveries(updated, true);
	if(updated.destroyed)
	{
		SharedMissionEvent mission;
		mission.sequence = nextServerMissionEventSequence++;
		mission.missionId = "shared-npc";
		mission.instanceId = updated.npcId;
		mission.playerId = SERVER_WORLD_OWNER_ID;
		mission.type = MissionEventType::NPC_DESTROYED;
		mission.system = updated.system;
		mission.npcId = updated.npcId;
		mission.detail = "Shared " + shipModel + " destroyed.";
		if(missionEvents.Apply(mission))
		{
			vector<RelayDelivery> missionDeliveries = MissionEventDeliveries(mission);
			deliveries.insert(deliveries.end(), missionDeliveries.begin(), missionDeliveries.end());
		}

		vector<string> participants;
		if(!damage.reporterId.empty())
			participants.push_back(damage.reporterId);
		for(const RemotePresence &remote : presence.InSystem(updated.system))
		{
			const string &playerId = remote.latest.playerId;
			if(!playerId.empty() && find(participants.begin(), participants.end(), playerId) == participants.end())
				participants.push_back(playerId);
		}
		if(!participants.empty())
		{
			const int64_t reward = model ? clamp<int64_t>(model->Cost() / 200, 500, 25000) : 2500;
			const int64_t perPlayerCredits = max<int64_t>(1, reward / static_cast<int64_t>(participants.size()));
			for(const string &participant : participants)
			{
				SharedResourceEvent event;
				event.sequence = nextServerResourceEventSequence++;
				event.actionId = "server-destroy-reward:" + updated.npcId + ":" + participant;
				event.playerId = SERVER_WORLD_OWNER_ID;
				event.targetPlayerId = participant;
				event.type = ResourceActionType::CREDIT_REWARD;
				event.status = ResourceActionStatus::APPLIED;
				event.resource = "credits";
				event.amount = static_cast<double>(perPlayerCredits);
				event.detail = "Co-op reward: " + to_string(perPlayerCredits)
					+ " credits for destroying shared " + shipModel + ".";
				if(!resourceEvents.Apply(event))
					continue;

				resourceReplay.push_back({nextResourceReplayIndex++, event});
				if(resourceReplay.size() > MAX_SESSION_EVENTS)
					resourceReplay.erase(resourceReplay.begin());

				vector<RelayDelivery> resourceDeliveries = ResourceEventDeliveries(event);
				deliveries.insert(deliveries.end(), resourceDeliveries.begin(), resourceDeliveries.end());
			}
		}
	}
	return deliveries;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::ReceiveServerNPCBoarding(const SharedNPCBoarding &boarding)
{
	if(!boarding.IsRequest() || !HasPeer(boarding.playerId))
		return {};
	const SharedNPCSnapshot *stored = sharedNPCs.Get(boarding.npcId);
	if(!stored || stored->ownerId != SERVER_WORLD_OWNER_ID || stored->system != boarding.system)
		return {};
	const RemotePresence *boarder = presence.Get(boarding.playerId);
	if(!boarder || boarder->latest.IsLanded() || boarder->latest.system != boarding.system
			|| !boarder->latest.simulationActive)
		return {};

	vector<RelayDelivery> deliveries;
	SharedNPCBoarding result = boarding;
	result.sequence = nextServerBoardingSequence++;
	if(!stored->disabled || stored->destroyed)
	{
		result.action = BoardingAction::REJECTED;
		result.detail = "Co-op boarding rejected: shared target is not disabled.";
		deliveries.push_back({boarding.playerId, nullopt, nullopt, nullopt, nullopt, nullopt, result});
		return deliveries;
	}

	result.action = BoardingAction::CAPTURED;
	result.detail = "Co-op capture confirmed by server.";
	deliveries.push_back({boarding.playerId, nullopt, nullopt, nullopt, nullopt, nullopt, result});

	const string shipModel = stored->shipModel.empty() ? "ship" : stored->shipModel;
	auto publishMissionEvent = [&](MissionEventType type, string detail) {
		SharedMissionEvent event;
		event.sequence = nextServerMissionEventSequence++;
		event.missionId = "shared-npc";
		event.instanceId = boarding.npcId;
		event.playerId = SERVER_WORLD_OWNER_ID;
		event.type = type;
		event.system = boarding.system;
		event.npcId = boarding.npcId;
		event.detail = std::move(detail);
		if(!missionEvents.Apply(event))
			return;

		vector<RelayDelivery> missionDeliveries = MissionEventDeliveries(event);
		deliveries.insert(deliveries.end(), missionDeliveries.begin(), missionDeliveries.end());
	};
	publishMissionEvent(MissionEventType::NPC_BOARDED, "Shared " + shipModel + " boarded.");
	publishMissionEvent(MissionEventType::NPC_CAPTURED, "Shared " + shipModel + " captured.");

	vector<string> participants;
	participants.push_back(boarding.playerId);
	for(const RemotePresence &remote : presence.InSystem(boarding.system))
	{
		const string &playerId = remote.latest.playerId;
		if(!playerId.empty() && find(participants.begin(), participants.end(), playerId) == participants.end())
			participants.push_back(playerId);
	}

	const Ship *model = ServerNPCModel(*stored);
	const int64_t reward = model ? clamp<int64_t>(model->Cost() / 100, 1000, 50000) : 5000;
	const int64_t perPlayerCredits = max<int64_t>(1, reward / static_cast<int64_t>(participants.size()));
	for(const string &participant : participants)
	{
		SharedResourceEvent event;
		event.sequence = nextServerResourceEventSequence++;
		event.actionId = "server-capture-reward:" + boarding.npcId + ":" + participant;
		event.playerId = SERVER_WORLD_OWNER_ID;
		event.targetPlayerId = participant;
		event.type = ResourceActionType::CREDIT_REWARD;
		event.status = ResourceActionStatus::APPLIED;
		event.resource = "credits";
		event.amount = static_cast<double>(perPlayerCredits);
		event.detail = "Co-op reward: " + to_string(perPlayerCredits) + " credits for capturing shared " + shipModel + ".";
		if(!resourceEvents.Apply(event))
			continue;

		resourceReplay.push_back({nextResourceReplayIndex++, event});
		if(resourceReplay.size() > MAX_SESSION_EVENTS)
			resourceReplay.erase(resourceReplay.begin());

		vector<RelayDelivery> resourceDeliveries = ResourceEventDeliveries(event);
		deliveries.insert(deliveries.end(), resourceDeliveries.begin(), resourceDeliveries.end());
	}

	SharedNPCSnapshot removed = *stored;
	removed.sequence = nextServerNPCSequence++;
	removed.captured = true;
	removed.removed = true;
	sharedNPCs.Apply(removed);
	serverNPCs.erase(remove_if(serverNPCs.begin(), serverNPCs.end(),
		[&removed](const ServerNPC &npc) {
			return npc.snapshot.npcId == removed.npcId;
		}), serverNPCs.end());

	vector<RelayDelivery> npcDeliveries = NPCDeliveries(removed, true);
	deliveries.insert(deliveries.end(), npcDeliveries.begin(), npcDeliveries.end());
	return deliveries;
}



const CoOpRelay::SystemAuthority *CoOpRelay::RelayServerCore::AuthorityFor(const string &system) const
{
	return authorities.Get(system);
}



vector<CoOpRelay::SystemAuthority> CoOpRelay::RelayServerCore::Authorities() const
{
	return authorities.All();
}



const CoOpRelay::SharedNPCStore &CoOpRelay::RelayServerCore::SharedNPCs() const noexcept
{
	return sharedNPCs;
}



const CoOpRelay::SharedMissionEventLog &CoOpRelay::RelayServerCore::MissionEvents() const noexcept
{
	return missionEvents;
}



const CoOpRelay::SharedResourceEventLog &CoOpRelay::RelayServerCore::ResourceEvents() const noexcept
{
	return resourceEvents;
}



size_t CoOpRelay::RelayServerCore::PlayerCount() const noexcept
{
	return peers.size();
}



const CoOpRelay::PresenceStore &CoOpRelay::RelayServerCore::Presence() const noexcept
{
	return presence;
}



CoOpRelay::Diagnostics CoOpRelay::RelayServerCore::GetDiagnostics() const
{
	Diagnostics diagnostics;
	diagnostics.connectedPlayers = peers.size();
	diagnostics.serverWorldEnabled = serverWorldEnabled;
	diagnostics.remotePlayerProxies = presence.All().size();
	diagnostics.npcProxies = sharedNPCs.All().size();
	diagnostics.staleProxyCount = staleProxyCount;
	diagnostics.duplicatePreventionCount = duplicatePreventionCount;
	return diagnostics;
}



bool CoOpRelay::RelayServerCore::HasPeer(const string &playerId) const
{
	return GetPeer(playerId);
}



const CoOpRelay::RelayServerCore::Peer *CoOpRelay::RelayServerCore::GetPeer(const string &playerId) const
{
	auto it = find_if(peers.begin(), peers.end(), [&playerId](const Peer &peer) {
		return peer.id == playerId;
	});
	return it == peers.end() ? nullptr : &*it;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::SnapshotDeliveries(const PlayerSnapshot &snapshot) const
{
	vector<RelayDelivery> result;
	for(const Peer &peer : peers)
		if(peer.id != snapshot.playerId)
			result.push_back({peer.id, snapshot, nullopt, nullopt, nullopt, nullopt, nullopt});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::EventDeliveries(const PlayerEvent &event) const
{
	vector<RelayDelivery> result;
	for(const Peer &peer : peers)
		if(peer.id != event.playerId)
			result.push_back({peer.id, nullopt, event, nullopt, nullopt, nullopt, nullopt});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::AuthorityDeliveries(
	const SystemAuthority &authority) const
{
	vector<RelayDelivery> result;
	for(const Peer &peer : peers)
		result.push_back({peer.id, nullopt, nullopt, authority, nullopt, nullopt, nullopt});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::PeerEndpointDeliveries(
	const PeerEndpoint &endpoint) const
{
	vector<RelayDelivery> result;
	for(const Peer &peer : peers)
		if(peer.id != endpoint.playerId)
			result.push_back({peer.id, nullopt, nullopt, nullopt, nullopt, nullopt, nullopt, nullopt, nullopt, endpoint});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::NPCDeliveries(
	const SharedNPCSnapshot &snapshot, bool includeOwner) const
{
	vector<RelayDelivery> result;
	for(const Peer &peer : peers)
		if(includeOwner || peer.id != snapshot.ownerId)
			result.push_back({peer.id, nullopt, nullopt, nullopt, snapshot, nullopt, nullopt});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::NPCDamageDeliveries(
	const SharedNPCDamage &damage) const
{
	vector<RelayDelivery> result;
	const SharedNPCSnapshot *snapshot = sharedNPCs.Get(damage.npcId);
	if(snapshot)
		result.push_back({snapshot->ownerId, nullopt, nullopt, nullopt, nullopt, damage, nullopt});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::CombatHitDeliveries(
	const SharedCombatHit &hit) const
{
	vector<RelayDelivery> result;
	if(HasPeer(hit.targetPlayerId))
		result.push_back({hit.targetPlayerId, nullopt, nullopt, nullopt, nullopt, nullopt, nullopt,
			nullopt, nullopt, nullopt, hit});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::WeaponFireDeliveries(
	const SharedWeaponFire &fire) const
{
	vector<RelayDelivery> result;
	for(const Peer &peer : peers)
	{
		if(peer.id == fire.playerId)
			continue;
		const RemotePresence *remote = presence.Get(peer.id);
		if(!remote || remote->latest.IsLanded() || remote->latest.system != fire.system
				|| !remote->latest.simulationActive)
			continue;

		RelayDelivery delivery;
		delivery.recipientId = peer.id;
		delivery.weaponFire = fire;
		result.push_back(std::move(delivery));
	}
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::NPCBoardingDeliveries(
	const SharedNPCBoarding &boarding) const
{
	vector<RelayDelivery> result;
	const SharedNPCSnapshot *snapshot = sharedNPCs.Get(boarding.npcId);
	if(!snapshot)
		return result;

	if(boarding.IsRequest())
		result.push_back({snapshot->ownerId, nullopt, nullopt, nullopt, nullopt, nullopt, boarding});
	else
		result.push_back({boarding.playerId, nullopt, nullopt, nullopt, nullopt, nullopt, boarding});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::MissionEventDeliveries(
	const SharedMissionEvent &event) const
{
	vector<RelayDelivery> result;
	for(const Peer &peer : peers)
		if(peer.id != event.playerId)
			result.push_back({peer.id, nullopt, nullopt, nullopt, nullopt, nullopt, nullopt, event});
	return result;
}



vector<CoOpRelay::RelayDelivery> CoOpRelay::RelayServerCore::ResourceEventDeliveries(
	const SharedResourceEvent &event) const
{
	vector<RelayDelivery> result;
	for(const Peer &peer : peers)
		if(peer.id != event.playerId && ResourceEventVisibleTo(event, peer.id))
			result.push_back({peer.id, nullopt, nullopt, nullopt, nullopt, nullopt, nullopt, nullopt, event});
	return result;
}



void CoOpRelay::ClientSession::StartJoin(string playerName, string password)
{
	this->playerName = std::move(playerName);
	this->password = std::move(password);
	playerId.clear();
	errorReason.clear();
	remotes.Clear();
	authorities.Clear();
	peerEndpointStore.Clear();
	sharedNPCs.Clear();
	missionEventLog.Clear();
	resourceEventLog.Clear();
	npcDamageReports.clear();
	combatHits.clear();
	weaponFires.clear();
	npcDamageSequences.clear();
	combatHitSequences.clear();
	weaponFireSequences.clear();
	npcBoardingReports.clear();
	npcBoardingSequences.clear();
	eventSequences.clear();
	remoteSnapshotActivity.clear();
	missionEvents.clear();
	resourceEvents.clear();
	recentEvents.clear();
	desyncWarnings.clear();
	connectionStep = 0;
	staleProxyCount = 0;
	duplicatePreventionCount = 0;
	emitter.SetEnabled(false);
	state = ConnectionState::CONNECTING;
}



void CoOpRelay::ClientSession::Disconnect()
{
	playerId.clear();
	errorReason.clear();
	remotes.Clear();
	authorities.Clear();
	peerEndpointStore.Clear();
	sharedNPCs.Clear();
	missionEventLog.Clear();
	resourceEventLog.Clear();
	npcDamageReports.clear();
	combatHits.clear();
	weaponFires.clear();
	npcDamageSequences.clear();
	combatHitSequences.clear();
	weaponFireSequences.clear();
	npcBoardingReports.clear();
	npcBoardingSequences.clear();
	eventSequences.clear();
	remoteSnapshotActivity.clear();
	missionEvents.clear();
	resourceEvents.clear();
	recentEvents.clear();
	desyncWarnings.clear();
	connectionStep = 0;
	staleProxyCount = 0;
	duplicatePreventionCount = 0;
	emitter.SetEnabled(false);
	state = ConnectionState::DISCONNECTED;
}



void CoOpRelay::ClientSession::SetError(string reason)
{
	playerId.clear();
	errorReason = std::move(reason);
	remotes.Clear();
	authorities.Clear();
	peerEndpointStore.Clear();
	sharedNPCs.Clear();
	missionEventLog.Clear();
	resourceEventLog.Clear();
	npcDamageReports.clear();
	combatHits.clear();
	weaponFires.clear();
	npcDamageSequences.clear();
	combatHitSequences.clear();
	weaponFireSequences.clear();
	npcBoardingReports.clear();
	npcBoardingSequences.clear();
	eventSequences.clear();
	remoteSnapshotActivity.clear();
	missionEvents.clear();
	resourceEvents.clear();
	recentEvents.clear();
	desyncWarnings.clear();
	connectionStep = 0;
	staleProxyCount = 0;
	duplicatePreventionCount = 0;
	emitter.SetEnabled(false);
	state = ConnectionState::ERROR;
}



bool CoOpRelay::ClientSession::AcceptWelcome(const string &line)
{
	vector<string> fields = SplitFields(line);
	if(fields.size() == 3 && fields[0] == MESSAGE_REJECT && fields[1] == to_string(PROTOCOL_VERSION))
	{
		SetError(fields[2].empty() ? "Join rejected" : fields[2]);
		return false;
	}
	if(fields.size() != 3 || fields[0] != "welcome" || fields[1] != to_string(PROTOCOL_VERSION) || fields[2].empty())
	{
		SetError("Malformed relay welcome");
		return false;
	}

	playerId = fields[2];
	emitter.SetIdentity(playerId);
	emitter.SetEnabled(true);
	state = ConnectionState::CONNECTED;
	return true;
}



bool CoOpRelay::ClientSession::ReceiveRelayLine(const string &line)
{
	if(state != ConnectionState::CONNECTED)
		return false;

	if(auto snapshot = ParseSnapshot(line))
	{
		if(snapshot->playerId == playerId)
			return false;
		if(const RemotePresence *presence = remotes.Get(snapshot->playerId))
			if(snapshot->sequence == presence->latest.sequence)
			{
				++duplicatePreventionCount;
				return false;
			}
		if(remotes.Apply(*snapshot))
		{
			NoteRemoteSnapshot(*snapshot);
			return true;
		}
		++staleProxyCount;
		RecordDesyncWarning("Ignored stale presence from "
			+ (snapshot->name.empty() ? snapshot->playerId : snapshot->name) + ".");
		return false;
	}
	if(auto event = ParseEvent(line))
	{
		if(event->playerId == playerId)
			return false;
		if(!AcceptEvent(*event))
		{
			++staleProxyCount;
			return false;
		}
		recentEvents.push_back(*event);
		if(recentEvents.size() > 20)
			recentEvents.erase(recentEvents.begin());
		return true;
	}
	if(auto authority = ParseAuthority(line))
	{
		if(authorities.Apply(*authority))
			return true;
		++staleProxyCount;
		RecordDesyncWarning("Ignored stale authority update for " + authority->system + ".");
		return false;
	}
	if(auto endpoint = ParsePeerEndpoint(line))
	{
		if(endpoint->playerId == playerId)
			return false;
		if(peerEndpointStore.Apply(*endpoint))
			return true;
		++staleProxyCount;
		RecordDesyncWarning("Ignored stale peer endpoint from " + endpoint->playerId + ".");
		return false;
	}
	if(auto npc = ParseNPCSnapshot(line))
	{
		if(const SharedNPCSnapshot *stored = sharedNPCs.Get(npc->npcId))
			if(SameNPCSnapshot(*npc, *stored))
			{
				++duplicatePreventionCount;
				return false;
			}
		if(sharedNPCs.Apply(*npc))
			return true;
		++staleProxyCount;
		RecordDesyncWarning("Ignored stale shared NPC update for " + npc->npcId + ".");
		return false;
	}
	if(auto damage = ParseNPCDamage(line))
	{
		if(damage->reporterId == playerId)
			return false;
		bool exactDuplicate = false;
		if(!AcceptNPCDamageReport(*damage, &exactDuplicate))
		{
			if(!exactDuplicate)
			{
				++staleProxyCount;
				RecordDesyncWarning("Ignored stale damage report from " + damage->reporterId
					+ " for " + damage->npcId + ".");
			}
			else
				++duplicatePreventionCount;
			return false;
		}
		npcDamageReports.push_back(*damage);
		return true;
	}
	if(auto hit = ParseCombatHit(line))
	{
		if(hit->targetPlayerId != playerId)
			return false;
		bool exactDuplicate = false;
		if(!AcceptCombatHit(*hit, &exactDuplicate))
		{
			if(!exactDuplicate)
			{
				++staleProxyCount;
				RecordDesyncWarning("Ignored stale combat hit from " + hit->attackerId + ".");
			}
			else
				++duplicatePreventionCount;
			return false;
		}
		combatHits.push_back(*hit);
		return true;
	}
	if(auto fire = ParseWeaponFire(line))
	{
		if(fire->playerId == playerId)
			return false;
		bool exactDuplicate = false;
		if(!AcceptWeaponFire(*fire, &exactDuplicate))
		{
			if(!exactDuplicate)
			{
				++staleProxyCount;
				RecordDesyncWarning("Ignored stale weapon fire from " + fire->playerId + ".");
			}
			else
				++duplicatePreventionCount;
			return false;
		}
		weaponFires.push_back(*fire);
		return true;
	}
	if(auto boarding = ParseNPCBoarding(line))
	{
		if(boarding->IsRequest() ? boarding->playerId == playerId : boarding->ownerId == playerId)
			return false;
		bool exactDuplicate = false;
		if(!AcceptNPCBoardingReport(*boarding, &exactDuplicate))
		{
			if(!exactDuplicate)
			{
				++staleProxyCount;
				const string source = boarding->IsRequest() ? boarding->playerId : boarding->ownerId;
				RecordDesyncWarning("Ignored stale boarding report from " + source
					+ " for " + boarding->npcId + ".");
			}
			else
				++duplicatePreventionCount;
			return false;
		}
		npcBoardingReports.push_back(*boarding);
		return true;
	}
	if(auto mission = ParseMissionEvent(line))
	{
		if(mission->playerId == playerId)
			return false;
		if(!missionEventLog.Apply(*mission))
		{
			++staleProxyCount;
			RecordDesyncWarning("Ignored stale mission event from " + mission->playerId
				+ " for " + mission->instanceId + ".");
			return false;
		}
		missionEvents.push_back(*mission);
		return true;
	}
	if(auto resource = ParseResourceEvent(line))
	{
		if(resource->playerId == playerId)
			return false;
		if(!ResourceEventVisibleTo(*resource, playerId))
			return false;
		if(!resourceEventLog.Apply(*resource))
		{
			++staleProxyCount;
			RecordDesyncWarning("Ignored stale resource event from " + resource->playerId
				+ " for " + resource->actionId + ".");
			return false;
		}
		resourceEvents.push_back(*resource);
		return true;
	}
	return false;
}



void CoOpRelay::ClientSession::SetSimulationActive(bool active) noexcept
{
	emitter.SetSimulationActive(active);
}



void CoOpRelay::ClientSession::StepConnectionHealth(unsigned steps)
{
	if(state != ConnectionState::CONNECTED || !steps)
		return;
	connectionStep += steps;

	for(RemoteSnapshotActivity &activity : remoteSnapshotActivity)
	{
		if(activity.staleWarningSent || connectionStep - activity.lastSeenStep <= STALE_REMOTE_SNAPSHOT_STEPS)
			continue;

		const RemotePresence *presence = remotes.Get(activity.playerId);
		string label = activity.playerId;
		if(presence && !presence->latest.name.empty())
			label = presence->latest.name;
		RecordDesyncWarning("No recent presence snapshot from " + label + "; remote state may be stale.");
		++staleProxyCount;
		activity.staleWarningSent = true;
	}
}



vector<CoOpRelay::OutgoingMessage> CoOpRelay::ClientSession::StepLocal(PlayerInfo &player)
{
	vector<OutgoingMessage> result;
	if(state != ConnectionState::CONNECTED)
		return result;

	if(emitter.Step(player))
		if(const auto &snapshot = emitter.LatestSnapshot())
			result.push_back({Serialize(*snapshot), true, false});

	for(const PlayerEvent &event : emitter.TakeEvents())
		result.push_back({Serialize(event), false, true});
	return result;
}



CoOpRelay::ConnectionState CoOpRelay::ClientSession::State() const noexcept
{
	return state;
}



const string &CoOpRelay::ClientSession::PlayerId() const noexcept
{
	return playerId;
}



const string &CoOpRelay::ClientSession::PlayerName() const noexcept
{
	return playerName;
}



const string &CoOpRelay::ClientSession::Password() const noexcept
{
	return password;
}



string CoOpRelay::ClientSession::StatusText() const
{
	switch(state)
	{
		case ConnectionState::DISCONNECTED:
			return "Disconnected";
		case ConnectionState::CONNECTING:
			return "Connecting";
		case ConnectionState::CONNECTED:
		{
			string result = "Connected as " + playerId;
			if(!desyncWarnings.empty())
				result += " - " + to_string(desyncWarnings.size()) + " desync "
					+ (desyncWarnings.size() == 1 ? "warning" : "warnings");
			return result;
		}
		case ConnectionState::ERROR:
			return errorReason.empty() ? "Connection error" : "Connection error: " + errorReason;
	}
	return "Disconnected";
}



const CoOpRelay::PresenceStore &CoOpRelay::ClientSession::Remotes() const noexcept
{
	return remotes;
}



const CoOpRelay::AuthorityStore &CoOpRelay::ClientSession::Authorities() const noexcept
{
	return authorities;
}



const CoOpRelay::PeerEndpointStore &CoOpRelay::ClientSession::PeerEndpoints() const noexcept
{
	return peerEndpointStore;
}



const CoOpRelay::SharedNPCStore &CoOpRelay::ClientSession::SharedNPCs() const noexcept
{
	return sharedNPCs;
}



const CoOpRelay::SharedMissionEventLog &CoOpRelay::ClientSession::MissionEvents() const noexcept
{
	return missionEventLog;
}



const CoOpRelay::SharedResourceEventLog &CoOpRelay::ClientSession::ResourceEvents() const noexcept
{
	return resourceEventLog;
}



vector<CoOpRelay::SharedNPCDamage> CoOpRelay::ClientSession::TakeNPCDamageReports()
{
	vector<SharedNPCDamage> result;
	result.swap(npcDamageReports);
	return result;
}



vector<CoOpRelay::SharedCombatHit> CoOpRelay::ClientSession::TakeCombatHits()
{
	vector<SharedCombatHit> result;
	result.swap(combatHits);
	return result;
}



vector<CoOpRelay::SharedWeaponFire> CoOpRelay::ClientSession::TakeWeaponFires()
{
	vector<SharedWeaponFire> result;
	result.swap(weaponFires);
	return result;
}



vector<CoOpRelay::SharedNPCBoarding> CoOpRelay::ClientSession::TakeNPCBoardingReports()
{
	vector<SharedNPCBoarding> result;
	result.swap(npcBoardingReports);
	return result;
}



vector<CoOpRelay::SharedMissionEvent> CoOpRelay::ClientSession::TakeMissionEvents()
{
	vector<SharedMissionEvent> result;
	result.swap(missionEvents);
	return result;
}



vector<CoOpRelay::SharedResourceEvent> CoOpRelay::ClientSession::TakeResourceEvents()
{
	vector<SharedResourceEvent> result;
	result.swap(resourceEvents);
	return result;
}



bool CoOpRelay::ClientSession::IsSystemAuthority(const string &system) const
{
	return authorities.IsOwner(system, playerId);
}



const vector<CoOpRelay::PlayerEvent> &CoOpRelay::ClientSession::RecentEvents() const noexcept
{
	return recentEvents;
}



const vector<string> &CoOpRelay::ClientSession::DesyncWarnings() const noexcept
{
	return desyncWarnings;
}



CoOpRelay::Diagnostics CoOpRelay::ClientSession::GetDiagnostics() const
{
	Diagnostics diagnostics;
	diagnostics.connectedPlayers = state == ConnectionState::CONNECTED ? remotes.All().size() + 1 : 0;
	diagnostics.playerId = playerId;
	diagnostics.remotePlayerProxies = remotes.All().size();
	diagnostics.npcProxies = sharedNPCs.All().size();
	diagnostics.staleProxyCount = staleProxyCount;
	diagnostics.duplicatePreventionCount = duplicatePreventionCount;
	if(!remoteSnapshotActivity.empty())
	{
		uint64_t newestSeenStep = 0;
		for(const RemoteSnapshotActivity &activity : remoteSnapshotActivity)
			newestSeenStep = max(newestSeenStep, activity.lastSeenStep);
		diagnostics.lastSnapshotAgeSteps = connectionStep >= newestSeenStep ? connectionStep - newestSeenStep : 0;
	}
	return diagnostics;
}



CoOpRelay::LocalStateEmitter &CoOpRelay::ClientSession::Emitter() noexcept
{
	return emitter;
}



const CoOpRelay::LocalStateEmitter &CoOpRelay::ClientSession::Emitter() const noexcept
{
	return emitter;
}



string CoOpRelay::ClientSession::JoinLine(const string &playerName, const string &password)
{
	string line = MESSAGE_JOIN + "\t" + EscapeField(playerName);
	if(!password.empty())
		line += "\t" + EscapeField(password);
	return line;
}



void CoOpRelay::ClientSession::RecordDesyncWarning(string warning)
{
	if(warning.empty())
		return;
	desyncWarnings.push_back(std::move(warning));
	if(desyncWarnings.size() > MAX_SESSION_EVENTS)
		desyncWarnings.erase(desyncWarnings.begin());
}



void CoOpRelay::ClientSession::NoteRemoteSnapshot(const PlayerSnapshot &snapshot)
{
	auto it = find_if(remoteSnapshotActivity.begin(), remoteSnapshotActivity.end(),
		[&snapshot](const RemoteSnapshotActivity &activity) {
			return activity.playerId == snapshot.playerId;
		});
	if(it == remoteSnapshotActivity.end())
	{
		remoteSnapshotActivity.push_back({snapshot.playerId, connectionStep, snapshot.sequence, false});
		return;
	}

	it->lastSeenStep = connectionStep;
	it->latestSequence = snapshot.sequence;
	it->staleWarningSent = false;
}



bool CoOpRelay::ClientSession::AcceptEvent(const PlayerEvent &event)
{
	auto it = find_if(eventSequences.begin(), eventSequences.end(), [&event](const EventSequence &sequence) {
		return sequence.playerId == event.playerId;
	});
	if(it == eventSequences.end())
	{
		eventSequences.push_back({event.playerId, event.sequence});
		return true;
	}
	if(event.sequence <= it->latestSequence)
		return false;
	it->latestSequence = event.sequence;
	return true;
}



bool CoOpRelay::ClientSession::AcceptNPCDamageReport(const SharedNPCDamage &damage, bool *exactDuplicate)
{
	if(exactDuplicate)
		*exactDuplicate = false;
	auto it = find_if(npcDamageSequences.begin(), npcDamageSequences.end(),
		[&damage](const DamageReportSequence &entry) {
			return entry.reporterId == damage.reporterId;
		});
	if(it == npcDamageSequences.end())
	{
		npcDamageSequences.push_back({damage.reporterId, damage.sequence});
		return true;
	}
	if(exactDuplicate && damage.sequence == it->latestSequence)
		*exactDuplicate = true;
	if(damage.sequence <= it->latestSequence)
		return false;

	it->latestSequence = damage.sequence;
	return true;
}



bool CoOpRelay::ClientSession::AcceptCombatHit(const SharedCombatHit &hit, bool *exactDuplicate)
{
	if(exactDuplicate)
		*exactDuplicate = false;
	auto it = find_if(combatHitSequences.begin(), combatHitSequences.end(),
		[&hit](const CombatHitSequence &entry) {
			return entry.attackerId == hit.attackerId;
		});
	if(it == combatHitSequences.end())
	{
		combatHitSequences.push_back({hit.attackerId, hit.sequence});
		return true;
	}
	if(exactDuplicate && hit.sequence == it->latestSequence)
		*exactDuplicate = true;
	if(hit.sequence <= it->latestSequence)
		return false;

	it->latestSequence = hit.sequence;
	return true;
}



bool CoOpRelay::ClientSession::AcceptWeaponFire(const SharedWeaponFire &fire, bool *exactDuplicate)
{
	if(exactDuplicate)
		*exactDuplicate = false;
	auto it = find_if(weaponFireSequences.begin(), weaponFireSequences.end(),
		[&fire](const WeaponFireSequence &entry) {
			return entry.playerId == fire.playerId;
		});
	if(it == weaponFireSequences.end())
	{
		weaponFireSequences.push_back({fire.playerId, fire.sequence});
		return true;
	}
	if(exactDuplicate && fire.sequence == it->latestSequence)
		*exactDuplicate = true;
	if(fire.sequence <= it->latestSequence)
		return false;

	it->latestSequence = fire.sequence;
	return true;
}



bool CoOpRelay::ClientSession::AcceptNPCBoardingReport(const SharedNPCBoarding &boarding, bool *exactDuplicate)
{
	if(exactDuplicate)
		*exactDuplicate = false;
	const bool isRequest = boarding.IsRequest();
	const string &sourceId = isRequest ? boarding.playerId : boarding.ownerId;
	auto it = find_if(npcBoardingSequences.begin(), npcBoardingSequences.end(),
		[&sourceId, isRequest](const BoardingReportSequence &entry) {
			return entry.sourceId == sourceId && entry.isRequest == isRequest;
		});
	if(it == npcBoardingSequences.end())
	{
		npcBoardingSequences.push_back({sourceId, isRequest, boarding.sequence});
		return true;
	}
	if(exactDuplicate && boarding.sequence == it->latestSequence)
		*exactDuplicate = true;
	if(boarding.sequence <= it->latestSequence)
		return false;

	it->latestSequence = boarding.sequence;
	return true;
}



string CoOpRelay::Serialize(const PlayerSnapshot &snapshot)
{
	ostringstream out;
	out.precision(17);
	out << MESSAGE_SNAPSHOT;
	AppendField(out, snapshot.protocolVersion);
	AppendField(out, snapshot.sequence);
	AppendField(out, snapshot.playerId);
	AppendField(out, snapshot.name);
	AppendField(out, snapshot.system);
	AppendField(out, snapshot.position.X());
	AppendField(out, snapshot.position.Y());
	AppendField(out, snapshot.velocity.X());
	AppendField(out, snapshot.velocity.Y());
	AppendField(out, snapshot.facing.Degrees());
	AppendField(out, snapshot.landedPlanet);
	AppendField(out, snapshot.shipModel);
	AppendField(out, snapshot.loadoutHash);
	AppendField(out, snapshot.shields);
	AppendField(out, snapshot.hull);
	AppendField(out, snapshot.fuel);
	AppendField(out, snapshot.energy);
	AppendField(out, snapshot.heat);
	AppendField(out, snapshot.simulationActive ? 1 : 0);
	return out.str();
}



optional<CoOpRelay::PlayerSnapshot> CoOpRelay::ParseSnapshot(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 20 || fields[0] != MESSAGE_SNAPSHOT)
		return nullopt;

	PlayerSnapshot snapshot;
	if(!ParseNumber(fields[1], snapshot.protocolVersion)
			|| !ParseNumber(fields[2], snapshot.sequence)
			|| !ParseNumber(fields[6], snapshot.position.X())
			|| !ParseNumber(fields[7], snapshot.position.Y())
			|| !ParseNumber(fields[8], snapshot.velocity.X())
			|| !ParseNumber(fields[9], snapshot.velocity.Y()))
		return nullopt;

	double facing = 0.;
	if(!ParseNumber(fields[10], facing)
			|| !ParseNumber(fields[13], snapshot.loadoutHash)
			|| !ParseNumber(fields[14], snapshot.shields)
			|| !ParseNumber(fields[15], snapshot.hull)
			|| !ParseNumber(fields[16], snapshot.fuel)
			|| !ParseNumber(fields[17], snapshot.energy)
			|| !ParseNumber(fields[18], snapshot.heat))
		return nullopt;

	unsigned simulationActive = 0;
	if(!ParseNumber(fields[19], simulationActive))
		return nullopt;

	snapshot.playerId = fields[3];
	snapshot.name = fields[4];
	snapshot.system = fields[5];
	snapshot.facing = Angle(facing);
	snapshot.landedPlanet = fields[11];
	snapshot.shipModel = fields[12];
	snapshot.simulationActive = simulationActive;
	if(!snapshot.IsCompatible())
		return nullopt;
	return snapshot;
}



string CoOpRelay::Serialize(const PlayerEvent &event)
{
	ostringstream out;
	out << MESSAGE_EVENT;
	AppendField(out, event.protocolVersion);
	AppendField(out, event.sequence);
	AppendField(out, event.playerId);
	AppendField(out, string(ToString(event.type)));
	AppendField(out, event.system);
	AppendField(out, event.planet);
	AppendField(out, event.detail);
	return out.str();
}



optional<CoOpRelay::PlayerEvent> CoOpRelay::ParseEvent(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 8 || fields[0] != MESSAGE_EVENT)
		return nullopt;

	PlayerEvent event;
	if(!ParseNumber(fields[1], event.protocolVersion) || !ParseNumber(fields[2], event.sequence))
		return nullopt;
	auto type = EventTypeFromString(fields[4]);
	if(!type)
		return nullopt;

	event.playerId = fields[3];
	event.type = *type;
	event.system = fields[5];
	event.planet = fields[6];
	event.detail = fields[7];
	if(!event.IsCompatible())
		return nullopt;
	return event;
}



string CoOpRelay::Serialize(const SystemAuthority &authority)
{
	ostringstream out;
	out << MESSAGE_AUTHORITY;
	AppendField(out, authority.protocolVersion);
	AppendField(out, authority.sequence);
	AppendField(out, authority.system);
	AppendField(out, authority.ownerId);
	AppendField(out, authority.playerCount);
	return out.str();
}



optional<CoOpRelay::SystemAuthority> CoOpRelay::ParseAuthority(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 6 || fields[0] != MESSAGE_AUTHORITY)
		return nullopt;

	SystemAuthority authority;
	if(!ParseNumber(fields[1], authority.protocolVersion)
			|| !ParseNumber(fields[2], authority.sequence)
			|| !ParseNumber(fields[5], authority.playerCount))
		return nullopt;

	authority.system = fields[3];
	authority.ownerId = fields[4];
	if(!authority.IsCompatible())
		return nullopt;
	return authority;
}



string CoOpRelay::Serialize(const PeerEndpoint &endpoint)
{
	ostringstream out;
	out << MESSAGE_PEER_ENDPOINT;
	AppendField(out, endpoint.protocolVersion);
	AppendField(out, endpoint.sequence);
	AppendField(out, endpoint.playerId);
	AppendField(out, endpoint.host);
	AppendField(out, endpoint.port);
	AppendField(out, endpoint.removed ? 1 : 0);
	return out.str();
}



optional<CoOpRelay::PeerEndpoint> CoOpRelay::ParsePeerEndpoint(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 7 || fields[0] != MESSAGE_PEER_ENDPOINT)
		return nullopt;

	PeerEndpoint endpoint;
	unsigned port = 0;
	unsigned removed = 0;
	if(!ParseNumber(fields[1], endpoint.protocolVersion)
			|| !ParseNumber(fields[2], endpoint.sequence)
			|| !ParseNumber(fields[5], port)
			|| !ParseNumber(fields[6], removed)
			|| port > 65535 || removed > 1)
		return nullopt;

	endpoint.playerId = fields[3];
	endpoint.host = fields[4];
	endpoint.port = static_cast<uint16_t>(port);
	endpoint.removed = removed;
	if(!endpoint.IsValid())
		return nullopt;
	return endpoint;
}



string CoOpRelay::Serialize(const SharedNPCSnapshot &snapshot)
{
	ostringstream out;
	out.precision(17);
	out << MESSAGE_NPC;
	AppendField(out, snapshot.protocolVersion);
	AppendField(out, snapshot.sequence);
	AppendField(out, snapshot.npcId);
	AppendField(out, snapshot.ownerId);
	AppendField(out, snapshot.system);
	AppendField(out, snapshot.position.X());
	AppendField(out, snapshot.position.Y());
	AppendField(out, snapshot.velocity.X());
	AppendField(out, snapshot.velocity.Y());
	AppendField(out, snapshot.facing.Degrees());
	AppendField(out, snapshot.shipModel);
	AppendField(out, snapshot.government);
	AppendField(out, snapshot.targetId);
	AppendField(out, snapshot.shields);
	AppendField(out, snapshot.hull);
	AppendField(out, snapshot.fuel);
	AppendField(out, snapshot.energy);
	AppendField(out, snapshot.heat);
	AppendField(out, snapshot.disabled ? 1 : 0);
	AppendField(out, snapshot.destroyed ? 1 : 0);
	AppendField(out, snapshot.captured ? 1 : 0);
	AppendField(out, snapshot.removed ? 1 : 0);
	return out.str();
}



optional<CoOpRelay::SharedNPCSnapshot> CoOpRelay::ParseNPCSnapshot(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 23 || fields[0] != MESSAGE_NPC)
		return nullopt;

	SharedNPCSnapshot snapshot;
	if(!ParseNumber(fields[1], snapshot.protocolVersion)
			|| !ParseNumber(fields[2], snapshot.sequence)
			|| !ParseNumber(fields[6], snapshot.position.X())
			|| !ParseNumber(fields[7], snapshot.position.Y())
			|| !ParseNumber(fields[8], snapshot.velocity.X())
			|| !ParseNumber(fields[9], snapshot.velocity.Y()))
		return nullopt;

	double facing = 0.;
	if(!ParseNumber(fields[10], facing)
			|| !ParseNumber(fields[14], snapshot.shields)
			|| !ParseNumber(fields[15], snapshot.hull)
			|| !ParseNumber(fields[16], snapshot.fuel)
			|| !ParseNumber(fields[17], snapshot.energy)
			|| !ParseNumber(fields[18], snapshot.heat))
		return nullopt;

	unsigned disabled = 0;
	unsigned destroyed = 0;
	unsigned captured = 0;
	unsigned removed = 0;
	if(!ParseNumber(fields[19], disabled)
			|| !ParseNumber(fields[20], destroyed)
			|| !ParseNumber(fields[21], captured)
			|| !ParseNumber(fields[22], removed))
		return nullopt;

	snapshot.npcId = fields[3];
	snapshot.ownerId = fields[4];
	snapshot.system = fields[5];
	snapshot.facing = Angle(facing);
	snapshot.shipModel = fields[11];
	snapshot.government = fields[12];
	snapshot.targetId = fields[13];
	snapshot.disabled = disabled;
	snapshot.destroyed = destroyed;
	snapshot.captured = captured;
	snapshot.removed = removed;
	if(!snapshot.IsValid())
		return nullopt;
	return snapshot;
}



string CoOpRelay::Serialize(const SharedNPCDamage &damage)
{
	ostringstream out;
	out.precision(17);
	out << MESSAGE_NPC_DAMAGE;
	AppendField(out, damage.protocolVersion);
	AppendField(out, damage.sequence);
	AppendField(out, damage.npcId);
	AppendField(out, damage.ownerId);
	AppendField(out, damage.system);
	AppendField(out, damage.reporterId);
	AppendField(out, damage.shieldDamage);
	AppendField(out, damage.hullDamage);
	AppendField(out, damage.fuelDamage);
	AppendField(out, damage.energyDamage);
	AppendField(out, damage.heatDamage);
	AppendField(out, damage.disabled ? 1 : 0);
	AppendField(out, damage.destroyed ? 1 : 0);
	return out.str();
}



optional<CoOpRelay::SharedNPCDamage> CoOpRelay::ParseNPCDamage(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 14 || fields[0] != MESSAGE_NPC_DAMAGE)
		return nullopt;

	SharedNPCDamage damage;
	if(!ParseNumber(fields[1], damage.protocolVersion)
			|| !ParseNumber(fields[2], damage.sequence)
			|| !ParseNumber(fields[7], damage.shieldDamage)
			|| !ParseNumber(fields[8], damage.hullDamage)
			|| !ParseNumber(fields[9], damage.fuelDamage)
			|| !ParseNumber(fields[10], damage.energyDamage)
			|| !ParseNumber(fields[11], damage.heatDamage))
		return nullopt;

	unsigned disabled = 0;
	unsigned destroyed = 0;
	if(!ParseNumber(fields[12], disabled) || !ParseNumber(fields[13], destroyed))
		return nullopt;

	damage.npcId = fields[3];
	damage.ownerId = fields[4];
	damage.system = fields[5];
	damage.reporterId = fields[6];
	damage.disabled = disabled;
	damage.destroyed = destroyed;
	if(!damage.IsValid())
		return nullopt;
	return damage;
}



string CoOpRelay::Serialize(const SharedCombatHit &hit)
{
	ostringstream out;
	out.precision(17);
	out << MESSAGE_COMBAT_HIT;
	AppendField(out, hit.protocolVersion);
	AppendField(out, hit.sequence);
	AppendField(out, hit.attackerId);
	AppendField(out, hit.targetPlayerId);
	AppendField(out, hit.system);
	AppendField(out, hit.attackerModel);
	AppendField(out, hit.attackerGovernment);
	AppendField(out, hit.shieldDamage);
	AppendField(out, hit.hullDamage);
	AppendField(out, hit.fuelDamage);
	AppendField(out, hit.energyDamage);
	AppendField(out, hit.heatDamage);
	AppendField(out, hit.disabled ? 1 : 0);
	AppendField(out, hit.destroyed ? 1 : 0);
	AppendField(out, hit.detail);
	AppendField(out, hit.weapon);
	AppendField(out, hit.impactPosition.X());
	AppendField(out, hit.impactPosition.Y());
	AppendField(out, hit.hitVelocity.X());
	AppendField(out, hit.hitVelocity.Y());
	AppendField(out, hit.facing);
	return out.str();
}



optional<CoOpRelay::SharedCombatHit> CoOpRelay::ParseCombatHit(const string &message)
{
	vector<string> fields = SplitFields(message);
	if((fields.size() != 16 && fields.size() != 22) || fields[0] != MESSAGE_COMBAT_HIT)
		return nullopt;

	SharedCombatHit hit;
	if(!ParseNumber(fields[1], hit.protocolVersion)
			|| !ParseNumber(fields[2], hit.sequence)
			|| !ParseNumber(fields[8], hit.shieldDamage)
			|| !ParseNumber(fields[9], hit.hullDamage)
			|| !ParseNumber(fields[10], hit.fuelDamage)
			|| !ParseNumber(fields[11], hit.energyDamage)
			|| !ParseNumber(fields[12], hit.heatDamage))
		return nullopt;

	unsigned disabled = 0;
	unsigned destroyed = 0;
	if(!ParseNumber(fields[13], disabled) || !ParseNumber(fields[14], destroyed))
		return nullopt;

	hit.attackerId = fields[3];
	hit.targetPlayerId = fields[4];
	hit.system = fields[5];
	hit.attackerModel = fields[6];
	hit.attackerGovernment = fields[7];
	hit.disabled = disabled;
	hit.destroyed = destroyed;
	hit.detail = fields[15];
	if(fields.size() == 22)
	{
		double impactX = 0.;
		double impactY = 0.;
		double velocityX = 0.;
		double velocityY = 0.;
		if(!ParseNumber(fields[17], impactX)
				|| !ParseNumber(fields[18], impactY)
				|| !ParseNumber(fields[19], velocityX)
				|| !ParseNumber(fields[20], velocityY)
				|| !ParseNumber(fields[21], hit.facing))
			return nullopt;
		hit.weapon = fields[16];
		hit.impactPosition = Point(impactX, impactY);
		hit.hitVelocity = Point(velocityX, velocityY);
	}
	if(!hit.IsValid())
		return nullopt;
	return hit;
}



string CoOpRelay::Serialize(const SharedWeaponFire &fire)
{
	ostringstream out;
	out.precision(17);
	out << MESSAGE_WEAPON_FIRE;
	AppendField(out, fire.protocolVersion);
	AppendField(out, fire.sequence);
	AppendField(out, fire.playerId);
	AppendField(out, fire.system);
	AppendField(out, fire.from.X());
	AppendField(out, fire.from.Y());
	AppendField(out, fire.to.X());
	AppendField(out, fire.to.Y());
	AppendField(out, fire.weapon);
	AppendField(out, fire.velocity.X());
	AppendField(out, fire.velocity.Y());
	AppendField(out, fire.facing);
	AppendField(out, fire.shipPosition.X());
	AppendField(out, fire.shipPosition.Y());
	AppendField(out, fire.targetPlayerId);
	AppendField(out, fire.shipVelocity.X());
	AppendField(out, fire.shipVelocity.Y());
	AppendField(out, fire.targetNPCId);
	return out.str();
}



optional<CoOpRelay::SharedWeaponFire> CoOpRelay::ParseWeaponFire(const string &message)
{
	vector<string> fields = SplitFields(message);
	if((fields.size() != 10 && fields.size() != 16 && fields.size() != 18 && fields.size() != 19)
			|| fields[0] != MESSAGE_WEAPON_FIRE)
		return nullopt;

	SharedWeaponFire fire;
	double fromX = 0.;
	double fromY = 0.;
	double toX = 0.;
	double toY = 0.;
	double velocityX = 0.;
	double velocityY = 0.;
	double shipX = 0.;
	double shipY = 0.;
	double shipVelocityX = 0.;
	double shipVelocityY = 0.;
	if(!ParseNumber(fields[1], fire.protocolVersion)
			|| !ParseNumber(fields[2], fire.sequence)
			|| !ParseNumber(fields[5], fromX)
			|| !ParseNumber(fields[6], fromY)
			|| !ParseNumber(fields[7], toX)
			|| !ParseNumber(fields[8], toY))
		return nullopt;

	fire.playerId = fields[3];
	fire.system = fields[4];
	fire.from = Point(fromX, fromY);
	fire.to = Point(toX, toY);
	fire.weapon = fields[9];
	fire.velocity = fire.to - fire.from;
	fire.facing = Angle(fire.to - fire.from).Degrees();
	fire.shipPosition = fire.from;
	if(fields.size() >= 16)
	{
		if(!ParseNumber(fields[10], velocityX)
				|| !ParseNumber(fields[11], velocityY)
				|| !ParseNumber(fields[12], fire.facing)
				|| !ParseNumber(fields[13], shipX)
				|| !ParseNumber(fields[14], shipY))
			return nullopt;
		fire.velocity = Point(velocityX, velocityY);
		fire.shipPosition = Point(shipX, shipY);
		fire.targetPlayerId = fields[15];
	}
	if(fields.size() == 18)
	{
		if(!ParseNumber(fields[16], shipVelocityX)
				|| !ParseNumber(fields[17], shipVelocityY))
			return nullopt;
		fire.shipVelocity = Point(shipVelocityX, shipVelocityY);
		fire.hasShipVelocity = true;
	}
	if(fields.size() == 19)
	{
		if(!ParseNumber(fields[16], shipVelocityX)
				|| !ParseNumber(fields[17], shipVelocityY))
			return nullopt;
		fire.shipVelocity = Point(shipVelocityX, shipVelocityY);
		fire.hasShipVelocity = true;
		fire.targetNPCId = fields[18];
	}
	if(!fire.IsValid())
		return nullopt;
	return fire;
}



string CoOpRelay::Serialize(const SharedNPCBoarding &boarding)
{
	ostringstream out;
	out.precision(17);
	out << MESSAGE_NPC_BOARDING;
	AppendField(out, boarding.protocolVersion);
	AppendField(out, boarding.sequence);
	AppendField(out, boarding.npcId);
	AppendField(out, boarding.playerId);
	AppendField(out, boarding.ownerId);
	AppendField(out, boarding.system);
	AppendField(out, string(BoardingActionName(boarding.action)));
	AppendField(out, boarding.detail);
	return out.str();
}



optional<CoOpRelay::SharedNPCBoarding> CoOpRelay::ParseNPCBoarding(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 9 || fields[0] != MESSAGE_NPC_BOARDING)
		return nullopt;

	SharedNPCBoarding boarding;
	if(!ParseNumber(fields[1], boarding.protocolVersion)
			|| !ParseNumber(fields[2], boarding.sequence))
		return nullopt;

	auto action = BoardingActionFromString(fields[7]);
	if(!action)
		return nullopt;

	boarding.npcId = fields[3];
	boarding.playerId = fields[4];
	boarding.ownerId = fields[5];
	boarding.system = fields[6];
	boarding.action = *action;
	boarding.detail = fields[8];
	if(!boarding.IsValid())
		return nullopt;
	return boarding;
}



string CoOpRelay::Serialize(const SharedMissionEvent &event)
{
	ostringstream out;
	out.precision(17);
	out << MESSAGE_MISSION_EVENT;
	AppendField(out, event.protocolVersion);
	AppendField(out, event.sequence);
	AppendField(out, event.missionId);
	AppendField(out, event.instanceId);
	AppendField(out, event.playerId);
	AppendField(out, string(MissionEventTypeName(event.type)));
	AppendField(out, event.system);
	AppendField(out, event.npcId);
	AppendField(out, event.detail);
	AppendField(out, event.credits);
	return out.str();
}



optional<CoOpRelay::SharedMissionEvent> CoOpRelay::ParseMissionEvent(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 11 || fields[0] != MESSAGE_MISSION_EVENT)
		return nullopt;

	SharedMissionEvent event;
	if(!ParseNumber(fields[1], event.protocolVersion)
			|| !ParseNumber(fields[2], event.sequence)
			|| !ParseNumber(fields[10], event.credits))
		return nullopt;

	auto type = MissionEventTypeFromString(fields[6]);
	if(!type)
		return nullopt;

	event.missionId = fields[3];
	event.instanceId = fields[4];
	event.playerId = fields[5];
	event.type = *type;
	event.system = fields[7];
	event.npcId = fields[8];
	event.detail = fields[9];
	if(!event.IsValid())
		return nullopt;
	return event;
}



string CoOpRelay::Serialize(const SharedResourceEvent &event)
{
	ostringstream out;
	out.precision(17);
	out << MESSAGE_RESOURCE_EVENT;
	AppendField(out, event.protocolVersion);
	AppendField(out, event.sequence);
	AppendField(out, event.actionId);
	AppendField(out, event.playerId);
	AppendField(out, event.targetPlayerId);
	AppendField(out, string(ResourceActionTypeName(event.type)));
	AppendField(out, string(ResourceActionStatusName(event.status)));
	AppendField(out, event.resource);
	AppendField(out, event.amount);
	AppendField(out, event.detail);
	return out.str();
}



optional<CoOpRelay::SharedResourceEvent> CoOpRelay::ParseResourceEvent(const string &message)
{
	vector<string> fields = SplitFields(message);
	if(fields.size() != 11 || fields[0] != MESSAGE_RESOURCE_EVENT)
		return nullopt;

	SharedResourceEvent event;
	if(!ParseNumber(fields[1], event.protocolVersion)
			|| !ParseNumber(fields[2], event.sequence)
			|| !ParseNumber(fields[9], event.amount))
		return nullopt;

	auto type = ResourceActionTypeFromString(fields[6]);
	auto status = ResourceActionStatusFromString(fields[7]);
	if(!type || !status)
		return nullopt;

	event.actionId = fields[3];
	event.playerId = fields[4];
	event.targetPlayerId = fields[5];
	event.type = *type;
	event.status = *status;
	event.resource = fields[8];
	event.detail = fields[10];
	if(!event.IsValid())
		return nullopt;
	return event;
}



optional<string> CoOpRelay::ParseJoinName(const string &message)
{
	auto request = ParseJoinRequest(message);
	if(!request)
		return nullopt;
	return request->playerName;
}



optional<CoOpRelay::JoinRequest> CoOpRelay::ParseJoinRequest(const string &message)
{
	vector<string> fields = SplitFields(message);
	if((fields.size() != 2 && fields.size() != 3) || fields[0] != MESSAGE_JOIN || fields[1].empty())
		return nullopt;

	JoinRequest request;
	request.playerName = fields[1];
	if(fields.size() == 3)
		request.password = fields[2];
	return request;
}



CoOpRelay::RelayEndpoint CoOpRelay::ParseEndpoint(string address, uint16_t defaultPort)
{
	RelayEndpoint endpoint;
	endpoint.host = Trim(std::move(address));
	endpoint.port = defaultPort;

	size_t colon = endpoint.host.find(':');
	if(colon != string::npos && endpoint.host.find(':', colon + 1) == string::npos)
	{
		string portString = endpoint.host.substr(colon + 1);
		endpoint.host = endpoint.host.substr(0, colon);

		unsigned parsedPort = 0;
		auto [ptr, ec] = from_chars(portString.data(), portString.data() + portString.size(), parsedPort);
		if(ec == errc() && ptr == portString.data() + portString.size() && parsedPort <= 65535)
			endpoint.port = static_cast<uint16_t>(parsedPort);
	}

	if(endpoint.host.empty())
		endpoint.host = "127.0.0.1";
	return endpoint;
}



const char *CoOpRelay::ToString(EventType type) noexcept
{
	switch(type)
	{
		case EventType::LAUNCHED:
			return "launched";
		case EventType::LANDED:
			return "landed";
		case EventType::JUMPED:
			return "jumped";
		case EventType::RESOURCES_CHANGED:
			return "resources_changed";
		case EventType::SHIP_CHANGED:
			return "ship_changed";
		case EventType::DISABLED:
			return "disabled";
		case EventType::DESTROYED:
			return "destroyed";
	}
	return "resources_changed";
}



optional<CoOpRelay::EventType> CoOpRelay::EventTypeFromString(const string &type)
{
	if(type == "launched")
		return EventType::LAUNCHED;
	if(type == "landed")
		return EventType::LANDED;
	if(type == "jumped")
		return EventType::JUMPED;
	if(type == "resources_changed")
		return EventType::RESOURCES_CHANGED;
	if(type == "ship_changed")
		return EventType::SHIP_CHANGED;
	if(type == "disabled")
		return EventType::DISABLED;
	if(type == "destroyed")
		return EventType::DESTROYED;
	return nullopt;
}



optional<CoOpRelay::PlayerSnapshot> CoOpRelay::SnapshotFromPlayer(const PlayerInfo &player,
	const string &playerId, uint64_t sequence)
{
	if(playerId.empty() || !player.IsLoaded() || !player.GetSystem())
		return nullopt;

	PlayerSnapshot snapshot;
	snapshot.sequence = sequence;
	snapshot.playerId = playerId;
	snapshot.name = PlayerName(player);
	snapshot.system = player.GetSystem()->TrueName();

	const Planet *planet = player.GetPlanet();
	if(planet)
		snapshot.landedPlanet = planet->TrueName();

	const Ship *flagship = player.Flagship();
	if(flagship)
	{
		snapshot.position = flagship->Position();
		snapshot.velocity = flagship->Velocity();
		snapshot.facing = flagship->Facing();
		snapshot.shipModel = flagship->TrueModelName();
		snapshot.loadoutHash = LoadoutHash(*flagship);
		snapshot.shields = flagship->Shields();
		snapshot.hull = flagship->Hull();
		snapshot.fuel = flagship->Fuel();
		snapshot.energy = flagship->Energy();
		snapshot.heat = flagship->Heat();
	}

	return snapshot;
}
