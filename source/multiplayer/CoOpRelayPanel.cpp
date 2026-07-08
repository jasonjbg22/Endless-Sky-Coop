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

#include <SDL2/SDL_clipboard.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using GameRectangle = Rectangle;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace std;

namespace {
#ifdef _WIN32
	using SocketHandle = SOCKET;
	constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;

	void EnsureSockets()
	{
		class WinsockScope {
		public:
			WinsockScope()
			{
				WSADATA data;
				WSAStartup(MAKEWORD(2, 2), &data);
			}

			~WinsockScope()
			{
				WSACleanup();
			}
		};

		static WinsockScope winsock;
	}

	void CloseSocket(SocketHandle socket)
	{
		if(socket != INVALID_SOCKET_HANDLE)
			closesocket(socket);
	}
#else
	using SocketHandle = int;
	constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;

	void EnsureSockets()
	{
	}

	void CloseSocket(SocketHandle socket)
	{
		if(socket != INVALID_SOCKET_HANDLE)
			close(socket);
	}
#endif



	bool IsValid(SocketHandle socket)
	{
		return socket != INVALID_SOCKET_HANDLE;
	}



	bool SendAll(SocketHandle socket, const char *data, size_t size)
	{
		while(size)
		{
#ifdef _WIN32
			const int sent = send(socket, data, static_cast<int>(min<size_t>(size, 4096)), 0);
#else
			const ssize_t sent = send(socket, data, size, 0);
#endif
			if(sent <= 0)
				return false;
			data += sent;
			size -= sent;
		}
		return true;
	}



