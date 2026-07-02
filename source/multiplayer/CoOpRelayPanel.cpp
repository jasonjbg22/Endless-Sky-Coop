/* CoOpRelayPanel.cpp
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

#include "CoOpRelayPanel.h"

#include "CoOpRelayController.h"

#include "../Color.h"
#include "../Command.h"
#include "../DialogPanel.h"
#include "../GameData.h"
#include "../PlayerInfo.h"
#include "../Planet.h"
#include "../Rectangle.h"
#include "../Screen.h"
#include "../System.h"
#include "../UI.h"
#include "../shader/FillShader.h"
#include "../text/Font.h"
#include "../text/FontSet.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace std;

namespace {
	string PilotLabel(const PlayerInfo &player)
	{
		if(!player.IsLoaded())
			return "No pilot loaded";

		string name = player.FirstName();
		if(!player.LastName().empty())
		{
			if(!name.empty())
				name += ' ';
			name += player.LastName();
		}
		return name.empty() ? "Player" : name;
	}



	string PlayerName(const PlayerInfo &player)
	{
		return player.IsLoaded() ? PilotLabel(player) : "Player";
	}



	string CleanRoomName(string name)
	{
		for(char &ch : name)
			if(ch == '\t' || ch == '\r' || ch == '\n')
				ch = ' ';

		const size_t first = name.find_first_not_of(' ');
		if(first == string::npos)
			return "Co-op Room";
		const size_t last = name.find_last_not_of(' ');
		name = name.substr(first, last - first + 1);
		if(name.size() > 48)
			name.resize(48);
		return name;
	}



	string CleanPassword(string password)
	{
		for(char &ch : password)
			if(ch == '\t' || ch == '\r' || ch == '\n')
				ch = ' ';

		const size_t first = password.find_first_not_of(' ');
		if(first == string::npos)
			return {};
		const size_t last = password.find_last_not_of(' ');
		password = password.substr(first, last - first + 1);
		if(password.size() > 64)
			password.resize(64);
		return password;
	}



	bool ParsePort(string port, uint16_t &result)
	{
		for(char &ch : port)
			if(ch == '\t' || ch == '\r' || ch == '\n')
				ch = ' ';

		const size_t first = port.find_first_not_of(' ');
		if(first == string::npos)
			return false;
		const size_t last = port.find_last_not_of(' ');
		port = port.substr(first, last - first + 1);

		try
		{
			size_t parsed = 0;
			const unsigned long value = stoul(port, &parsed);
			if(parsed == port.size() && value && value <= 65535)
			{
				result = static_cast<uint16_t>(value);
				return true;
			}
		}
		catch(...)
		{
		}
		return false;
	}



	string DefaultRoomName(const PlayerInfo &player)
	{
		if(!player.IsLoaded())
			return "Co-op Room";
		return CleanRoomName(PilotLabel(player) + "'s Room");
	}



	string FitText(const Font &font, string text, double maxWidth)
	{
		if(font.Width(text) <= maxWidth)
			return text;
		while(!text.empty() && font.Width(text + "...") > maxWidth)
			text.pop_back();
		return text.empty() ? "..." : text + "...";
	}



	string LocalStatus(const PlayerInfo &player)
	{
		string text = PilotLabel(player) + " (you)";
		if(player.GetSystem())
			text += " - " + player.GetSystem()->TrueName();
		if(player.GetPlanet())
			text += " / " + player.GetPlanet()->TrueName();
		return text;
	}



	string PasswordStatus(const string &roomPassword)
	{
		return string("Password: ") + (roomPassword.empty() ? "not set" : "set");
	}



	string PortStatus(uint16_t roomPort)
	{
		return "Port: " + to_string(roomPort);
	}



	string RateText(double rate)
	{
		return to_string(static_cast<int>(rate + .5));
	}



	string LatencyText(int latencyMs)
	{
		return latencyMs >= 0 ? to_string(latencyMs) + " ms" : "n/a";
	}



	string RemoteName(const string &playerId, const CoOpRelay::PresenceStore &remotes)
	{
		const CoOpRelay::RemotePresence *presence = remotes.Get(playerId);
		if(presence && !presence->latest.name.empty())
			return presence->latest.name;
		return playerId;
	}



	string ResourceTargetLabel(const string &targetPlayerId, const CoOpRelay::PresenceStore &remotes)
	{
		return targetPlayerId.empty() ? "all players" : RemoteName(targetPlayerId, remotes);
	}



	string PendingResourceLabel(const CoOpRelay::SharedResourceEvent &event, const CoOpRelay::PresenceStore &remotes)
	{
		const string resource = event.resource.empty() ? "resource" : event.resource;
		return RemoteName(event.playerId, remotes) + " requests " + resource;
	}



	string AuthorityStatus(const PlayerInfo &player, const CoOpRelay::AuthorityStore &authorities,
		const CoOpRelay::PresenceStore &remotes, bool isLocalAuthority)
	{
		if(!player.GetSystem())
			return "Authority: no system";

		const string system = player.GetSystem()->TrueName();
		const CoOpRelay::SystemAuthority *authority = authorities.Get(system);
		if(!authority)
			return "Authority: pending for " + system;

		string owner = isLocalAuthority ? "you" : RemoteName(authority->ownerId, remotes);
		return "Authority: " + owner + " owns " + system + " (" + to_string(authority->playerCount) + ")";
	}



	string EventText(const CoOpRelay::PlayerEvent &event, const CoOpRelay::PresenceStore &remotes)
	{
		string name = RemoteName(event.playerId, remotes);
		switch(event.type)
		{
			case CoOpRelay::EventType::LAUNCHED:
				return name + " launched" + (event.detail.empty() ? "" : " from " + event.detail);
			case CoOpRelay::EventType::LANDED:
				return name + " landed" + (event.planet.empty() ? "" : " on " + event.planet);
			case CoOpRelay::EventType::JUMPED:
				return name + " jumped from " + event.detail + " to " + event.system;
			case CoOpRelay::EventType::RESOURCES_CHANGED:
				return name + " updated fuel/resources";
			case CoOpRelay::EventType::SHIP_CHANGED:
				return name + " changed ship/loadout";
			case CoOpRelay::EventType::DISABLED:
				return name + " was disabled";
			case CoOpRelay::EventType::DESTROYED:
				return name + " was destroyed";
		}
		return name + " updated status";
	}



	string MissionTypeText(CoOpRelay::MissionEventType type)
	{
		switch(type)
		{
			case CoOpRelay::MissionEventType::ACCEPTED:
				return "accepted";
			case CoOpRelay::MissionEventType::OBJECTIVE_UPDATED:
				return "objective updated";
			case CoOpRelay::MissionEventType::NPC_DISABLED:
				return "NPC disabled";
			case CoOpRelay::MissionEventType::NPC_DESTROYED:
				return "NPC destroyed";
			case CoOpRelay::MissionEventType::NPC_BOARDED:
				return "NPC boarded";
			case CoOpRelay::MissionEventType::NPC_CAPTURED:
				return "NPC captured";
			case CoOpRelay::MissionEventType::COMPLETED:
				return "completed";
			case CoOpRelay::MissionEventType::FAILED:
				return "failed";
			case CoOpRelay::MissionEventType::REWARD:
				return "reward";
		}
		return "updated";
	}



	string MissionStateText(const vector<CoOpRelay::SharedMissionEvent> &events)
	{
		if(events.empty())
			return "Shared missions: none";

		const CoOpRelay::SharedMissionEvent &event = events.back();
		string text = "Shared missions: " + to_string(events.size()) + " - " + MissionTypeText(event.type);
		if(!event.detail.empty())
			text += ": " + event.detail;
		else if(!event.npcId.empty())
			text += " - " + event.npcId;
		return text;
	}
}



CoOpRelayPanel::CoOpRelayPanel(PlayerInfo &player, UI &gamePanels)
	: player(player), gamePanels(gamePanels), roomName(DefaultRoomName(player))
{
}



void CoOpRelayPanel::Step()
{
	CoOpRelayController::Get().Step(player);
}



void CoOpRelayPanel::Draw()
{
	DrawBackdrop();

	const Font &title = FontSet::Get(18);
	const Font &font = FontSet::Get(14);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &medium = *GameData::Colors().Get("medium");
	const Color &dim = *GameData::Colors().Get("dim");
	const Color &inactive = *GameData::Colors().Get("inactive");
	const Color back(0.f, .78f);
	const Color panel(.04f, .04f, .05f, .92f);

	vector<CoOpRelayController::DiscoveredRelay> discovered = CoOpRelayController::Get().DiscoveredRelays();
	if(selectedDiscovery >= static_cast<int>(discovered.size()))
		selectedDiscovery = -1;

	const bool canJoin = player.IsLoaded();
	const bool isHostRunning = CoOpRelayController::Get().IsHostRunning();
	const bool canPlay = canJoin && isHostRunning;
	const bool canJoinSelected = canJoin && selectedDiscovery >= 0
		&& selectedDiscovery < static_cast<int>(discovered.size());
	const bool canSync = CoOpRelayController::Get().IsConnected();
	const bool canSendResources = canJoin && CoOpRelayController::Get().IsConnected();
	const CoOpRelay::PresenceStore &remoteStore = CoOpRelayController::Get().Remotes();
	const vector<CoOpRelay::SharedResourceEvent> pendingRequests = CoOpRelayController::Get().PendingResourceRequests();
	const bool hasPendingRequest = !pendingRequests.empty();
	if(!resourceTargetPlayerId.empty() && !remoteStore.Get(resourceTargetPlayerId))
		resourceTargetPlayerId.clear();

	const Rectangle body(Point(), Point(760., 640.));
	FillShader::Fill(body, panel);
	FillShader::Fill(Rectangle::FromCorner(body.TopLeft(), Point(body.Width(), 1.)), bright);
	FillShader::Fill(Rectangle::FromCorner(body.BottomLeft() - Point(0., 1.), Point(body.Width(), 1.)), dim);

	DrawCentered(title, "Co-op Relay", body.Top() + 22., bright);
	DrawCentered(font, PilotLabel(player), body.Top() + 52., medium);

	font.Draw("Client: " + CoOpRelayController::Get().StatusText(), body.TopLeft() + Point(24., 86.), bright);
	font.Draw("Host: " + CoOpRelayController::Get().HostStatusText(), body.TopLeft() + Point(24., 108.), medium);

	const double buttonY = body.Top() + 152.;
	DrawButton(Rectangle(Point(-330., buttonY), Point(90., 30.)), "Room", true, 'n');
	DrawButton(Rectangle(Point(-235., buttonY), Point(100., 30.)), "Password", true, 'w');
	DrawButton(Rectangle(Point(-145., buttonY), Point(80., 30.)), "Port", !isHostRunning, 't');
	DrawButton(Rectangle(Point(-65., buttonY), Point(80., 30.)), "Host", true, 'h');
	DrawButton(Rectangle(Point(20., buttonY), Point(80., 30.)), "Play", canPlay, 'p');
	DrawButton(Rectangle(Point(115., buttonY), Point(100., 30.)), "Direct IP", canJoin, 'j');
	DrawButton(Rectangle(Point(205., buttonY), Point(80., 30.)), "Join", canJoinSelected, SDLK_RETURN);
	DrawButton(Rectangle(Point(310., buttonY), Point(120., 30.)), "Disconnect", true, 'd');
	DrawButton(Rectangle(Point(325., body.Bottom() - 24.), Point(90., 28.)), "Back", true, SDLK_ESCAPE);

	font.Draw("Host Lobby", body.TopLeft() + Point(24., 198.), bright);
	font.Draw(FitText(font, "Room: " + roomName, 700.), body.TopLeft() + Point(24., 224.),
		isHostRunning ? bright : medium);
	font.Draw(PasswordStatus(roomPassword), body.TopLeft() + Point(24., 246.), roomPassword.empty() ? inactive : medium);
	font.Draw(PortStatus(roomPort), body.TopLeft() + Point(280., 246.), isHostRunning ? bright : medium);
	const double assistY = body.Top() + 252.;
	DrawButton(Rectangle(Point(20., assistY), Point(70., 24.)), "Sync", canSync, 's');
	DrawButton(Rectangle(Point(95., assistY), Point(70., 24.)), "Refuel", canSendResources, 'f');
	DrawButton(Rectangle(Point(170., assistY), Point(70., 24.)), "Repair", canSendResources, 'r');
	DrawButton(Rectangle(Point(245., assistY), Point(70., 24.)), "Fuel", canSendResources, 'u');
	DrawButton(Rectangle(Point(320., assistY), Point(70., 24.)), "Energy", canSendResources, 'e');
	DrawButton(Rectangle(Point(395., assistY), Point(70., 24.)), "Accept", hasPendingRequest, 'y');
	DrawButton(Rectangle(Point(470., assistY), Point(70., 24.)), "Reject", hasPendingRequest, 'x');
	string assistText = "Assist target: " + ResourceTargetLabel(resourceTargetPlayerId, remoteStore);
	if(hasPendingRequest)
		assistText += "    Pending: " + PendingResourceLabel(pendingRequests.front(), remoteStore);
	font.Draw(FitText(font, assistText, 700.),
		body.TopLeft() + Point(24., 282.), canSendResources ? medium : inactive);

	if(!canJoin)
	{
		string text = isHostRunning
			? "Room is hosted. Load a pilot, then click Play."
			: "Name and host a room now. Load a pilot before joining as a player.";
		font.Draw(FitText(font, text, 700.), body.TopLeft() + Point(24., 268.), inactive);
	}

	font.Draw("Internet / Direct IP Join", body.TopLeft() + Point(24., 306.), bright);
	font.Draw(FitText(font, "Friends across the internet will not see LAN discovery. Press J and enter host public IP:5050.",
		700.), body.TopLeft() + Point(24., 332.), canJoin ? medium : inactive);
	font.Draw(FitText(font, "Last direct address: " + directAddress + "    Port-forward host TCP/UDP " + to_string(roomPort) + ".",
		700.), body.TopLeft() + Point(24., 352.), canJoin ? medium : inactive);

	font.Draw("LAN / Tailscale Discovery", body.TopLeft() + Point(24., 382.), bright);
	if(discovered.empty())
		font.Draw("No nearby relays found. This is normal across different homes without VPN discovery.",
			body.TopLeft() + Point(24., 408.), inactive);
	else
	{
		const int discoveryLines = min(static_cast<int>(discovered.size()), 3);
		for(int i = 0; i < discoveryLines; ++i)
		{
			const CoOpRelayController::DiscoveredRelay &relay = discovered[i];
			string text = to_string(i + 1) + "  " + relay.name + " - " + relay.host + ":" + to_string(relay.port);
			if(relay.playerCount >= 0)
				text += " - " + to_string(relay.playerCount) + (relay.playerCount == 1 ? " player" : " players");
			if(relay.passwordProtected)
				text += " locked";
			if(relay.ageSeconds > 0)
				text += " (" + to_string(relay.ageSeconds) + "s)";

			const bool selected = (i == selectedDiscovery);
			const Rectangle row = Rectangle::FromCorner(body.TopLeft() + Point(20., 404. + 20. * i), Point(720., 20.));
			if(canJoin)
			{
				FillShader::Fill(row, selected ? Color(.1f, .1f, .12f, .72f) : Color(.06f, .06f, .07f, .42f));
				AddZone(row, [this, i](){ SelectDiscovered(static_cast<size_t>(i)); });
			}
			font.Draw(FitText(font, text, 700.), body.TopLeft() + Point(24., 408. + 20. * i),
				selected ? bright : (canJoin ? medium : inactive));
		}
	}

	const CoOpRelay::AuthorityStore &authorityStore = CoOpRelayController::Get().Authorities();
	vector<CoOpRelay::RemotePresence> remotes = remoteStore.All();
	vector<CoOpRelay::SharedMissionEvent> missionStates =
		CoOpRelayController::Get().MissionEvents().LatestStates();
	vector<CoOpRelay::SharedNPCSnapshot> localNPCs;
	if(player.GetSystem())
		localNPCs = CoOpRelayController::Get().SharedNPCs().InSystem(player.GetSystem()->TrueName());
	font.Draw("Players", body.TopLeft() + Point(24., 462.), bright);
	int line = 0;
	if(CoOpRelayController::Get().IsConnected() && player.IsLoaded())
	{
		const string system = player.GetSystem() ? player.GetSystem()->TrueName() : "";
		font.Draw(FitText(font, LocalStatus(player), 700.), body.TopLeft() + Point(24., 488.), bright);
		++line;
		font.Draw(FitText(font, AuthorityStatus(player, authorityStore, remoteStore,
			CoOpRelayController::Get().IsSystemAuthority(system)), 700.),
			body.TopLeft() + Point(24., 488. + 20. * line), medium);
		++line;
		font.Draw(FitText(font, "Shared NPCs: " + to_string(localNPCs.size()), 700.),
			body.TopLeft() + Point(24., 488. + 20. * line), localNPCs.empty() ? inactive : medium);
		++line;
		font.Draw(FitText(font, MissionStateText(missionStates), 700.),
			body.TopLeft() + Point(24., 488. + 20. * line), missionStates.empty() ? inactive : medium);
		++line;
	}
	for(const CoOpRelay::RemotePresence &presence : remotes)
	{
		if(line >= 3)
			break;
		const CoOpRelay::PlayerSnapshot &snapshot = presence.latest;
		string text = snapshot.name.empty() ? snapshot.playerId : snapshot.name;
		text += " - " + snapshot.system;
		if(snapshot.IsLanded())
			text += " / " + snapshot.landedPlanet;
		const bool isTarget = snapshot.playerId == resourceTargetPlayerId;
		const Rectangle row = Rectangle::FromCorner(body.TopLeft() + Point(20., 484. + 20. * line), Point(720., 20.));
		if(canSendResources)
		{
			FillShader::Fill(row, isTarget ? Color(.1f, .1f, .12f, .72f) : Color(.06f, .06f, .07f, .32f));
			AddZone(row, [this, playerId = snapshot.playerId](){ SelectResourceTarget(playerId); });
		}
		font.Draw(FitText(font, text, 700.), body.TopLeft() + Point(24., 488. + 20. * line),
			isTarget ? bright : medium);
		++line;
	}
	if(!line)
		font.Draw("Not connected to a relay.", body.TopLeft() + Point(24., 488.), inactive);
	else if(remotes.empty())
		font.Draw("Waiting for remote players.", body.TopLeft() + Point(24., 488. + 20. * line), inactive);

	const vector<string> &warnings = CoOpRelayController::Get().DesyncWarnings();
	const vector<CoOpRelay::PlayerEvent> &events = CoOpRelayController::Get().RecentEvents();
	font.Draw(diagnosticsVisible ? "Diagnostics" : "Events", body.TopLeft() + Point(24., 556.), bright);
	if(diagnosticsVisible)
	{
		const CoOpRelay::Diagnostics diagnostics = CoOpRelayController::Get().DiagnosticsFor(player);
		string authorityOwner = diagnostics.authorityOwner.empty() ? "pending" : diagnostics.authorityOwner;
		if(authorityOwner == CoOpRelayController::Get().PlayerId())
			authorityOwner = "you";
		font.Draw(FitText(font, "Players: " + to_string(diagnostics.connectedPlayers)
			+ "    Player ID: " + (diagnostics.playerId.empty() ? "n/a" : diagnostics.playerId)
			+ "    Authority: " + authorityOwner
			+ "    Server world: " + (diagnostics.serverWorldEnabled ? "on" : "off"), 700.),
			body.TopLeft() + Point(24., 582.), medium);
		font.Draw(FitText(font, "Remote proxies: " + to_string(diagnostics.remotePlayerProxies)
			+ "    NPC proxies: " + to_string(diagnostics.npcProxies)
			+ "    Stale: " + to_string(diagnostics.staleProxyCount)
			+ "    Duplicates blocked: " + to_string(diagnostics.duplicatePreventionCount), 700.),
			body.TopLeft() + Point(24., 602.), diagnostics.staleProxyCount ? bright : medium);
		font.Draw(FitText(font, "Snapshot age: " + to_string(diagnostics.lastSnapshotAgeSteps) + " steps"
			+ "    Server tick: " + RateText(diagnostics.serverTickRate) + "/s"
			+ "    Client snapshots: " + RateText(diagnostics.clientSnapshotRate) + "/s"
			+ "    Ping: " + LatencyText(diagnostics.latencyMs), 700.),
			body.TopLeft() + Point(24., 622.), medium);
	}
	else if(events.empty() && warnings.empty())
		font.Draw("No remote events received.", body.TopLeft() + Point(24., 582.), inactive);
	else
	{
		int line = 0;
		for(auto it = warnings.rbegin(); it != warnings.rend() && line < 3; ++it, ++line)
			font.Draw(FitText(font, "Warning: " + *it, 700.),
				body.TopLeft() + Point(24., 582. + 20. * line), bright);
		for(auto it = events.rbegin(); it != events.rend() && line < 3; ++it, ++line)
			font.Draw(FitText(font, EventText(*it, remoteStore), 700.),
				body.TopLeft() + Point(24., 582. + 20. * line), medium);
	}

	FillShader::Fill(Rectangle::FromCorner(body.BottomLeft() + Point(0., -42.), Point(body.Width(), 42.)), back);
	font.Draw(FitText(font, "N room  W password  T port  H host  P play  J direct IP  1-3 select  Enter join  D disconnect  Esc back", 710.),
		body.BottomLeft() + Point(24., -34.), dim);
	font.Draw(FitText(font, "A all  S sync  F refuel assist  R repair assist  U fuel transfer  E energy transfer  Y accept  X reject", 710.),
		body.BottomLeft() + Point(24., -16.), dim);
}



bool CoOpRelayPanel::KeyDown(SDL_Keycode key, Uint16, const Command &, bool)
{
	if(key == SDLK_ESCAPE || key == 'b')
	{
		GetUI().Pop(this);
		return true;
	}
	if(key == 'g')
	{
		diagnosticsVisible = !diagnosticsVisible;
		return true;
	}
	if(key == 'h')
	{
		HostRoom();
		return true;
	}
	if(key == 'n')
	{
		NameRoom();
		return true;
	}
	if(key == 'w')
	{
		PasswordRoom();
		return true;
	}
	if(key == 't')
	{
		if(!CoOpRelayController::Get().IsHostRunning())
			PortRoom();
		return true;
	}
	if(key == 'p')
	{
		PlayHost();
		return true;
	}
	if(key == 'j')
	{
		if(player.IsLoaded())
			JoinAddressPrompt();
		return true;
	}
	if(key == 'l')
	{
		JoinLocal();
		return true;
	}
	if(key == 'd')
	{
		CoOpRelayController::Get().Disconnect();
		return true;
	}
	if(key == 's')
	{
		RequestResync();
		return true;
	}
	if(key == 'a')
	{
		SelectResourceTarget({});
		return true;
	}
	if(key == 'f')
	{
		SendRefuelAssist();
		return true;
	}
	if(key == 'r')
	{
		SendRepairAssist();
		return true;
	}
	if(key == 'u')
	{
		SendFuelAssist();
		return true;
	}
	if(key == 'e')
	{
		SendEnergyAssist();
		return true;
	}
	if(key == 'y')
	{
		AcceptResourceRequest();
		return true;
	}
	if(key == 'x')
	{
		RejectResourceRequest();
		return true;
	}
	if(key == SDLK_RETURN || key == SDLK_KP_ENTER)
	{
		JoinSelected();
		return true;
	}
	if(key >= SDLK_1 && key <= SDLK_3)
	{
		SelectDiscovered(static_cast<size_t>(key - SDLK_1));
		return true;
	}
	if(key >= SDLK_KP_1 && key <= SDLK_KP_3)
	{
		SelectDiscovered(static_cast<size_t>(key - SDLK_KP_1));
		return true;
	}
	return false;
}



void CoOpRelayPanel::DrawButton(const Rectangle &bounds, const string &label, bool enabled, SDL_Keycode key)
{
	const Font &font = FontSet::Get(14);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &medium = *GameData::Colors().Get("medium");
	const Color &inactive = *GameData::Colors().Get("inactive");
	const Color fill = enabled ? Color(.08f, .08f, .09f, .78f) : Color(.03f, .03f, .03f, .5f);
	FillShader::Fill(bounds, fill);
	FillShader::Fill(Rectangle::FromCorner(bounds.TopLeft(), Point(bounds.Width(), 1.)), enabled ? medium : inactive);
	font.Draw(label, bounds.Center() - .5 * Point(font.Width(label), font.Height()), enabled ? bright : inactive);
	if(enabled)
		AddZone(bounds, key);
}



void CoOpRelayPanel::DrawCentered(const Font &font, const string &text, double y, const Color &color) const
{
	font.Draw(text, Point(-.5 * font.Width(text), y), color);
}



void CoOpRelayPanel::NameRoom()
{
	GetUI().Push(DialogPanel::RequestString(this, &CoOpRelayPanel::SetRoomName, "Room name:", roomName));
}



void CoOpRelayPanel::SetRoomName(const string &name)
{
	roomName = CleanRoomName(name);
	CoOpRelayController::Get().SetHostRoomName(roomName);
	UI::PlaySound(UI::UISound::NORMAL);
}



void CoOpRelayPanel::PasswordRoom()
{
	GetUI().Push(DialogPanel::RequestString(this, &CoOpRelayPanel::SetRoomPassword,
		"Room password (blank for none):", roomPassword));
}



void CoOpRelayPanel::SetRoomPassword(const string &password)
{
	roomPassword = CleanPassword(password);
	CoOpRelayController::Get().SetHostRoomPassword(roomName, roomPassword);
	UI::PlaySound(UI::UISound::NORMAL);
}



void CoOpRelayPanel::PortRoom()
{
	GetUI().Push(DialogPanel::RequestString(this, &CoOpRelayPanel::SetRoomPort,
		"Room port:", to_string(roomPort)));
}



void CoOpRelayPanel::SetRoomPort(const string &port)
{
	uint16_t parsed = roomPort;
	const bool valid = ParsePort(port, parsed);
	if(valid)
		roomPort = parsed;
	UI::PlaySound(valid ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::HostRoom()
{
	CoOpRelayController::Get().StartHost(roomPort, roomName, roomPassword);
	UI::PlaySound(UI::UISound::NORMAL);
}



void CoOpRelayPanel::PlayHost()
{
	if(!player.IsLoaded())
		return;
	if(!CoOpRelayController::Get().IsHostRunning())
		CoOpRelayController::Get().StartHost(roomPort, roomName, roomPassword);
	if(CoOpRelayController::Get().IsHostRunning() && !CoOpRelayController::Get().IsConnected())
		CoOpRelayController::Get().JoinLocal(player, roomPort, roomPassword);
	if(CoOpRelayController::Get().IsHostRunning())
		EnterGame();
}



void CoOpRelayPanel::JoinLocal()
{
	if(!player.IsLoaded())
		return;
	CoOpRelayController::Get().JoinLocal(player, roomPort, roomPassword);
	UI::PlaySound(UI::UISound::NORMAL);
}



void CoOpRelayPanel::JoinAddressPrompt()
{
	GetUI().Push(DialogPanel::RequestString(this, &CoOpRelayPanel::JoinAddress,
		"Host public IP or IP:port:", directAddress));
}



void CoOpRelayPanel::JoinAddress(const string &address)
{
	if(!player.IsLoaded())
		return;

	CoOpRelay::RelayEndpoint endpoint = CoOpRelay::ParseEndpoint(address);
	if(endpoint.host.empty())
	{
		UI::PlaySound(UI::UISound::FAILURE);
		return;
	}

	directAddress = endpoint.host + ":" + to_string(endpoint.port);
	pendingJoinHost = std::move(endpoint.host);
	pendingJoinPort = endpoint.port;
	GetUI().Push(DialogPanel::RequestString(this, &CoOpRelayPanel::JoinAddressWithPassword,
		"Room password (blank if none):", roomPassword));
}



void CoOpRelayPanel::JoinAddressWithPassword(const string &password)
{
	if(!player.IsLoaded() || pendingJoinHost.empty() || !pendingJoinPort)
		return;

	roomPassword = CleanPassword(password);
	CoOpRelayController::Get().Connect(std::move(pendingJoinHost), pendingJoinPort, PlayerName(player), roomPassword);
	pendingJoinHost.clear();
	pendingJoinPort = 0;
	UI::PlaySound(UI::UISound::NORMAL);
	EnterGame();
}



void CoOpRelayPanel::SelectDiscovered(size_t index)
{
	vector<CoOpRelayController::DiscoveredRelay> relays = CoOpRelayController::Get().DiscoveredRelays();
	if(index >= relays.size())
		return;

	selectedDiscovery = static_cast<int>(index);
	UI::PlaySound(UI::UISound::SOFT);
}



void CoOpRelayPanel::SelectResourceTarget(string playerId)
{
	if(!playerId.empty() && !CoOpRelayController::Get().Remotes().Get(playerId))
		return;

	resourceTargetPlayerId = std::move(playerId);
	UI::PlaySound(UI::UISound::SOFT);
}



void CoOpRelayPanel::JoinSelected()
{
	if(!player.IsLoaded())
		return;

	vector<CoOpRelayController::DiscoveredRelay> relays = CoOpRelayController::Get().DiscoveredRelays();
	if(selectedDiscovery < 0 || selectedDiscovery >= static_cast<int>(relays.size()))
		return;

	CoOpRelayController::DiscoveredRelay relay = std::move(relays[selectedDiscovery]);
	if(relay.passwordProtected && roomPassword.empty())
	{
		pendingJoinHost = std::move(relay.host);
		pendingJoinPort = relay.port;
		GetUI().Push(DialogPanel::RequestString(this, &CoOpRelayPanel::JoinSelectedWithPassword,
			"Room password:", roomPassword));
		return;
	}

	CoOpRelayController::Get().Connect(relay.host, relay.port, PlayerName(player), roomPassword);
	UI::PlaySound(UI::UISound::NORMAL);
	EnterGame();
}



void CoOpRelayPanel::JoinSelectedWithPassword(const string &password)
{
	if(!player.IsLoaded() || pendingJoinHost.empty() || !pendingJoinPort)
		return;

	roomPassword = CleanPassword(password);
	CoOpRelayController::Get().Connect(std::move(pendingJoinHost), pendingJoinPort, PlayerName(player), roomPassword);
	pendingJoinHost.clear();
	pendingJoinPort = 0;
	UI::PlaySound(UI::UISound::NORMAL);
	EnterGame();
}



void CoOpRelayPanel::RequestResync()
{
	const bool sent = CoOpRelayController::Get().RequestResync();
	UI::PlaySound(sent ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::SendRefuelAssist()
{
	const bool sent = CoOpRelayController::Get().PublishResourceAssist(CoOpRelay::ResourceActionType::REFUEL_ASSIST,
		1., resourceTargetPlayerId);
	UI::PlaySound(sent ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::SendRepairAssist()
{
	const bool sent = CoOpRelayController::Get().PublishResourceAssist(CoOpRelay::ResourceActionType::REPAIR_ASSIST,
		1., resourceTargetPlayerId);
	UI::PlaySound(sent ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::SendFuelAssist()
{
	const bool sent = CoOpRelayController::Get().PublishResourceAssist(CoOpRelay::ResourceActionType::FUEL_TRANSFER,
		.25, resourceTargetPlayerId);
	UI::PlaySound(sent ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::SendEnergyAssist()
{
	const bool sent = CoOpRelayController::Get().PublishResourceAssist(CoOpRelay::ResourceActionType::ENERGY_TRANSFER,
		.25, resourceTargetPlayerId);
	UI::PlaySound(sent ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::AcceptResourceRequest()
{
	vector<CoOpRelay::SharedResourceEvent> requests = CoOpRelayController::Get().PendingResourceRequests();
	const bool sent = !requests.empty()
		&& CoOpRelayController::Get().RespondToResourceRequest(player, requests.front().actionId, true);
	UI::PlaySound(sent ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::RejectResourceRequest()
{
	vector<CoOpRelay::SharedResourceEvent> requests = CoOpRelayController::Get().PendingResourceRequests();
	const bool sent = !requests.empty()
		&& CoOpRelayController::Get().RespondToResourceRequest(player, requests.front().actionId, false);
	UI::PlaySound(sent ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::EnterGame()
{
	gamePanels.CanSave(true);
	if(shared_ptr<Panel> root = GetUI().Root())
		GetUI().PopThrough(root.get());
	else
		GetUI().Pop(this);
}