	string TrimAscii(string text)
	{
		const size_t first = text.find_first_not_of(" \t\r\n");
		if(first == string::npos)
			return {};
		const size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1);
	}



	bool LooksLikePublicIp(const string &text)
	{
		if(text.empty() || text.size() > 64)
			return false;

		bool hasDigit = false;
		for(char ch : text)
		{
			hasDigit = hasDigit || (ch >= '0' && ch <= '9');
			if((ch < '0' || ch > '9') && ch != '.' && ch != ':')
				return false;
		}
		return hasDigit;
	}



	string FetchHttpText(const string &host, const string &path, string &error)
	{
		EnsureSockets();

		addrinfo hints = {};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		addrinfo *addresses = nullptr;
		if(getaddrinfo(host.c_str(), "80", &hints, &addresses) != 0)
		{
			error = "lookup failed";
			return {};
		}

		SocketHandle connected = INVALID_SOCKET_HANDLE;
		for(addrinfo *address = addresses; address; address = address->ai_next)
		{
			SocketHandle socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
			if(!IsValid(socket))
				continue;

#ifdef _WIN32
			const DWORD timeout = 5000;
			setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
			setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
			const timeval timeout = {5, 0};
			setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif

			if(connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
			{
				connected = socket;
				break;
			}
			CloseSocket(socket);
		}
		freeaddrinfo(addresses);

		if(!IsValid(connected))
		{
			error = "connect failed";
			return {};
		}

		ostringstream request;
		request << "GET " << path << " HTTP/1.1\r\n"
			<< "Host: " << host << "\r\n"
			<< "User-Agent: EndlessSkyCoop/1\r\n"
			<< "Connection: close\r\n\r\n";
		const string wire = request.str();
		if(!SendAll(connected, wire.data(), wire.size()))
		{
			CloseSocket(connected);
			error = "request failed";
			return {};
		}

		string response;
		char buffer[1024] = {};
		while(response.size() < 8192)
		{
#ifdef _WIN32
			const int count = recv(connected, buffer, sizeof(buffer), 0);
#else
			const ssize_t count = recv(connected, buffer, sizeof(buffer), 0);
#endif
			if(count <= 0)
				break;
			response.append(buffer, static_cast<size_t>(count));
		}
		CloseSocket(connected);

		const size_t body = response.find("\r\n\r\n");
		if(body == string::npos)
		{
			error = "bad response";
			return {};
		}
		return TrimAscii(response.substr(body + 4));
	}



	string FetchPublicIpAddress(string &error)
	{
		const pair<const char *, const char *> services[] = {
			{"api.ipify.org", "/?format=text"},
			{"checkip.amazonaws.com", "/"},
			{"ifconfig.me", "/ip"},
		};

		for(const auto &[host, path] : services)
		{
			string serviceError;
			string address = FetchHttpText(host, path, serviceError);
			address = TrimAscii(address);
			if(LooksLikePublicIp(address))
				return address;
			if(error.empty())
				error = serviceError.empty() ? "invalid response" : serviceError;
		}
		if(error.empty())
			error = "no response";
		return {};
	}



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



	string EndpointStatus(string address, uint16_t defaultPort)
	{
		CoOpRelay::RelayEndpoint endpoint = CoOpRelay::ParseEndpoint(std::move(address), defaultPort);
		return endpoint.host + ":" + to_string(endpoint.port);
	}



	bool NeedsPublicAddress(const string &address)
	{
		return address.find("public-ip") != string::npos || address.find("host-public-ip") != string::npos;
	}



	void DrawPanelBox(const GameRectangle &bounds, const Color &fill, const Color &line)
	{
		FillShader::Fill(bounds, fill);
		FillShader::Fill(GameRectangle::FromCorner(bounds.TopLeft(), Point(bounds.Width(), 1.)), line);
		FillShader::Fill(GameRectangle::FromCorner(bounds.BottomLeft() - Point(0., 1.), Point(bounds.Width(), 1.)), line);
		FillShader::Fill(GameRectangle::FromCorner(bounds.TopLeft(), Point(1., bounds.Height())), line);
		FillShader::Fill(GameRectangle::FromCorner(bounds.TopRight() - Point(1., 0.), Point(1., bounds.Height())), line);
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



struct CoOpRelayPanel::PublicIpLookupState {
	mutex lock;
	bool done = false;
	string address;
	string error;
};



CoOpRelayPanel::CoOpRelayPanel(PlayerInfo &player, UI &gamePanels)
	: player(player), gamePanels(gamePanels), roomName(DefaultRoomName(player))
{
}



void CoOpRelayPanel::Step()
{
	CoOpRelayController::Get().Step(player);

	if(!publicIpLookup)
		return;

	string address;
	string error;
	{
		lock_guard<mutex> lock(publicIpLookup->lock);
		if(!publicIpLookup->done)
			return;
		address = publicIpLookup->address;
		error = publicIpLookup->error;
	}
	publicIpLookup.reset();

	if(!address.empty())
	{
		if(address.find(':') != string::npos && address.find('.') == string::npos)
		{
			bridgeNotice = "Detected IPv6; enter an IPv4 public IP or DNS name for now.";
			UI::PlaySound(UI::UISound::FAILURE);
			return;
		}
		wanAddress = address + ":" + to_string(roomPort);
		directAddress = wanAddress;
		bridgeNotice = "Detected public IP: " + wanAddress;
		UI::PlaySound(UI::UISound::NORMAL);
		return;
	}

	bridgeNotice = "Public IP lookup failed" + (error.empty() ? string(".") : ": " + error + ".");
	UI::PlaySound(UI::UISound::FAILURE);
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
	const Color panel(.035f, .038f, .045f, .95f);
	const Color card(.065f, .07f, .082f, .86f);
	const Color cardLine(.18f, .22f, .26f, .82f);
	const Color cardAccent(.18f, .34f, .52f, .9f);
	const Color footer(.015f, .018f, .022f, .88f);

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

	const GameRectangle body(Point(), Point(880., 700.));
	FillShader::Fill(body, panel);
	FillShader::Fill(GameRectangle::FromCorner(body.TopLeft(), Point(body.Width(), 1.)), cardAccent);
	FillShader::Fill(GameRectangle::FromCorner(body.BottomLeft() - Point(0., 1.), Point(body.Width(), 1.)), dim);

	DrawCentered(title, "Co-op Relay", body.Top() + 22., bright);
	DrawCentered(font, PilotLabel(player), body.Top() + 52., medium);

	auto at = [&body](double x, double y) -> Point {
		return body.TopLeft() + Point(x, y);
	};
	auto drawText = [&](const string &text, double x, double y, const Color &color, double width = 390.) {
		font.Draw(FitText(font, text, width), at(x, y), color);
	};
	auto button = [&](double x, double y, double width, double height, const string &label, bool enabled,
			SDL_Keycode key) {
		DrawButton(GameRectangle::FromCorner(at(x, y), Point(width, height)), label, enabled, key);
	};
	auto box = [&](double x, double y, double width, double height, const string &label) -> GameRectangle {
		GameRectangle bounds = GameRectangle::FromCorner(at(x, y), Point(width, height));
		DrawPanelBox(bounds, card, cardLine);
		FillShader::Fill(GameRectangle::FromCorner(bounds.TopLeft(), Point(bounds.Width(), 1.)), cardAccent);
		font.Draw(label, bounds.TopLeft() + Point(14., 13.), bright);
		return bounds;
	};

	drawText("Client: " + CoOpRelayController::Get().StatusText(), 24., 80., bright, 830.);
	drawText("Host: " + CoOpRelayController::Get().HostStatusText(), 24., 101., medium, 830.);

	const double navY = 126.;
	double navX = 24.;
	button(navX, navY, 78., 28., "Room", true, 'n');
	navX += 86.;
	button(navX, navY, 100., 28., "Password", true, 'w');
	navX += 108.;
	button(navX, navY, 64., 28., "Port", !isHostRunning, 't');
	navX += 72.;
	button(navX, navY, 72., 28., "Host", true, 'h');
	navX += 80.;
	button(navX, navY, 64., 28., "Play", canPlay, 'p');
	navX += 72.;
	button(navX, navY, 92., 28., "Join WAN", canJoin, 'j');
	navX += 100.;
	button(navX, navY, 88., 28., "Join LAN", canJoinSelected, SDLK_RETURN);
	navX += 96.;
	button(navX, navY, 104., 28., "Disconnect", true, 'd');
	button(788., navY, 68., 28., "Back", true, SDLK_ESCAPE);

	box(24., 174., 404., 168., "Host Room");
	drawText("Room", 42., 216., dim, 80.);
	drawText(roomName, 134., 216., isHostRunning ? bright : medium, 260.);
	drawText("Password", 42., 240., dim, 90.);
	drawText(roomPassword.empty() ? "not set" : "set", 134., 240., roomPassword.empty() ? inactive : medium, 260.);
	drawText("Port", 42., 264., dim, 80.);
	drawText(to_string(roomPort), 134., 264., isHostRunning ? bright : medium, 260.);
	button(42., 294., 116., 28., "Host WAN", true, 'm');
	button(166., 294., 78., 28., "Host", true, 'h');
	button(252., 294., 78., 28., "Play", canPlay, 'p');

	box(452., 174., 404., 168., "WAN Bridge");
	string publicIpText = NeedsPublicAddress(wanAddress) ? "not detected" : CoOpRelay::ParseEndpoint(wanAddress).host;
	if(publicIpLookup)
		publicIpText = "detecting...";
	drawText("Public IP", 470., 216., dim, 90.);
	drawText(publicIpText, 564., 216., publicIpLookup ? medium : (NeedsPublicAddress(wanAddress) ? inactive : bright),
		260.);
	const string invite = EndpointStatus(wanAddress, roomPort);
	drawText("Invite", 470., 240., dim, 80.);
	drawText(invite, 564., 240., NeedsPublicAddress(wanAddress) ? inactive : bright, 260.);
	drawText("Status", 470., 264., dim, 80.);
	string bridgeText = bridgeNotice.empty()
		? (isHostRunning ? "listening" : "ready")
		: bridgeNotice;
	drawText(bridgeText, 564., 264., bridgeNotice.empty() ? medium : bright, 260.);
	button(470., 294., 92., 28., "Detect IP", true, 'i');
	button(570., 294., 72., 28., "Edit IP", true, 'o');
	button(650., 294., 66., 28., "Copy", true, 'c');
	button(724., 294., 72., 28., "Join", canJoin, 'j');

	box(24., 362., 404., 120., "LAN / Tailscale");
	if(discovered.empty())
		drawText("No nearby relays found.", 42., 404., inactive, 360.);
	else
	{
		const int discoveryLines = min(static_cast<int>(discovered.size()), 3);
		for(int i = 0; i < discoveryLines; ++i)
		{
			const CoOpRelayController::DiscoveredRelay &relay = discovered[i];
			string text = to_string(i + 1) + "  " + relay.name + "  " + relay.host + ":" + to_string(relay.port);
			if(relay.playerCount >= 0)
				text += "  " + to_string(relay.playerCount) + (relay.playerCount == 1 ? " player" : " players");
			if(relay.passwordProtected)
				text += " locked";
			if(relay.ageSeconds > 0)
				text += " (" + to_string(relay.ageSeconds) + "s)";

			const bool selected = (i == selectedDiscovery);
			const GameRectangle row = GameRectangle::FromCorner(at(38., 398. + 22. * i), Point(372., 22.));
			if(canJoin)
			{
				FillShader::Fill(row, selected ? Color(.1f, .1f, .12f, .72f) : Color(.06f, .06f, .07f, .42f));
				AddZone(row, [this, i](){ SelectDiscovered(static_cast<size_t>(i)); });
			}
			font.Draw(FitText(font, text, 352.), row.TopLeft() + Point(8., 3.),
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
	box(452., 362., 404., 120., "Players");
	int line = 0;
	if(CoOpRelayController::Get().IsConnected() && player.IsLoaded())
	{
		const string system = player.GetSystem() ? player.GetSystem()->TrueName() : "";
		drawText(LocalStatus(player), 470., 404., bright, 360.);
		++line;
		drawText(AuthorityStatus(player, authorityStore, remoteStore,
			CoOpRelayController::Get().IsSystemAuthority(system)), 470., 404. + 20. * line, medium, 360.);
		++line;
		drawText("Shared NPCs: " + to_string(localNPCs.size()), 470., 404. + 20. * line,
			localNPCs.empty() ? inactive : medium, 360.);
		++line;
		drawText(MissionStateText(missionStates), 470., 404. + 20. * line,
			missionStates.empty() ? inactive : medium, 360.);
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
		const GameRectangle row = GameRectangle::FromCorner(at(466., 400. + 20. * line), Point(372., 20.));
		if(canSendResources)
		{
			FillShader::Fill(row, isTarget ? Color(.1f, .1f, .12f, .72f) : Color(.06f, .06f, .07f, .32f));
			AddZone(row, [this, playerId = snapshot.playerId](){ SelectResourceTarget(playerId); });
		}
		font.Draw(FitText(font, text, 352.), row.TopLeft() + Point(8., 2.),
			isTarget ? bright : medium);
		++line;
	}
	if(!line)
		drawText("Not connected to a relay.", 470., 404., inactive, 360.);
	else if(remotes.empty())
		drawText("Waiting for remote players.", 470., 404. + 20. * line, inactive, 360.);

	const vector<string> &warnings = CoOpRelayController::Get().DesyncWarnings();
	const vector<CoOpRelay::PlayerEvent> &events = CoOpRelayController::Get().RecentEvents();
	box(24., 502., 404., 116., diagnosticsVisible ? "Diagnostics" : "Events");
	if(diagnosticsVisible)
	{
		const CoOpRelay::Diagnostics diagnostics = CoOpRelayController::Get().DiagnosticsFor(player);
		string authorityOwner = diagnostics.authorityOwner.empty() ? "pending" : diagnostics.authorityOwner;
		if(authorityOwner == CoOpRelayController::Get().PlayerId())
			authorityOwner = "you";
		drawText("Players: " + to_string(diagnostics.connectedPlayers)
			+ "    Player ID: " + (diagnostics.playerId.empty() ? "n/a" : diagnostics.playerId)
			+ "    Authority: " + authorityOwner
			+ "    Server world: " + (diagnostics.serverWorldEnabled ? "on" : "off"),
			42., 544., medium, 360.);
		drawText("Remote proxies: " + to_string(diagnostics.remotePlayerProxies)
			+ "    NPC proxies: " + to_string(diagnostics.npcProxies)
			+ "    Stale: " + to_string(diagnostics.staleProxyCount)
			+ "    Duplicates blocked: " + to_string(diagnostics.duplicatePreventionCount),
			42., 564., diagnostics.staleProxyCount ? bright : medium, 360.);
		drawText("Snapshot age: " + to_string(diagnostics.lastSnapshotAgeSteps) + " steps"
			+ "    Server tick: " + RateText(diagnostics.serverTickRate) + "/s"
			+ "    Client snapshots: " + RateText(diagnostics.clientSnapshotRate) + "/s"
			+ "    Ping: " + LatencyText(diagnostics.latencyMs),
			42., 584., medium, 360.);
	}
	else if(events.empty() && warnings.empty())
		drawText("No remote events received.", 42., 544., inactive, 360.);
	else
	{
		int line = 0;
		for(auto it = warnings.rbegin(); it != warnings.rend() && line < 3; ++it, ++line)
			drawText("Warning: " + *it, 42., 544. + 20. * line, bright, 360.);
		for(auto it = events.rbegin(); it != events.rend() && line < 3; ++it, ++line)
			drawText(EventText(*it, remoteStore), 42., 544. + 20. * line, medium, 360.);
	}

	box(452., 502., 404., 116., "Co-op Assist");
	string assistText = "Target: " + ResourceTargetLabel(resourceTargetPlayerId, remoteStore);
	if(hasPendingRequest)
		assistText += "    " + PendingResourceLabel(pendingRequests.front(), remoteStore);
	drawText(assistText, 470., 544., canSendResources ? medium : inactive, 360.);
	button(470., 568., 58., 24., "Sync", canSync, 's');
	button(536., 568., 62., 24., "Refuel", canSendResources, 'f');
	button(606., 568., 62., 24., "Repair", canSendResources, 'r');
	button(676., 568., 52., 24., "Fuel", canSendResources, 'u');
	button(736., 568., 66., 24., "Energy", canSendResources, 'e');
	button(470., 596., 70., 22., "Accept", hasPendingRequest, 'y');
	button(548., 596., 70., 22., "Reject", hasPendingRequest, 'x');

	FillShader::Fill(GameRectangle::FromCorner(body.BottomLeft() + Point(0., -48.), Point(body.Width(), 48.)), footer);
	drawText("M host WAN  I detect IP  O edit IP  C copy invite  J join WAN  Enter join LAN  G diagnostics",
		24., 660., dim, 830.);
	drawText("N room  W password  T port  P play  D disconnect  Esc back",
		24., 680., dim, 830.);
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
	if(key == 'm')
	{
		HostWanBridge();
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
	if(key == 'i')
	{
		FetchPublicIp();
		return true;
	}
	if(key == 'o')
	{
		SetWanAddressPrompt();
		return true;
	}
	if(key == 'c')
	{
		CopyWanInvite();
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



void CoOpRelayPanel::DrawButton(const GameRectangle &bounds, const string &label, bool enabled, SDL_Keycode key)
{
	const Font &font = FontSet::Get(14);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &medium = *GameData::Colors().Get("medium");
	const Color &inactive = *GameData::Colors().Get("inactive");
	const Color fill = enabled ? Color(.105f, .12f, .145f, .88f) : Color(.035f, .038f, .045f, .62f);
	const Color topLine = enabled ? Color(.24f, .38f, .52f, .9f) : Color(.1f, .11f, .12f, .72f);
	FillShader::Fill(bounds, fill);
	FillShader::Fill(GameRectangle::FromCorner(bounds.TopLeft(), Point(bounds.Width(), 1.)), topLine);
	FillShader::Fill(GameRectangle::FromCorner(bounds.BottomLeft() - Point(0., 1.), Point(bounds.Width(), 1.)),
		enabled ? Color(.035f, .04f, .05f, .9f) : Color(.02f, .02f, .025f, .65f));
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
	{
		roomPort = parsed;
		CoOpRelay::RelayEndpoint endpoint = CoOpRelay::ParseEndpoint(wanAddress, roomPort);
		wanAddress = endpoint.host + ":" + to_string(roomPort);
	}
	UI::PlaySound(valid ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::HostRoom()
{
	CoOpRelayController::Get().StartHost(roomPort, roomName, roomPassword);
	if(player.IsLoaded() && CoOpRelayController::Get().IsHostRunning() && !CoOpRelayController::Get().IsConnected())
		CoOpRelayController::Get().JoinLocal(player, roomPort, roomPassword);
	UI::PlaySound(UI::UISound::NORMAL);
}



void CoOpRelayPanel::HostWanBridge()
{
	HostRoom();
	if(!CoOpRelayController::Get().IsHostRunning())
	{
		bridgeNotice = "WAN bridge could not open. Try another port or close the old host.";
		UI::PlaySound(UI::UISound::FAILURE);
		return;
	}

	if(NeedsPublicAddress(wanAddress))
	{
		bridgeNotice = "WAN bridge is listening. Looking up public IP...";
		FetchPublicIp();
	}
	else
		bridgeNotice = "WAN bridge is listening. Copy the invite for friends.";
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



void CoOpRelayPanel::FetchPublicIp()
{
	if(publicIpLookup)
	{
		bridgeNotice = "Public IP lookup is already running.";
		UI::PlaySound(UI::UISound::SOFT);
		return;
	}

	bridgeNotice = "Looking up public IP...";
	publicIpLookup = make_shared<PublicIpLookupState>();
	shared_ptr<PublicIpLookupState> lookup = publicIpLookup;
	thread([lookup]() {
		string error;
		string address = FetchPublicIpAddress(error);
		lock_guard<mutex> lock(lookup->lock);
		lookup->address = std::move(address);
		lookup->error = std::move(error);
		lookup->done = true;
	}).detach();
	UI::PlaySound(UI::UISound::NORMAL);
}



void CoOpRelayPanel::SetWanAddressPrompt()
{
	GetUI().Push(DialogPanel::RequestString(this, &CoOpRelayPanel::SetWanAddress,
		"Public invite address or IP:port:", wanAddress));
}



void CoOpRelayPanel::SetWanAddress(const string &address)
{
	if(address.find_first_not_of(" \t\r\n") == string::npos)
	{
		bridgeNotice = "Enter your public IP or DNS name before copying the WAN invite.";
		UI::PlaySound(UI::UISound::FAILURE);
		return;
	}

	CoOpRelay::RelayEndpoint endpoint = CoOpRelay::ParseEndpoint(address, roomPort);
	if(CoOpRelayController::Get().IsHostRunning() && endpoint.port != roomPort)
	{
		endpoint.port = roomPort;
		bridgeNotice = "Host is already listening on port " + to_string(roomPort) + "; invite port was corrected.";
	}
	else
	{
		roomPort = endpoint.port;
		bridgeNotice = "Invite is ready. Friends can paste it with Join.";
	}

	wanAddress = endpoint.host + ":" + to_string(endpoint.port);
	directAddress = wanAddress;
	UI::PlaySound(UI::UISound::NORMAL);
}



void CoOpRelayPanel::CopyWanInvite()
{
	if(NeedsPublicAddress(wanAddress))
	{
		bridgeNotice = "Set your public IP first, then copy the invite.";
		SetWanAddressPrompt();
		UI::PlaySound(UI::UISound::FAILURE);
		return;
	}

	const string invite = EndpointStatus(wanAddress, roomPort);
	const bool copied = SDL_SetClipboardText(invite.c_str()) == 0;
	bridgeNotice = copied ? "Copied invite: " + invite : "Could not copy invite to clipboard.";
	UI::PlaySound(copied ? UI::UISound::NORMAL : UI::UISound::FAILURE);
}



void CoOpRelayPanel::JoinAddressPrompt()
{
	GetUI().Push(DialogPanel::RequestString(this, &CoOpRelayPanel::JoinAddress,
		"WAN invite, public IP, or IP:port:", directAddress));
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
