/* CoOpRelayController.cpp
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

#include "CoOpRelayController.h"

#include "../Color.h"
#include "../Engine.h"
#include "../GameData.h"
#include "../Messages.h"
#include "../PlayerInfo.h"
#include "../Port.h"
#include "../Rectangle.h"
#include "../Screen.h"
#include "../Ship.h"
#include "../System.h"
#include "../image/Sprite.h"
#include "../shader/FillShader.h"
#include "../shader/LineShader.h"
#include "../shader/SpriteShader.h"
#include "../text/Font.h"
#include "../text/FontSet.h"
#include "../text/Format.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#else
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace std;

namespace {
	constexpr uint16_t DISCOVERY_PORT = 5051;
	constexpr int DISCOVERY_PROTOCOL_VERSION = 4;
	constexpr int DISCOVERY_TTL_SECONDS = 8;
	constexpr int RELAY_PING_INTERVAL_MS = 1000;
	constexpr int RELAY_PING_TIMEOUT_MS = 5000;
	constexpr int RELAY_CONNECT_RETRY_MS = 3000;
	constexpr size_t RELAY_INBOUND_QUEUE_LIMIT = 4096;
	constexpr size_t RELAY_INBOUND_PROCESS_LIMIT = 256;
	constexpr size_t RELAY_OUTBOUND_QUEUE_LIMIT = 512;
	const char *const DISCOVERY_PREFIX = "endless-sky-coop-relay";

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



	void ConfigureTcpLowLatency(SocketHandle socket)
	{
		if(!IsValid(socket))
			return;

		const int yes = 1;
		setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&yes), sizeof(yes));
#ifdef _WIN32
		const DWORD sendTimeout = 50;
		setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&sendTimeout), sizeof(sendTimeout));
#else
		const timeval sendTimeout = {0, 50000};
		setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&sendTimeout), sizeof(sendTimeout));
#endif
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



	bool SendLine(SocketHandle socket, const string &line)
	{
		string wire = line;
		wire += '\n';
		return SendAll(socket, wire.data(), wire.size());
	}



	uint16_t SocketLocalPort(SocketHandle socket)
	{
		sockaddr_storage address = {};
#ifdef _WIN32
		int length = sizeof(address);
#else
		socklen_t length = sizeof(address);
#endif
		if(getsockname(socket, reinterpret_cast<sockaddr *>(&address), &length) != 0)
			return 0;
		if(address.ss_family == AF_INET)
			return ntohs(reinterpret_cast<const sockaddr_in *>(&address)->sin_port);
		if(address.ss_family == AF_INET6)
			return ntohs(reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_port);
		return 0;
	}



	string SocketLocalHost(SocketHandle socket)
	{
		sockaddr_storage address = {};
#ifdef _WIN32
		int length = sizeof(address);
#else
		socklen_t length = sizeof(address);
#endif
		if(getsockname(socket, reinterpret_cast<sockaddr *>(&address), &length) != 0)
			return {};

		char host[NI_MAXHOST] = {};
		if(getnameinfo(reinterpret_cast<const sockaddr *>(&address), length, host, sizeof(host), nullptr, 0,
				NI_NUMERICHOST) != 0)
			return {};
		return host;
	}



	string CommandLineQuote(const string &argument)
	{
		if(argument.empty())
			return "\"\"";

		bool needsQuotes = false;
		for(char ch : argument)
			if(ch == ' ' || ch == '\t' || ch == '"')
			{
				needsQuotes = true;
				break;
			}
		if(!needsQuotes)
			return argument;

		string result = "\"";
		size_t backslashes = 0;
		for(char ch : argument)
		{
			if(ch == '\\')
			{
				++backslashes;
				continue;
			}
			if(ch == '"')
			{
				result.append(backslashes * 2 + 1, '\\');
				result += ch;
			}
			else
			{
				result.append(backslashes, '\\');
				result += ch;
			}
			backslashes = 0;
		}
		result.append(backslashes * 2, '\\');
		result += '"';
		return result;
	}



	SocketHandle ConnectSocket(const string &host, uint16_t port, string &error)
	{
		EnsureSockets();

		addrinfo hints = {};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		addrinfo *addresses = nullptr;
		const string portString = to_string(port);
		if(getaddrinfo(host.c_str(), portString.c_str(), &hints, &addresses) != 0)
		{
			error = "Unable to resolve relay host";
			return INVALID_SOCKET_HANDLE;
		}

		SocketHandle connected = INVALID_SOCKET_HANDLE;
		for(addrinfo *address = addresses; address; address = address->ai_next)
		{
			SocketHandle socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
			if(!IsValid(socket))
				continue;
			if(connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
			{
				ConfigureTcpLowLatency(socket);
				connected = socket;
				break;
			}
			CloseSocket(socket);
		}
		freeaddrinfo(addresses);

		if(!IsValid(connected))
			error = "Unable to connect to relay";
		return connected;
	}



	SocketHandle ListenSocket(uint16_t port, string &error)
	{
		EnsureSockets();

		addrinfo hints = {};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		addrinfo *addresses = nullptr;
		const string portString = to_string(port);
		if(getaddrinfo(nullptr, portString.c_str(), &hints, &addresses) != 0)
		{
			error = "Unable to resolve relay bind address";
			return INVALID_SOCKET_HANDLE;
		}

		SocketHandle listener = INVALID_SOCKET_HANDLE;
		for(addrinfo *address = addresses; address; address = address->ai_next)
		{
			SocketHandle socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
			if(!IsValid(socket))
				continue;

			const int yes = 1;
			setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
			if(::bind(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0 && listen(socket, 16) == 0)
			{
				listener = socket;
				break;
			}
			CloseSocket(socket);
		}
		freeaddrinfo(addresses);

		if(!IsValid(listener))
			error = "Unable to host relay on port " + portString;
		return listener;
	}



	string PeerHandshakeLine(const string &playerId)
	{
		return "peer\t" + to_string(CoOpRelay::PROTOCOL_VERSION) + "\t" + playerId;
	}



	optional<string> ParsePeerHandshake(const string &line)
	{
		string type;
		string protocolText;
		string playerId;
		string extra;
		istringstream in(line);
		if(!getline(in, type, '\t') || type != "peer")
			return nullopt;
		if(!getline(in, protocolText, '\t') || !getline(in, playerId, '\t') || getline(in, extra, '\t'))
			return nullopt;

		try
		{
			size_t parsed = 0;
			const unsigned long protocol = stoul(protocolText, &parsed);
			if(parsed != protocolText.size() || protocol != CoOpRelay::PROTOCOL_VERSION || playerId.empty())
				return nullopt;
		}
		catch(...)
		{
			return nullopt;
		}
		return playerId;
	}



	bool ReadSocketLines(SocketHandle socket, string &buffer, vector<string> &lines)
	{
		char chunk[4096];
#ifdef _WIN32
		const int count = recv(socket, chunk, sizeof(chunk), 0);
#else
		const ssize_t count = recv(socket, chunk, sizeof(chunk), 0);
#endif
		if(count <= 0)
			return false;

		for(int i = 0; i < count; ++i)
		{
			const char ch = chunk[i];
			if(ch == '\n')
			{
				if(!buffer.empty() && buffer.back() == '\r')
					buffer.pop_back();
				lines.push_back(std::move(buffer));
				buffer.clear();
			}
			else if(buffer.size() < 65536)
				buffer += ch;
		}
		return true;
	}



	bool ReadOneLine(SocketHandle socket, string &buffer, string &line)
	{
		vector<string> lines;
		while(lines.empty())
			if(!ReadSocketLines(socket, buffer, lines))
				return false;
		line = std::move(lines.front());
		if(lines.size() > 1)
		{
			for(size_t i = 1; i < lines.size(); ++i)
			{
				if(!buffer.empty())
					buffer += '\n';
				buffer += lines[i];
			}
		}
		return true;
	}



	string PlayerName(const PlayerInfo &player)
	{
		if(!player.IsLoaded())
			return "Player";

		string name = player.FirstName();
		if(!player.LastName().empty())
		{
			if(!name.empty())
				name += ' ';
			name += player.LastName();
		}
		return name.empty() ? "Player" : name;
	}



	string PresenceName(const CoOpRelay::PlayerSnapshot &snapshot)
	{
		return snapshot.name.empty() ? snapshot.playerId : snapshot.name;
	}



	string PresenceStatus(const CoOpRelay::PlayerSnapshot &snapshot, const string &localSystem)
	{
		string status = PresenceName(snapshot);
		if(snapshot.IsLanded())
			status += " - landed" + (snapshot.landedPlanet.empty() ? "" : " on " + snapshot.landedPlanet);
		else if(snapshot.system == localSystem)
			status += " - nearby" + (snapshot.shipModel.empty() ? "" : " / " + snapshot.shipModel);
		else
			status += " - " + (snapshot.system.empty() ? "unknown system" : snapshot.system);
		return status;
	}



	string FitText(const Font &font, string text, double maxWidth)
	{
		if(font.Width(text) <= maxWidth)
			return text;
		while(!text.empty() && font.Width(text + "...") > maxWidth)
			text.pop_back();
		return text.empty() ? "..." : text + "...";
	}



	string AuthorityStatus(const CoOpRelay::SystemAuthority &authority,
		const CoOpRelay::PresenceStore &remotes, bool isLocalAuthority)
	{
		string owner = authority.ownerId;
		if(isLocalAuthority)
			owner = "you";
		else if(const CoOpRelay::RemotePresence *presence = remotes.Get(authority.ownerId))
			owner = PresenceName(presence->latest);
		return "Authority: " + owner + " (" + to_string(authority.playerCount) + ")";
	}



	string MissionEventDescription(const CoOpRelay::SharedMissionEvent &event)
	{
		if(!event.detail.empty())
			return event.detail;

		switch(event.type)
		{
			case CoOpRelay::MissionEventType::ACCEPTED:
				return "Shared mission accepted.";
			case CoOpRelay::MissionEventType::OBJECTIVE_UPDATED:
				return "Shared mission objective updated.";
			case CoOpRelay::MissionEventType::NPC_DISABLED:
				return "Shared mission NPC disabled.";
			case CoOpRelay::MissionEventType::NPC_DESTROYED:
				return "Shared mission NPC destroyed.";
			case CoOpRelay::MissionEventType::NPC_BOARDED:
				return "Shared mission NPC boarded.";
			case CoOpRelay::MissionEventType::NPC_CAPTURED:
				return "Shared mission NPC captured.";
			case CoOpRelay::MissionEventType::COMPLETED:
				return "Shared mission completed.";
			case CoOpRelay::MissionEventType::FAILED:
				return "Shared mission failed.";
			case CoOpRelay::MissionEventType::REWARD:
				return "Shared mission reward updated.";
		}
		return "Shared mission updated.";
	}



	string ResourceEventDescription(const CoOpRelay::SharedResourceEvent &event)
	{
		if(!event.detail.empty())
			return event.detail;

		ostringstream out;
		out << "Co-op resource ";
		switch(event.status)
		{
			case CoOpRelay::ResourceActionStatus::REQUEST:
				out << "requested";
				break;
			case CoOpRelay::ResourceActionStatus::CONFIRMED:
				out << "confirmed";
				break;
			case CoOpRelay::ResourceActionStatus::REJECTED:
				out << "rejected";
				break;
			case CoOpRelay::ResourceActionStatus::APPLIED:
				out << "applied";
				break;
		}
		if(event.amount > 0.)
			out << ": " << event.amount << " " << event.resource;
		else if(!event.resource.empty())
			out << ": " << event.resource;
		out << ".";
		return out.str();
	}



	string ResourceName(CoOpRelay::ResourceActionType type)
	{
		switch(type)
		{
			case CoOpRelay::ResourceActionType::REFUEL_ASSIST:
			case CoOpRelay::ResourceActionType::FUEL_TRANSFER:
				return "fuel";
			case CoOpRelay::ResourceActionType::REPAIR_ASSIST:
				return "repair";
			case CoOpRelay::ResourceActionType::ENERGY_TRANSFER:
				return "energy";
			case CoOpRelay::ResourceActionType::CARGO_TRANSFER:
				return "cargo";
			case CoOpRelay::ResourceActionType::CREDIT_REWARD:
				return "credits";
		}
		return "resource";
	}



	string ResourceAssistReceivedText(CoOpRelay::ResourceActionType type)
	{
		switch(type)
		{
			case CoOpRelay::ResourceActionType::REFUEL_ASSIST:
				return "Co-op refuel assist received.";
			case CoOpRelay::ResourceActionType::REPAIR_ASSIST:
				return "Co-op repair assist received.";
			case CoOpRelay::ResourceActionType::FUEL_TRANSFER:
				return "Co-op fuel transfer received.";
			case CoOpRelay::ResourceActionType::ENERGY_TRANSFER:
				return "Co-op energy transfer received.";
			case CoOpRelay::ResourceActionType::CARGO_TRANSFER:
				return "Co-op cargo transfer received.";
			case CoOpRelay::ResourceActionType::CREDIT_REWARD:
				return "Co-op credit reward received.";
		}
		return "Co-op resource assist received.";
	}



	string ResourceAssistSentText(CoOpRelay::ResourceActionType type)
	{
		switch(type)
		{
			case CoOpRelay::ResourceActionType::REFUEL_ASSIST:
				return "Co-op refuel assist sent.";
			case CoOpRelay::ResourceActionType::REPAIR_ASSIST:
				return "Co-op repair assist sent.";
			case CoOpRelay::ResourceActionType::FUEL_TRANSFER:
				return "Co-op fuel transfer sent.";
			case CoOpRelay::ResourceActionType::ENERGY_TRANSFER:
				return "Co-op energy transfer sent.";
			case CoOpRelay::ResourceActionType::CARGO_TRANSFER:
				return "Co-op cargo transfer sent.";
			case CoOpRelay::ResourceActionType::CREDIT_REWARD:
				return "Co-op credit reward sent.";
		}
		return "Co-op resource assist sent.";
	}



	bool IsResourceEventVisibleTo(const string &localPlayerId, const CoOpRelay::SharedResourceEvent &event)
	{
		return event.targetPlayerId.empty() || event.targetPlayerId == localPlayerId || event.playerId == localPlayerId;
	}



	bool ApplyLocalResourceEvent(PlayerInfo &player, const string &localPlayerId,
		vector<string> &appliedResourceActions, const CoOpRelay::SharedResourceEvent &event)
	{
		if(event.status != CoOpRelay::ResourceActionStatus::APPLIED || event.actionId.empty() || event.amount <= 0.)
			return false;
		if(!event.targetPlayerId.empty() && event.targetPlayerId != localPlayerId)
			return false;
		if(find(appliedResourceActions.begin(), appliedResourceActions.end(), event.actionId)
				!= appliedResourceActions.end())
			return false;

		bool applied = false;
		if(event.type == CoOpRelay::ResourceActionType::CREDIT_REWARD)
		{
			const double capped = min(event.amount, static_cast<double>((numeric_limits<int64_t>::max)()));
			const int64_t credits = static_cast<int64_t>(capped);
			if(credits <= 0)
				return false;
			player.Accounts().AddCredits(credits);
			applied = true;
		}
		else
		{
			const shared_ptr<Ship> &flagship = player.FlagshipPtr();
			if(!flagship || flagship->IsDestroyed())
				return false;

			if(event.type == CoOpRelay::ResourceActionType::REFUEL_ASSIST)
			{
				flagship->Recharge(Port::RechargeType::Fuel, false);
				applied = true;
			}
			else if(event.type == CoOpRelay::ResourceActionType::REPAIR_ASSIST)
			{
				flagship->Recharge(Port::RechargeType::Shields | Port::RechargeType::Hull, false);
				applied = true;
			}
			else if(event.type == CoOpRelay::ResourceActionType::FUEL_TRANSFER)
			{
				flagship->SetCoOpProxyState(flagship->Shields(), flagship->Hull(),
					min(1., flagship->Fuel() + event.amount), flagship->Energy(), flagship->Heat(),
					flagship->IsDisabled());
				applied = true;
			}
			else if(event.type == CoOpRelay::ResourceActionType::ENERGY_TRANSFER)
			{
				flagship->SetCoOpProxyState(flagship->Shields(), flagship->Hull(), flagship->Fuel(),
					min(1., flagship->Energy() + event.amount), flagship->Heat(), flagship->IsDisabled());
				applied = true;
			}
		}
		if(!applied)
			return false;

		appliedResourceActions.push_back(event.actionId);
		return true;
	}



	void DrawRemoteShipSprite(const Ship &model, const Point &screen, double zoom, const Angle &facing)
	{
		const Sprite *sprite = model.GetSprite();
		if(!sprite || !sprite->Width() || !sprite->Height())
			return;

		double width = model.Width();
		double height = model.Height();
		if(!width || !height)
		{
			width = .5 * sprite->Width();
			height = .5 * sprite->Height();
		}

		SpriteShader::Item item;
		item.texture = sprite->Texture();
		item.swizzleMask = sprite->SwizzleMask();
		item.frame = model.GetFrame();
		item.frameCount = static_cast<float>(sprite->Frames());
		item.uniqueSwizzleMaskFrames = sprite->SwizzleMaskFrames() > 1;
		item.position[0] = static_cast<float>(screen.X());
		item.position[1] = static_cast<float>(screen.Y());

		Point unit = facing.Unit();
		Point uw = unit * width * zoom;
		Point uh = unit * height * zoom;
		item.transform[0] = static_cast<float>(-uw.Y());
		item.transform[1] = static_cast<float>(uw.X());
		item.transform[2] = static_cast<float>(-uh.X());
		item.transform[3] = static_cast<float>(-uh.Y());
		item.swizzle = Swizzle::None();

		SpriteShader::Bind();
		SpriteShader::Add(item);
		SpriteShader::Unbind();
	}



	string RelayControlLine(const string &type, uint64_t sequence)
	{
		return type + "\t" + to_string(CoOpRelay::PROTOCOL_VERSION) + "\t" + to_string(sequence);
	}



	bool ParseRelayControlLine(const string &line, const string &type, uint64_t &sequence)
	{
		string parsedType;
		string protocolText;
		string sequenceText;
		string extra;
		istringstream in(line);
		if(!getline(in, parsedType, '\t') || parsedType != type)
			return false;
		if(!getline(in, protocolText, '\t') || !getline(in, sequenceText, '\t') || getline(in, extra, '\t'))
			return false;

		try
		{
			size_t protocolParsed = 0;
			const unsigned long protocol = stoul(protocolText, &protocolParsed);
			if(protocolParsed != protocolText.size() || protocol != CoOpRelay::PROTOCOL_VERSION)
				return false;

			size_t sequenceParsed = 0;
			const unsigned long long parsedSequence = stoull(sequenceText, &sequenceParsed);
			if(sequenceParsed != sequenceText.size())
				return false;
			sequence = static_cast<uint64_t>(parsedSequence);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
}



struct DirectInboundLine {
	string playerId;
	string line;
};



class DirectPeerMesh {
public:
	~DirectPeerMesh()
	{
		Stop();
	}

	bool Start(string localPlayerId)
	{
		Stop();
		if(localPlayerId.empty())
			return false;

		string error;
		SocketHandle listener = ListenSocket(0, error);
		if(!IsValid(listener))
			return false;

		const uint16_t actualPort = SocketLocalPort(listener);
		if(!actualPort)
		{
			CloseSocket(listener);
			return false;
		}

		{
			lock_guard<std::mutex> lock(mutex);
			playerId = std::move(localPlayerId);
			listenSocket = listener;
			port = actualPort;
			stop = false;
			status = "Direct peer listener on port " + to_string(port);
		}
		acceptThread = thread(&DirectPeerMesh::AcceptLoop, this, listener);
		return true;
	}

	void Stop()
	{
		vector<SocketHandle> sockets;
		{
			lock_guard<std::mutex> lock(mutex);
			stop = true;
			if(IsValid(listenSocket))
			{
				sockets.push_back(listenSocket);
				listenSocket = INVALID_SOCKET_HANDLE;
			}
			for(const OutboundPeer &peer : outbound)
				sockets.push_back(peer.socket);
			for(SocketHandle socket : inboundSockets)
				sockets.push_back(socket);
			outbound.clear();
			inboundSockets.clear();
			port = 0;
			status = "Direct peer mesh stopped";
		}
		for(SocketHandle socket : sockets)
			CloseSocket(socket);

		if(acceptThread.joinable())
			acceptThread.join();

		vector<thread> threads;
		{
			lock_guard<std::mutex> lock(mutex);
			threads.swap(workerThreads);
			connecting.clear();
			attempted.clear();
			inbound.clear();
			playerId.clear();
		}
		for(thread &worker : threads)
			if(worker.joinable())
				worker.join();
	}

	bool IsRunning() const
	{
		lock_guard<std::mutex> lock(mutex);
		return IsValid(listenSocket);
	}

	uint16_t Port() const
	{
		lock_guard<std::mutex> lock(mutex);
		return port;
	}

	string StatusText() const
	{
		lock_guard<std::mutex> lock(mutex);
		return status;
	}

	int ConnectionCount() const
	{
		lock_guard<std::mutex> lock(mutex);
		return static_cast<int>(outbound.size());
	}

	void UpdatePeers(const vector<CoOpRelay::PeerEndpoint> &endpoints)
	{
		vector<CoOpRelay::PeerEndpoint> toConnect;
		{
			lock_guard<std::mutex> lock(mutex);
			if(stop || !IsValid(listenSocket))
				return;

			for(const CoOpRelay::PeerEndpoint &endpoint : endpoints)
			{
				if(!endpoint.IsValid() || endpoint.removed || endpoint.playerId == playerId)
					continue;
				const string key = EndpointKey(endpoint);
				if(HasOutboundLocked(endpoint.playerId) || Contains(connecting, key) || Contains(attempted, key))
					continue;
				connecting.push_back(key);
				attempted.push_back(key);
				toConnect.push_back(endpoint);
			}
		}

		for(CoOpRelay::PeerEndpoint &endpoint : toConnect)
		{
			lock_guard<std::mutex> lock(mutex);
			workerThreads.emplace_back(&DirectPeerMesh::ConnectLoop, this, std::move(endpoint));
		}
	}

	vector<DirectInboundLine> TakeInbound()
	{
		lock_guard<std::mutex> lock(mutex);
		vector<DirectInboundLine> result;
		result.assign(make_move_iterator(inbound.begin()), make_move_iterator(inbound.end()));
		inbound.clear();
		return result;
	}

	bool Send(const string &line)
	{
		vector<pair<string, SocketHandle>> peers;
		{
			lock_guard<std::mutex> lock(mutex);
			for(OutboundPeer &peer : outbound)
				peers.emplace_back(peer.playerId, peer.socket);
		}

		vector<SocketHandle> failed;
		int sent = 0;
		for(const auto &[playerId, socket] : peers)
		{
			if(SendLine(socket, line))
				++sent;
			else
				failed.push_back(socket);
		}

		if(!failed.empty())
		{
			for(SocketHandle socket : failed)
				CloseSocket(socket);

			lock_guard<std::mutex> lock(mutex);
			for(SocketHandle socket : failed)
				outbound.erase(remove_if(outbound.begin(), outbound.end(), [socket](const OutboundPeer &peer) {
					return peer.socket == socket;
				}), outbound.end());
		}

		return sent > 0;
	}

private:
	struct OutboundPeer {
		string playerId;
		SocketHandle socket = INVALID_SOCKET_HANDLE;
	};

	static bool Contains(const vector<string> &values, const string &value)
	{
		return find(values.begin(), values.end(), value) != values.end();
	}

	static string EndpointKey(const CoOpRelay::PeerEndpoint &endpoint)
	{
		return endpoint.playerId + "\t" + endpoint.host + "\t" + to_string(endpoint.port)
			+ "\t" + to_string(endpoint.sequence);
	}

	bool HasOutboundLocked(const string &remotePlayerId) const
	{
		return any_of(outbound.begin(), outbound.end(), [&remotePlayerId](const OutboundPeer &peer) {
			return peer.playerId == remotePlayerId;
		});
	}

	bool ShouldStop() const
	{
		lock_guard<std::mutex> lock(mutex);
		return stop;
	}

	string LocalPlayerId() const
	{
		lock_guard<std::mutex> lock(mutex);
		return playerId;
	}

	void AcceptLoop(SocketHandle listener)
	{
		while(!ShouldStop())
		{
			SocketHandle client = accept(listener, nullptr, nullptr);
			if(!IsValid(client))
				break;
			ConfigureTcpLowLatency(client);
			{
				lock_guard<std::mutex> lock(mutex);
				if(stop)
				{
					CloseSocket(client);
					break;
				}
				inboundSockets.push_back(client);
				workerThreads.emplace_back(&DirectPeerMesh::InboundLoop, this, client);
			}
		}
	}

	void ConnectLoop(CoOpRelay::PeerEndpoint endpoint)
	{
		string error;
		SocketHandle connected = ConnectSocket(endpoint.host, endpoint.port, error);
		const string key = EndpointKey(endpoint);
		if(!IsValid(connected))
		{
			lock_guard<std::mutex> lock(mutex);
			connecting.erase(remove(connecting.begin(), connecting.end(), key), connecting.end());
			return;
		}

		if(!SendLine(connected, PeerHandshakeLine(LocalPlayerId())))
		{
			CloseSocket(connected);
			lock_guard<std::mutex> lock(mutex);
			connecting.erase(remove(connecting.begin(), connecting.end(), key), connecting.end());
			return;
		}

		lock_guard<std::mutex> lock(mutex);
		connecting.erase(remove(connecting.begin(), connecting.end(), key), connecting.end());
		if(stop || HasOutboundLocked(endpoint.playerId))
		{
			CloseSocket(connected);
			return;
		}
		outbound.push_back({endpoint.playerId, connected});
		status = "Direct peers: " + to_string(outbound.size());
	}

	void InboundLoop(SocketHandle client)
	{
		string buffer;
		vector<string> lines;
		if(!ReadSocketLines(client, buffer, lines) || lines.empty())
		{
			ForgetInbound(client);
			return;
		}

		const optional<string> remotePlayerId = ParsePeerHandshake(lines.front());
		if(!remotePlayerId || *remotePlayerId == LocalPlayerId())
		{
			ForgetInbound(client);
			return;
		}

		if(!SendLine(client, PeerHandshakeLine(LocalPlayerId())))
		{
			ForgetInbound(client);
			return;
		}

		for(size_t i = 1; i < lines.size(); ++i)
			QueueInbound(*remotePlayerId, lines[i]);

		while(!ShouldStop())
		{
			lines.clear();
			if(!ReadSocketLines(client, buffer, lines))
				break;
			for(string &line : lines)
				QueueInbound(*remotePlayerId, std::move(line));
		}
		ForgetInbound(client);
	}

	void QueueInbound(const string &remotePlayerId, string line)
	{
		lock_guard<std::mutex> lock(mutex);
		if(inbound.size() < 4096)
			inbound.push_back({remotePlayerId, std::move(line)});
	}

	void ForgetInbound(SocketHandle socket)
	{
		CloseSocket(socket);
		lock_guard<std::mutex> lock(mutex);
		inboundSockets.erase(remove(inboundSockets.begin(), inboundSockets.end(), socket), inboundSockets.end());
	}

	mutable std::mutex mutex;
	bool stop = true;
	string playerId;
	SocketHandle listenSocket = INVALID_SOCKET_HANDLE;
	uint16_t port = 0;
	string status = "Direct peer mesh stopped";
	thread acceptThread;
	vector<thread> workerThreads;
	vector<OutboundPeer> outbound;
	vector<SocketHandle> inboundSockets;
	vector<string> connecting;
	vector<string> attempted;
	deque<DirectInboundLine> inbound;
};



bool IsDirectRealtimeLineFrom(const string &sourcePlayerId, const string &localPlayerId, const string &line)
{
	if(sourcePlayerId.empty() || sourcePlayerId == localPlayerId)
		return false;

	if(auto snapshot = CoOpRelay::ParseSnapshot(line))
		return snapshot->playerId == sourcePlayerId;
	if(auto event = CoOpRelay::ParseEvent(line))
		return event->playerId == sourcePlayerId;
	if(auto npc = CoOpRelay::ParseNPCSnapshot(line))
		return npc->ownerId == sourcePlayerId;
	if(auto damage = CoOpRelay::ParseNPCDamage(line))
		return damage->reporterId == sourcePlayerId;
	if(auto hit = CoOpRelay::ParseCombatHit(line))
		return hit->attackerId == sourcePlayerId && hit->targetPlayerId == localPlayerId;
	if(auto fire = CoOpRelay::ParseWeaponFire(line))
		return fire->playerId == sourcePlayerId;
	if(auto boarding = CoOpRelay::ParseNPCBoarding(line))
		return boarding->IsRequest() ? boarding->playerId == sourcePlayerId : boarding->ownerId == sourcePlayerId;

	return false;
}



bool IsDirectRealtimeOutgoing(const string &line)
{
	return CoOpRelay::ParseSnapshot(line).has_value()
		|| CoOpRelay::ParseEvent(line).has_value()
		|| CoOpRelay::ParseNPCSnapshot(line).has_value()
		|| CoOpRelay::ParseNPCDamage(line).has_value()
		|| CoOpRelay::ParseCombatHit(line).has_value()
		|| CoOpRelay::ParseWeaponFire(line).has_value()
		|| CoOpRelay::ParseNPCBoarding(line).has_value();
}



class CoOpRelayController::NetworkClient {
public:
	~NetworkClient()
	{
		Disconnect();
	}

	void Connect(string host, uint16_t port, string playerName, string password)
	{
		Disconnect();

		session.StartJoin(playerName, password);
		{
			lock_guard<std::mutex> lock(mutex);
			stop = false;
			failed = false;
			status = "Connecting to " + host + ":" + to_string(port);
			nextPingSequence = 1;
			pendingPingSequence = 0;
			lastPingSent = {};
			pendingPingSent = {};
			relayLatencyMs = -1;
			nextResyncSequence = 1;
			pendingResyncSequence = 0;
			snapshotRateWindowStart = {};
			snapshotsInRateWindow = 0;
			clientSnapshotRate = 0.;
			outbound.clear();
		}

		reader = thread(&NetworkClient::ReadLoop, this,
			std::move(host), port, std::move(playerName), std::move(password));
		writer = thread(&NetworkClient::WriteLoop, this);
	}

	void Disconnect()
	{
		directPeers.Stop();

		SocketHandle toClose = INVALID_SOCKET_HANDLE;
		{
			lock_guard<std::mutex> lock(mutex);
			stop = true;
			toClose = socket;
			socket = INVALID_SOCKET_HANDLE;
		}
		outboundCondition.notify_all();
		CloseSocket(toClose);
		if(reader.joinable())
			reader.join();
		if(writer.joinable())
			writer.join();

		session.Disconnect();
		lock_guard<std::mutex> lock(mutex);
		inbound.clear();
		outbound.clear();
		failed = false;
		status = "Disconnected";
		pendingPingSequence = 0;
		pendingResyncSequence = 0;
		relayLatencyMs = -1;
		snapshotRateWindowStart = {};
		snapshotsInRateWindow = 0;
		clientSnapshotRate = 0.;
		relayLocalHost.clear();
		directEndpointPublished = false;
		nextDirectEndpointSequence = 1;
	}

	void Step(PlayerInfo &player)
	{
		vector<string> lines;
		bool shouldError = false;
		string failureStatus;
		{
			lock_guard<std::mutex> lock(mutex);
			const size_t count = min(RELAY_INBOUND_PROCESS_LIMIT, inbound.size());
			for(size_t i = 0; i < count; ++i)
			{
				lines.push_back(std::move(inbound.front()));
				inbound.pop_front();
			}
			shouldError = failed;
			if(shouldError)
				failureStatus = status;
		}
		for(const string &line : lines)
		{
			uint64_t pongSequence = 0;
			if(ParseRelayControlLine(line, "pong", pongSequence))
			{
				RecordPong(pongSequence);
				continue;
			}
			uint64_t resyncSequence = 0;
			if(ParseRelayControlLine(line, "resync-done", resyncSequence))
			{
				RecordResyncDone(resyncSequence);
				continue;
			}
			if(session.State() == CoOpRelay::ConnectionState::CONNECTING)
				session.AcceptWelcome(line);
			else
				session.ReceiveRelayLine(line);
		}

		const string localPlayerId = session.PlayerId();
		if(session.State() == CoOpRelay::ConnectionState::CONNECTED && !localPlayerId.empty())
			for(const DirectInboundLine &direct : directPeers.TakeInbound())
				if(IsDirectRealtimeLineFrom(direct.playerId, localPlayerId, direct.line))
					session.ReceiveRelayLine(direct.line);

		if(shouldError && session.State() != CoOpRelay::ConnectionState::ERROR)
			session.SetError(std::move(failureStatus));

		if(session.State() != CoOpRelay::ConnectionState::CONNECTED)
			return;

		StartDirectPeerMeshIfReady();
		directPeers.UpdatePeers(session.PeerEndpoints().All());
		session.StepConnectionHealth();

		for(const CoOpRelay::OutgoingMessage &message : session.StepLocal(player))
		{
			if(!QueueSend(message.line, message.isSnapshot))
			{
				Fail("Relay send queue failed");
				session.SetError();
				return;
			}
			if(message.isSnapshot)
				RecordSnapshotSent();
			if(IsDirectRealtimeOutgoing(message.line))
				directPeers.Send(message.line);
		}

		if(!SendPingIfDue())
		{
			Fail("Relay ping failed");
			session.SetError();
		}
	}

	void SetSimulationActive(bool active)
	{
		session.SetSimulationActive(active);
	}

	bool IsConnected() const
	{
		return session.State() == CoOpRelay::ConnectionState::CONNECTED;
	}

	int DirectPeerCount() const
	{
		return directPeers.ConnectionCount();
	}

	string StatusText() const
	{
		lock_guard<std::mutex> lock(mutex);
		string result = session.StatusText();
		if(!status.empty() && status != result)
			result += " - " + status;
		if(relayLatencyMs >= 0)
			result += " - " + to_string(relayLatencyMs) + " ms";
		else if(session.State() == CoOpRelay::ConnectionState::CONNECTED && pendingPingSequence)
			result += " - pinging";
		if(directPeers.ConnectionCount() > 0)
			result += " - direct peers " + to_string(directPeers.ConnectionCount());
		if(pendingResyncSequence)
			result += " - resyncing";
		return result;
	}

	CoOpRelay::Diagnostics GetDiagnostics() const
	{
		CoOpRelay::Diagnostics diagnostics = session.GetDiagnostics();
		lock_guard<std::mutex> lock(mutex);
		diagnostics.clientSnapshotRate = clientSnapshotRate;
		diagnostics.latencyMs = relayLatencyMs;
		return diagnostics;
	}

	const CoOpRelay::PresenceStore &Remotes() const
	{
		return session.Remotes();
	}

	const CoOpRelay::AuthorityStore &Authorities() const
	{
		return session.Authorities();
	}

	const CoOpRelay::SharedNPCStore &SharedNPCs() const
	{
		return session.SharedNPCs();
	}

	const CoOpRelay::SharedMissionEventLog &MissionEvents() const
	{
		return session.MissionEvents();
	}

	const CoOpRelay::SharedResourceEventLog &ResourceEvents() const
	{
		return session.ResourceEvents();
	}

	string PlayerId() const
	{
		return session.PlayerId();
	}

	bool PublishPeerEndpoint(const CoOpRelay::PeerEndpoint &endpoint)
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED || endpoint.playerId != session.PlayerId())
			return false;
		return QueueSend(CoOpRelay::Serialize(endpoint));
	}

	bool PublishSharedNPC(const CoOpRelay::SharedNPCSnapshot &snapshot)
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED || snapshot.ownerId != session.PlayerId())
			return false;
		const string line = CoOpRelay::Serialize(snapshot);
		if(!QueueSend(line))
			return false;
		directPeers.Send(line);
		return true;
	}

	bool PublishSharedNPCDamage(const CoOpRelay::SharedNPCDamage &damage)
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED || damage.reporterId != session.PlayerId())
			return false;
		const string line = CoOpRelay::Serialize(damage);
		if(!QueueSend(line))
			return false;
		directPeers.Send(line);
		return true;
	}

	bool PublishSharedCombatHit(const CoOpRelay::SharedCombatHit &hit)
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED || hit.attackerId != session.PlayerId())
			return false;
		const string line = CoOpRelay::Serialize(hit);
		if(!QueueSend(line))
			return false;
		directPeers.Send(line);
		return true;
	}

	bool PublishSharedWeaponFire(const CoOpRelay::SharedWeaponFire &fire)
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED || fire.playerId != session.PlayerId())
			return false;
		const string line = CoOpRelay::Serialize(fire);
		if(!QueueSend(line))
			return false;
		directPeers.Send(line);
		return true;
	}

	vector<CoOpRelay::SharedNPCDamage> TakeNPCDamageReports()
	{
		return session.TakeNPCDamageReports();
	}

	vector<CoOpRelay::SharedCombatHit> TakeCombatHits()
	{
		return session.TakeCombatHits();
	}

	vector<CoOpRelay::SharedWeaponFire> TakeWeaponFires()
	{
		return session.TakeWeaponFires();
	}

	bool PublishSharedNPCBoarding(const CoOpRelay::SharedNPCBoarding &boarding)
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED)
			return false;
		const string &playerId = session.PlayerId();
		if(boarding.IsRequest() ? boarding.playerId != playerId : boarding.ownerId != playerId)
			return false;
		const string line = CoOpRelay::Serialize(boarding);
		if(!QueueSend(line))
			return false;
		directPeers.Send(line);
		return true;
	}

	vector<CoOpRelay::SharedNPCBoarding> TakeNPCBoardingReports()
	{
		return session.TakeNPCBoardingReports();
	}

	bool PublishSharedMissionEvent(const CoOpRelay::SharedMissionEvent &event)
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED || event.playerId != session.PlayerId())
			return false;
		return QueueSend(CoOpRelay::Serialize(event));
	}

	vector<CoOpRelay::SharedMissionEvent> TakeMissionEvents()
	{
		return session.TakeMissionEvents();
	}

	bool PublishSharedResourceEvent(const CoOpRelay::SharedResourceEvent &event)
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED || event.playerId != session.PlayerId())
			return false;
		return QueueSend(CoOpRelay::Serialize(event));
	}

	vector<CoOpRelay::SharedResourceEvent> TakeResourceEvents()
	{
		return session.TakeResourceEvents();
	}

	bool IsSystemAuthority(const string &system) const
	{
		return session.IsSystemAuthority(system);
	}

	const vector<CoOpRelay::PlayerEvent> &RecentEvents() const
	{
		return session.RecentEvents();
	}

	const vector<string> &DesyncWarnings() const
	{
		return session.DesyncWarnings();
	}

	bool RequestResync()
	{
		if(session.State() != CoOpRelay::ConnectionState::CONNECTED)
			return false;

		uint64_t sequence = 0;
		{
			lock_guard<std::mutex> lock(mutex);
			if(pendingResyncSequence)
				return true;
			sequence = nextResyncSequence++;
			pendingResyncSequence = sequence;
		}
		if(QueueSend(RelayControlLine("resync", sequence)))
			return true;

		lock_guard<std::mutex> lock(mutex);
		if(pendingResyncSequence == sequence)
			pendingResyncSequence = 0;
		return false;
	}

private:
	void ReadLoop(string host, uint16_t port, string playerName, string password)
	{
		string error;
		SocketHandle connected = INVALID_SOCKET_HANDLE;
		const auto connectDeadline = chrono::steady_clock::now()
			+ chrono::milliseconds(RELAY_CONNECT_RETRY_MS);
		do
		{
			connected = ConnectSocket(host, port, error);
			if(IsValid(connected) || ShouldStop())
				break;
			this_thread::sleep_for(chrono::milliseconds(100));
		} while(chrono::steady_clock::now() < connectDeadline);

		if(!IsValid(connected))
		{
			Fail(error);
			return;
		}

		{
			lock_guard<std::mutex> lock(mutex);
			if(stop)
			{
				CloseSocket(connected);
				return;
			}
			socket = connected;
			status = "Relay socket open";
			relayLocalHost = SocketLocalHost(connected);
			if(relayLocalHost == "::1")
				relayLocalHost = "127.0.0.1";
		}

		if(!SendLine(connected, CoOpRelay::ClientSession::JoinLine(playerName, password)))
		{
			Fail("Relay join failed");
			CloseOwnedSocket(connected);
			return;
		}

		string buffer;
		while(!ShouldStop())
		{
			vector<string> lines;
			if(!ReadSocketLines(connected, buffer, lines))
				break;
			lock_guard<std::mutex> lock(mutex);
			for(string &line : lines)
			{
				if(inbound.size() >= RELAY_INBOUND_QUEUE_LIMIT)
					inbound.pop_front();
				inbound.push_back(std::move(line));
			}
		}

		if(!ShouldStop())
			Fail("Relay disconnected");
		CloseOwnedSocket(connected);
	}

	bool QueueSend(string line, bool replaceSnapshot = false)
	{
		{
			lock_guard<std::mutex> lock(mutex);
			if(stop || !IsValid(socket))
				return false;

			if(replaceSnapshot)
				outbound.erase(remove_if(outbound.begin(), outbound.end(), [](const string &queued) {
					return queued.compare(0, 9, "snapshot\t") == 0;
				}), outbound.end());

			if(outbound.size() >= RELAY_OUTBOUND_QUEUE_LIMIT)
			{
				if(replaceSnapshot)
					return true;
				outbound.pop_front();
			}
			outbound.push_back(std::move(line));
		}
		outboundCondition.notify_one();
		return true;
	}

	void RecordSnapshotSent()
	{
		lock_guard<std::mutex> lock(mutex);
		const auto now = chrono::steady_clock::now();
		if(snapshotRateWindowStart == chrono::steady_clock::time_point{})
			snapshotRateWindowStart = now;
		++snapshotsInRateWindow;
		const auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - snapshotRateWindowStart);
		if(elapsed.count() >= 1000)
		{
			clientSnapshotRate = snapshotsInRateWindow * 1000. / max<int64_t>(1, elapsed.count());
			snapshotsInRateWindow = 0;
			snapshotRateWindowStart = now;
		}
	}

	void WriteLoop()
	{
		while(true)
		{
			string line;
			SocketHandle current = INVALID_SOCKET_HANDLE;
			{
				unique_lock<std::mutex> lock(mutex);
				outboundCondition.wait(lock, [this] {
					return stop || !outbound.empty();
				});
				if(stop && outbound.empty())
					return;
				line = std::move(outbound.front());
				outbound.pop_front();
				current = socket;
			}

			if(IsValid(current) && SendLine(current, line))
				continue;

			Fail("Relay send failed");
			CloseOwnedSocket(current);
			{
				lock_guard<std::mutex> lock(mutex);
				outbound.clear();
			}
			return;
		}
	}

	void StartDirectPeerMeshIfReady()
	{
		if(directEndpointPublished || session.PlayerId().empty())
			return;
		if(!directPeers.IsRunning() && !directPeers.Start(session.PlayerId()))
			return;

		string host;
		{
			lock_guard<std::mutex> lock(mutex);
			host = relayLocalHost;
		}
		if(host.empty())
			return;

		CoOpRelay::PeerEndpoint endpoint;
		endpoint.sequence = nextDirectEndpointSequence++;
		endpoint.playerId = session.PlayerId();
		endpoint.host = std::move(host);
		endpoint.port = directPeers.Port();
		if(endpoint.IsValid() && QueueSend(CoOpRelay::Serialize(endpoint)))
			directEndpointPublished = true;
	}

	bool SendPingIfDue()
	{
		const auto now = chrono::steady_clock::now();
		uint64_t sequence = 0;
		{
			lock_guard<std::mutex> lock(mutex);
			if(pendingPingSequence && now - pendingPingSent < chrono::milliseconds(RELAY_PING_TIMEOUT_MS))
				return true;
			if(!pendingPingSequence && lastPingSent.time_since_epoch().count()
					&& now - lastPingSent < chrono::milliseconds(RELAY_PING_INTERVAL_MS))
				return true;

			sequence = nextPingSequence++;
			pendingPingSequence = sequence;
			lastPingSent = now;
			pendingPingSent = now;
		}
		return QueueSend(RelayControlLine("ping", sequence));
	}

	void RecordPong(uint64_t sequence)
	{
		const auto now = chrono::steady_clock::now();
		lock_guard<std::mutex> lock(mutex);
		if(sequence != pendingPingSequence)
			return;

		relayLatencyMs = static_cast<int>(chrono::duration_cast<chrono::milliseconds>(now - pendingPingSent).count());
		pendingPingSequence = 0;
	}

	void RecordResyncDone(uint64_t sequence)
	{
		lock_guard<std::mutex> lock(mutex);
		if(sequence == pendingResyncSequence)
			pendingResyncSequence = 0;
	}

	void Fail(string message)
	{
		lock_guard<std::mutex> lock(mutex);
		if(!stop)
		{
			failed = true;
			status = std::move(message);
		}
	}

	bool ShouldStop() const
	{
		lock_guard<std::mutex> lock(mutex);
		return stop;
	}

	void CloseOwnedSocket(SocketHandle owned)
	{
		bool shouldClose = false;
		{
			lock_guard<std::mutex> lock(mutex);
			if(socket == owned)
			{
				socket = INVALID_SOCKET_HANDLE;
				shouldClose = true;
			}
		}
		if(shouldClose)
			CloseSocket(owned);
	}

private:
	mutable std::mutex mutex;
	thread reader;
	thread writer;
	condition_variable outboundCondition;
	SocketHandle socket = INVALID_SOCKET_HANDLE;
	bool stop = false;
	bool failed = false;
	string status = "Disconnected";
	deque<string> inbound;
	deque<string> outbound;
	CoOpRelay::ClientSession session;
	uint64_t nextPingSequence = 1;
	uint64_t pendingPingSequence = 0;
	chrono::steady_clock::time_point lastPingSent;
	chrono::steady_clock::time_point pendingPingSent;
	int relayLatencyMs = -1;
	uint64_t nextResyncSequence = 1;
	uint64_t pendingResyncSequence = 0;
	chrono::steady_clock::time_point snapshotRateWindowStart;
	uint64_t snapshotsInRateWindow = 0;
	double clientSnapshotRate = 0.;
	DirectPeerMesh directPeers;
	string relayLocalHost;
	bool directEndpointPublished = false;
	uint64_t nextDirectEndpointSequence = 1;
};



class CoOpRelayController::LocalRelayHost {
public:
	~LocalRelayHost()
	{
		Stop();
	}

	bool Start(uint16_t port, string password = {}, bool serverWorldEnabled = false)
	{
		Stop();

		string error;
		SocketHandle listener = ListenSocket(port, error);
		if(!IsValid(listener))
		{
			lock_guard<std::mutex> lock(mutex);
			status = std::move(error);
			return false;
		}

		{
			lock_guard<std::mutex> lock(mutex);
			this->port = port;
			listenSocket = listener;
			core = CoOpRelay::RelayServerCore();
			core.SetServerWorldEnabled(serverWorldEnabled);
			core.SetRoomPassword(std::move(password));
			status = "Hosting on port " + to_string(port);
			if(serverWorldEnabled)
				status += " (experimental server-world)";
			serverTickRate = 0.;
		}
		stop = false;
		acceptThread = thread(&LocalRelayHost::AcceptLoop, this, listener);
		worldThread = thread(&LocalRelayHost::WorldLoop, this);
		return true;
	}

	void Stop()
	{
		stop = true;

		SocketHandle listener = INVALID_SOCKET_HANDLE;
		vector<SocketHandle> clients;
		{
			lock_guard<std::mutex> lock(mutex);
			listener = listenSocket;
			listenSocket = INVALID_SOCKET_HANDLE;
			for(const auto &[id, socket] : peerSockets)
				clients.push_back(socket);
			peerSockets.clear();
		}

		CloseSocket(listener);
		for(SocketHandle socket : clients)
			CloseSocket(socket);

		if(acceptThread.joinable())
			acceptThread.join();
		if(worldThread.joinable())
			worldThread.join();

		vector<thread> threads;
		{
			lock_guard<std::mutex> lock(mutex);
			threads.swap(clientThreads);
			core = CoOpRelay::RelayServerCore();
			status = "Host stopped";
			port = 0;
			serverTickRate = 0.;
		}
		for(thread &clientThread : threads)
			if(clientThread.joinable())
				clientThread.join();
	}

	bool IsRunning() const
	{
		lock_guard<std::mutex> lock(mutex);
		return IsValid(listenSocket);
	}

	string StatusText() const
	{
		lock_guard<std::mutex> lock(mutex);
		return status;
	}

	uint16_t Port() const
	{
		lock_guard<std::mutex> lock(mutex);
		return port;
	}

	bool HasPassword() const
	{
		lock_guard<std::mutex> lock(mutex);
		return core.HasRoomPassword();
	}

	size_t PlayerCount() const
	{
		lock_guard<std::mutex> lock(mutex);
		return core.PlayerCount();
	}

	CoOpRelay::Diagnostics GetDiagnostics() const
	{
		lock_guard<std::mutex> lock(mutex);
		CoOpRelay::Diagnostics diagnostics = core.GetDiagnostics();
		diagnostics.serverTickRate = serverTickRate;
		return diagnostics;
	}

	void SetPassword(string password)
	{
		lock_guard<std::mutex> lock(mutex);
		core.SetRoomPassword(std::move(password));
	}

private:
	void AcceptLoop(SocketHandle listener)
	{
		while(!stop)
		{
			SocketHandle client = accept(listener, nullptr, nullptr);
			if(!IsValid(client))
				break;
			ConfigureTcpLowLatency(client);
			lock_guard<std::mutex> lock(mutex);
			clientThreads.emplace_back(&LocalRelayHost::ClientLoop, this, client);
		}
	}

	struct CachedState {
		vector<CoOpRelay::PlayerSnapshot> snapshots;
		vector<CoOpRelay::SystemAuthority> authorities;
		vector<CoOpRelay::PeerEndpoint> peerEndpoints;
		vector<CoOpRelay::SharedNPCSnapshot> npcs;
		vector<CoOpRelay::SharedMissionEvent> missionEvents;
		vector<CoOpRelay::SharedResourceEvent> resourceEvents;
	};

	CachedState CachedStateFor(const string &playerId) const
	{
		CachedState cached;
		cached.snapshots = core.LatestSnapshotsFor(playerId);
		cached.authorities = core.LatestAuthoritiesFor(playerId);
		cached.peerEndpoints = core.LatestPeerEndpointsFor(playerId);
		cached.npcs = core.LatestNPCsFor(playerId);
		cached.missionEvents = core.LatestMissionEventsFor(playerId);
		cached.resourceEvents = core.LatestResourceEventsFor(playerId);
		return cached;
	}

	bool SendCachedState(SocketHandle client, const CachedState &cached) const
	{
		for(const CoOpRelay::PlayerSnapshot &snapshot : cached.snapshots)
			if(!SendLine(client, CoOpRelay::Serialize(snapshot)))
				return false;
		for(const CoOpRelay::SystemAuthority &authority : cached.authorities)
			if(!SendLine(client, CoOpRelay::Serialize(authority)))
				return false;
		for(const CoOpRelay::PeerEndpoint &endpoint : cached.peerEndpoints)
			if(!SendLine(client, CoOpRelay::Serialize(endpoint)))
				return false;
		for(const CoOpRelay::SharedNPCSnapshot &npc : cached.npcs)
			if(!SendLine(client, CoOpRelay::Serialize(npc)))
				return false;
		for(const CoOpRelay::SharedMissionEvent &event : cached.missionEvents)
			if(!SendLine(client, CoOpRelay::Serialize(event)))
				return false;
		for(const CoOpRelay::SharedResourceEvent &event : cached.resourceEvents)
			if(!SendLine(client, CoOpRelay::Serialize(event)))
				return false;
		return true;
	}

	void ClientLoop(SocketHandle client)
	{
		string buffer;
		string joinLine;
		if(!ReadOneLine(client, buffer, joinLine))
		{
			CloseSocket(client);
			return;
		}

		optional<CoOpRelay::JoinRequest> request = CoOpRelay::ParseJoinRequest(joinLine);
		if(!request)
		{
			CloseSocket(client);
			return;
		}

		string playerId;
		CachedState cached;
		{
			lock_guard<std::mutex> lock(mutex);
			CoOpRelay::JoinResult join = core.TryJoin(*request);
			if(!join.accepted)
			{
				SendLine(client, "reject\t" + to_string(CoOpRelay::PROTOCOL_VERSION) + "\t" + join.message);
				CloseSocket(client);
				return;
			}
			playerId = std::move(join.playerId);
			peerSockets[playerId] = client;
			cached = CachedStateFor(playerId);
		}

		if(!SendLine(client, "welcome\t" + to_string(CoOpRelay::PROTOCOL_VERSION) + "\t" + playerId))
		{
			RemoveClient(playerId, client);
			return;
		}
		if(!SendCachedState(client, cached))
		{
			RemoveClient(playerId, client);
			return;
		}

		while(!stop)
		{
			vector<string> lines;
			if(!ReadSocketLines(client, buffer, lines))
				break;
			for(const string &line : lines)
				DispatchLine(playerId, line);
		}

		RemoveClient(playerId, client);
	}

	void DispatchLine(const string &senderId, const string &line)
	{
		uint64_t pingSequence = 0;
		if(ParseRelayControlLine(line, "ping", pingSequence))
		{
			SendPong(senderId, pingSequence);
			return;
		}
		uint64_t resyncSequence = 0;
		if(ParseRelayControlLine(line, "resync", resyncSequence))
		{
			SendResync(senderId, resyncSequence);
			return;
		}

		vector<CoOpRelay::RelayDelivery> deliveries;
		if(auto snapshot = CoOpRelay::ParseSnapshot(line))
		{
			if(snapshot->playerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*snapshot);
		}
		else if(auto event = CoOpRelay::ParseEvent(line))
		{
			if(event->playerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*event);
		}
		else if(auto endpoint = CoOpRelay::ParsePeerEndpoint(line))
		{
			if(endpoint->playerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*endpoint);
		}
		else if(auto npc = CoOpRelay::ParseNPCSnapshot(line))
		{
			if(npc->ownerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*npc);
		}
		else if(auto damage = CoOpRelay::ParseNPCDamage(line))
		{
			if(damage->reporterId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*damage);
		}
		else if(auto hit = CoOpRelay::ParseCombatHit(line))
		{
			if(hit->attackerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*hit);
		}
		else if(auto fire = CoOpRelay::ParseWeaponFire(line))
		{
			if(fire->playerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*fire);
		}
		else if(auto boarding = CoOpRelay::ParseNPCBoarding(line))
		{
			if(boarding->IsRequest() ? boarding->playerId != senderId : boarding->ownerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*boarding);
		}
		else if(auto mission = CoOpRelay::ParseMissionEvent(line))
		{
			if(mission->playerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*mission);
		}
		else if(auto resource = CoOpRelay::ParseResourceEvent(line))
		{
			if(resource->playerId != senderId)
				return;
			lock_guard<std::mutex> lock(mutex);
			deliveries = core.Receive(*resource);
		}
		else
			return;

		BroadcastDeliveries(deliveries);
	}

	static optional<string> SerializeDelivery(const CoOpRelay::RelayDelivery &delivery)
	{
		if(delivery.snapshot)
			return CoOpRelay::Serialize(*delivery.snapshot);
		if(delivery.event)
			return CoOpRelay::Serialize(*delivery.event);
		if(delivery.authority)
			return CoOpRelay::Serialize(*delivery.authority);
		if(delivery.peerEndpoint)
			return CoOpRelay::Serialize(*delivery.peerEndpoint);
		if(delivery.npc)
			return CoOpRelay::Serialize(*delivery.npc);
		if(delivery.npcDamage)
			return CoOpRelay::Serialize(*delivery.npcDamage);
		if(delivery.combatHit)
			return CoOpRelay::Serialize(*delivery.combatHit);
		if(delivery.weaponFire)
			return CoOpRelay::Serialize(*delivery.weaponFire);
		if(delivery.npcBoarding)
			return CoOpRelay::Serialize(*delivery.npcBoarding);
		if(delivery.missionEvent)
			return CoOpRelay::Serialize(*delivery.missionEvent);
		if(delivery.resourceEvent)
			return CoOpRelay::Serialize(*delivery.resourceEvent);
		return nullopt;
	}

	void BroadcastDeliveries(const vector<CoOpRelay::RelayDelivery> &deliveries)
	{
		vector<pair<SocketHandle, string>> outgoing;
		vector<pair<string, SocketHandle>> recipients;
		{
			lock_guard<std::mutex> lock(mutex);
			for(const CoOpRelay::RelayDelivery &delivery : deliveries)
			{
				auto it = peerSockets.find(delivery.recipientId);
				if(it == peerSockets.end())
					continue;
				if(optional<string> message = SerializeDelivery(delivery))
				{
					outgoing.emplace_back(it->second, std::move(*message));
					recipients.emplace_back(delivery.recipientId, it->second);
				}
			}
		}

		set<SocketHandle> failed;
		for(const auto &[socket, message] : outgoing)
			if(!failed.contains(socket) && !SendLine(socket, message))
				failed.insert(socket);

		for(const auto &[playerId, socket] : recipients)
			if(failed.erase(socket))
				RemoveClient(playerId, socket);
	}

	void WorldLoop()
	{
		auto nextStep = chrono::steady_clock::now();
		auto tickWindowStart = nextStep;
		uint64_t ticksInWindow = 0;
		while(!stop)
		{
			vector<CoOpRelay::RelayDelivery> deliveries;
			{
				lock_guard<std::mutex> lock(mutex);
				deliveries = core.StepServerWorld();
				++ticksInWindow;
				const auto now = chrono::steady_clock::now();
				const auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - tickWindowStart);
				if(elapsed.count() >= 1000)
				{
					serverTickRate = ticksInWindow * 1000. / max<int64_t>(1, elapsed.count());
					ticksInWindow = 0;
					tickWindowStart = now;
				}
			}
			BroadcastDeliveries(deliveries);
			nextStep += chrono::milliseconds(16);
			this_thread::sleep_until(nextStep);
			if(chrono::steady_clock::now() > nextStep + chrono::milliseconds(100))
				nextStep = chrono::steady_clock::now();
		}
	}

	void SendPong(const string &playerId, uint64_t sequence)
	{
		SocketHandle socket = INVALID_SOCKET_HANDLE;
		{
			lock_guard<std::mutex> lock(mutex);
			auto it = peerSockets.find(playerId);
			if(it != peerSockets.end())
				socket = it->second;
		}
		if(IsValid(socket))
			if(!SendLine(socket, RelayControlLine("pong", sequence)))
				RemoveClient(playerId, socket);
	}

	void SendResync(const string &playerId, uint64_t sequence)
	{
		SocketHandle socket = INVALID_SOCKET_HANDLE;
		CachedState cached;
		{
			lock_guard<std::mutex> lock(mutex);
			auto it = peerSockets.find(playerId);
			if(it == peerSockets.end())
				return;
			socket = it->second;
			cached = CachedStateFor(playerId);
		}

		if(!SendCachedState(socket, cached) || !SendLine(socket, RelayControlLine("resync-done", sequence)))
			RemoveClient(playerId, socket);
	}

	void RemoveClient(const string &playerId, SocketHandle client)
	{
		bool shouldClose = true;
		vector<CoOpRelay::RelayDelivery> deliveries;
		{
			lock_guard<std::mutex> lock(mutex);
			auto it = peerSockets.find(playerId);
			if(it != peerSockets.end() && it->second == client)
				peerSockets.erase(it);
			else
				shouldClose = false;
			deliveries = core.Leave(playerId);
		}
		if(shouldClose)
			CloseSocket(client);
		BroadcastDeliveries(deliveries);
	}

private:
	mutable std::mutex mutex;
	atomic_bool stop{true};
	thread acceptThread;
	thread worldThread;
	vector<thread> clientThreads;
	SocketHandle listenSocket = INVALID_SOCKET_HANDLE;
	uint16_t port = 0;
	string status = "Host stopped";
	CoOpRelay::RelayServerCore core;
	double serverTickRate = 0.;
	map<string, SocketHandle> peerSockets;
};



class CoOpRelayController::DetachedRelayHost {
public:
	~DetachedRelayHost()
	{
		CloseHandles();
	}

	bool Start(uint16_t port, string roomName, string password, bool serverWorldEnabled = false)
	{
		CloseHandles();
		const bool passwordProtected = !password.empty();
#ifdef _WIN32
		string executable = CurrentExecutablePath();
		if(executable.empty())
		{
			SetStatus("Detached relay unavailable: unable to find executable path");
			return false;
		}

		string command = CommandLineQuote(executable);
		command += " --coop-relay-server " + to_string(port);
		command += " --coop-relay-room " + CommandLineQuote(std::move(roomName));
		if(passwordProtected)
			command += " --coop-relay-password " + CommandLineQuote(std::move(password));
		if(serverWorldEnabled)
			command += " --coop-server-world";

		STARTUPINFOA startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION info = {};
		vector<char> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back('\0');
		const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
		if(!CreateProcessA(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
				flags, nullptr, nullptr, &startup, &info))
		{
			SetStatus("Detached relay unavailable: CreateProcess failed with "
				+ to_string(GetLastError()));
			return false;
		}

		{
			lock_guard<std::mutex> lock(mutex);
			process = info.hProcess;
			thread = info.hThread;
			this->port = port;
			this->passwordProtected = passwordProtected;
			status = "Detached relay on port " + to_string(port);
		}
		return true;
#else
		(void)port;
		(void)roomName;
		(void)password;
		(void)passwordProtected;
		SetStatus("Detached relay unavailable on this platform");
		return false;
#endif
	}

	void Stop()
	{
#ifdef _WIN32
		HANDLE processToStop = nullptr;
		{
			lock_guard<std::mutex> lock(mutex);
			if(process && ProcessIsRunning(process))
				processToStop = process;
		}
		if(processToStop)
		{
			TerminateProcess(processToStop, 0);
			WaitForSingleObject(processToStop, 2000);
		}
#endif
		CloseHandles();
		SetStatus("Detached host stopped");
	}

	bool IsRunning() const
	{
#ifdef _WIN32
		lock_guard<std::mutex> lock(mutex);
		return process && ProcessIsRunning(process);
#else
		return false;
#endif
	}

	string StatusText() const
	{
		lock_guard<std::mutex> lock(mutex);
#ifdef _WIN32
		if(process && !ProcessIsRunning(process))
			return "Detached relay exited";
#endif
		return status;
	}

	uint16_t Port() const
	{
		lock_guard<std::mutex> lock(mutex);
		return port;
	}

	bool HasPassword() const
	{
		lock_guard<std::mutex> lock(mutex);
		return passwordProtected;
	}

private:
	void SetStatus(string value)
	{
		lock_guard<std::mutex> lock(mutex);
		status = std::move(value);
	}

	void CloseHandles()
	{
#ifdef _WIN32
		HANDLE oldProcess = nullptr;
		HANDLE oldThread = nullptr;
		{
			lock_guard<std::mutex> lock(mutex);
			oldProcess = process;
			oldThread = thread;
			process = nullptr;
			thread = nullptr;
			port = 0;
			passwordProtected = false;
		}
		if(oldThread)
			CloseHandle(oldThread);
		if(oldProcess)
			CloseHandle(oldProcess);
#endif
	}

#ifdef _WIN32
	static bool ProcessIsRunning(HANDLE handle)
	{
		DWORD exitCode = 0;
		return GetExitCodeProcess(handle, &exitCode) && exitCode == STILL_ACTIVE;
	}

	static string CurrentExecutablePath()
	{
		vector<char> buffer(MAX_PATH);
		while(true)
		{
			const DWORD copied = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if(!copied)
				return {};
			if(copied < buffer.size() - 1)
				return string(buffer.data(), copied);
			buffer.resize(buffer.size() * 2);
		}
	}

	HANDLE process = nullptr;
	HANDLE thread = nullptr;
#endif
	mutable std::mutex mutex;
	uint16_t port = 0;
	bool passwordProtected = false;
	string status = "Detached host stopped";
};



class CoOpRelayController::DiscoveryService {
public:
	DiscoveryService()
	{
		worker = thread(&DiscoveryService::Run, this);
	}

	~DiscoveryService()
	{
		stop = true;
		if(worker.joinable())
			worker.join();
	}

	void Advertise(bool enabled, uint16_t port = CoOpRelay::DEFAULT_PORT, string name = {},
		bool passwordProtected = false, int playerCount = 0)
	{
		lock_guard<std::mutex> lock(mutex);
		advertising = enabled;
		relayPort = port ? port : CoOpRelay::DEFAULT_PORT;
		relayName = SanitizeName(std::move(name));
		relayPasswordProtected = passwordProtected;
		relayPlayerCount = max(0, playerCount);
	}

	void SetPlayerCount(int playerCount)
	{
		lock_guard<std::mutex> lock(mutex);
		relayPlayerCount = max(0, playerCount);
	}

	vector<DiscoveredRelay> Relays() const
	{
		lock_guard<std::mutex> lock(mutex);
		vector<DiscoveredRelay> result;
		const auto now = chrono::steady_clock::now();
		for(const Entry &entry : relays)
		{
			const int age = static_cast<int>(chrono::duration_cast<chrono::seconds>(now - entry.lastSeen).count());
			if(age > DISCOVERY_TTL_SECONDS)
				continue;
			DiscoveredRelay relay = entry.relay;
			relay.ageSeconds = age;
			result.push_back(std::move(relay));
		}
		sort(result.begin(), result.end(),
			[](const DiscoveredRelay &left, const DiscoveredRelay &right) {
				if(left.name != right.name)
					return left.name < right.name;
				if(left.host != right.host)
					return left.host < right.host;
				return left.port < right.port;
			});
		return result;
	}

private:
	struct Entry {
		DiscoveredRelay relay;
		chrono::steady_clock::time_point lastSeen;
	};

	struct Advert {
		uint16_t port = CoOpRelay::DEFAULT_PORT;
		string relayId;
		string name;
		int playerCount = -1;
		bool passwordProtected = false;
	};

	static string MakeRelayId()
	{
		ostringstream out;
		out << hex << chrono::steady_clock::now().time_since_epoch().count()
			<< reinterpret_cast<uintptr_t>(&out);
		return out.str();
	}

	static int HostPreference(const string &host)
	{
		in_addr address = {};
		if(inet_pton(AF_INET, host.c_str(), &address) != 1)
			return 50;

		const uint32_t ip = ntohl(address.s_addr);
		const uint8_t first = static_cast<uint8_t>((ip >> 24) & 0xff);
		const uint8_t second = static_cast<uint8_t>((ip >> 16) & 0xff);
		if(first == 127)
			return 100;
		if(first == 169 && second == 254)
			return 90;
		if(first == 100 && second >= 64 && second <= 127)
			return 10;
		if(first == 192 && second == 168)
			return 15;
		if(first == 10)
			return 20;
		if(first == 172 && second >= 16 && second <= 31)
			return 30;
		return 5;
	}

	static string SanitizeName(string name)
	{
		for(char &ch : name)
			if(ch == '\t' || ch == '\r' || ch == '\n')
				ch = ' ';
		if(name.empty())
			name = "Co-op Relay";
		if(name.size() > 48)
			name.resize(48);
		return name;
	}

	static optional<Advert> ParseAdvert(const string &message)
	{
		vector<string> parts;
		size_t start = 0;
		while(start <= message.size() && parts.size() < 7)
		{
			const size_t end = message.find('\t', start);
			parts.push_back(message.substr(start, end == string::npos ? string::npos : end - start));
			if(end == string::npos)
				break;
			start = end + 1;
		}

		if(parts.size() < 2 || parts[0] != DISCOVERY_PREFIX)
			return nullopt;
		if(parts[1] == "1")
		{
			if(parts.size() != 4)
				return nullopt;
		}
		else if(parts[1] == "2")
		{
			if(parts.size() != 5)
				return nullopt;
		}
		else if(parts[1] == to_string(DISCOVERY_PROTOCOL_VERSION))
		{
			if(parts.size() != 7)
				return nullopt;
		}
		else
			return nullopt;

		istringstream portStream(parts[2]);
		unsigned int port = 0;
		if(!(portStream >> port) || !portStream.eof() || !port || port > 65535)
			return nullopt;

		Advert advert;
		advert.port = static_cast<uint16_t>(port);
		if(parts[1] == "1")
			advert.name = SanitizeName(parts[3]);
		else if(parts[1] == "2")
		{
			advert.relayId = parts[3];
			advert.name = SanitizeName(parts[4]);
		}
		else
		{
			advert.relayId = parts[3];
			advert.name = SanitizeName(parts[4]);
			advert.passwordProtected = (parts[5] == "locked");
			istringstream countStream(parts[6]);
			unsigned int playerCount = 0;
			if(countStream >> playerCount && countStream.eof())
				advert.playerCount = static_cast<int>(min(playerCount, 999u));
		}
		return advert;
	}

	string AdvertLine() const
	{
		lock_guard<std::mutex> lock(mutex);
		if(!advertising)
			return {};
		return string(DISCOVERY_PREFIX) + '\t' + to_string(DISCOVERY_PROTOCOL_VERSION) + '\t'
			+ to_string(relayPort) + '\t' + relayId + '\t' + relayName + '\t'
			+ (relayPasswordProtected ? "locked" : "open") + '\t' + to_string(relayPlayerCount);
	}

	void NoteRelay(string host, const Advert &advert)
	{
		if(host.empty() || !advert.port)
			return;

		lock_guard<std::mutex> lock(mutex);
		const auto now = chrono::steady_clock::now();
		for(Entry &entry : relays)
			if((!advert.relayId.empty() && entry.relay.id == advert.relayId && entry.relay.port == advert.port)
					|| (advert.relayId.empty() && entry.relay.host == host && entry.relay.port == advert.port))
			{
				entry.relay.name = advert.name;
				entry.relay.id = advert.relayId;
				entry.relay.passwordProtected = advert.passwordProtected;
				entry.relay.playerCount = advert.playerCount;
				if(HostPreference(host) < HostPreference(entry.relay.host))
					entry.relay.host = std::move(host);
				entry.relay.ageSeconds = 0;
				entry.lastSeen = now;
				ExpireLocked(now);
				return;
			}

		Entry entry;
		entry.relay.name = advert.name;
		entry.relay.host = std::move(host);
		entry.relay.id = advert.relayId;
		entry.relay.port = advert.port;
		entry.relay.passwordProtected = advert.passwordProtected;
		entry.relay.playerCount = advert.playerCount;
		entry.relay.ageSeconds = 0;
		entry.lastSeen = now;
		relays.push_back(std::move(entry));
		ExpireLocked(now);
	}

	void ExpireLocked(chrono::steady_clock::time_point now)
	{
		relays.erase(remove_if(relays.begin(), relays.end(),
			[now](const Entry &entry) {
				return chrono::duration_cast<chrono::seconds>(now - entry.lastSeen).count() > DISCOVERY_TTL_SECONDS;
			}), relays.end());
	}

	void Broadcast(SocketHandle socket, const string &message) const
	{
		if(message.empty())
			return;

		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_port = htons(DISCOVERY_PORT);
		address.sin_addr.s_addr = INADDR_BROADCAST;

#ifdef _WIN32
		sendto(socket, message.data(), static_cast<int>(message.size()), 0,
			reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#else
		sendto(socket, message.data(), message.size(), 0,
			reinterpret_cast<const sockaddr *>(&address), sizeof(address));
#endif
	}

	void ReceiveAvailable(SocketHandle socket)
	{
		char buffer[512];
		sockaddr_in from = {};
#ifdef _WIN32
		int fromSize = sizeof(from);
		const int count = recvfrom(socket, buffer, sizeof(buffer), 0,
			reinterpret_cast<sockaddr *>(&from), &fromSize);
#else
		socklen_t fromSize = sizeof(from);
		const ssize_t count = recvfrom(socket, buffer, sizeof(buffer), 0,
			reinterpret_cast<sockaddr *>(&from), &fromSize);
#endif
		if(count <= 0)
			return;

		auto advert = ParseAdvert(string(buffer, static_cast<size_t>(count)));
		if(!advert)
			return;

		char host[INET_ADDRSTRLEN] = {};
		if(!inet_ntop(AF_INET, &from.sin_addr, host, sizeof(host)))
			return;
		NoteRelay(host, *advert);
	}

	void Run()
	{
		EnsureSockets();

		SocketHandle socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if(!IsValid(socket))
			return;

		const int yes = 1;
		setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
		setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&yes), sizeof(yes));
#ifdef SO_REUSEPORT
		setsockopt(socket, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char *>(&yes), sizeof(yes));
#endif

		sockaddr_in local = {};
		local.sin_family = AF_INET;
		local.sin_port = htons(DISCOVERY_PORT);
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		if(::bind(socket, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) != 0)
		{
			CloseSocket(socket);
			return;
		}

		auto nextAdvert = chrono::steady_clock::now();
		while(!stop.load())
		{
			const auto now = chrono::steady_clock::now();
			if(now >= nextAdvert)
			{
				Broadcast(socket, AdvertLine());
				nextAdvert = now + chrono::seconds(1);
			}

			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(socket, &readSet);
			timeval timeout = {};
			timeout.tv_usec = 200000;
#ifdef _WIN32
			const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
			const int ready = select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
			if(ready > 0 && FD_ISSET(socket, &readSet))
				ReceiveAvailable(socket);

			lock_guard<std::mutex> lock(mutex);
			ExpireLocked(chrono::steady_clock::now());
		}

		CloseSocket(socket);
	}

private:
	mutable std::mutex mutex;
	atomic_bool stop{false};
	thread worker;
	string relayId = MakeRelayId();
	bool advertising = false;
	uint16_t relayPort = CoOpRelay::DEFAULT_PORT;
	string relayName = "Co-op Relay";
	bool relayPasswordProtected = false;
	int relayPlayerCount = 0;
	vector<Entry> relays;
};



CoOpRelayController &CoOpRelayController::Get()
{
	static CoOpRelayController controller;
	return controller;
}



CoOpRelayController::CoOpRelayController()
	: host(make_unique<LocalRelayHost>()), detachedHost(make_unique<DetachedRelayHost>()), client(make_unique<NetworkClient>()),
	discovery(make_unique<DiscoveryService>())
{
}



CoOpRelayController::~CoOpRelayController() = default;



bool CoOpRelayController::StartHost(uint16_t port, string roomName, string password, bool allowDetached,
	bool serverWorldEnabled)
{
	if(allowDetached && detachedHost && detachedHost->Start(port, roomName, password, serverWorldEnabled))
	{
		host->Stop();
		discovery->Advertise(false);
		return true;
	}

	const bool started = host->Start(port, std::move(password), serverWorldEnabled);
	if(started)
		discovery->Advertise(true, port, std::move(roomName), host->HasPassword(),
			static_cast<int>(host->PlayerCount()));
	else
		discovery->Advertise(false);
	return started;
}



void CoOpRelayController::SetHostRoomName(string roomName)
{
	if(host->IsRunning())
		discovery->Advertise(true, host->Port(), std::move(roomName), host->HasPassword(),
			static_cast<int>(host->PlayerCount()));
}



void CoOpRelayController::SetHostRoomPassword(string roomName, string password)
{
	if(host->IsRunning())
	{
		host->SetPassword(std::move(password));
		discovery->Advertise(true, host->Port(), std::move(roomName), host->HasPassword(),
			static_cast<int>(host->PlayerCount()));
	}
}



void CoOpRelayController::StopHost()
{
	if(detachedHost)
		detachedHost->Stop();
	host->Stop();
	discovery->Advertise(false);
}



void CoOpRelayController::HostAndJoin(const PlayerInfo &player, uint16_t port, string password)
{
	if(StartHost(port, PlayerName(player) + "'s Room", password))
		Connect("127.0.0.1", port, PlayerName(player), std::move(password));
}



void CoOpRelayController::JoinLocal(const PlayerInfo &player, uint16_t port, string password)
{
	Connect("127.0.0.1", port, PlayerName(player), std::move(password));
}



void CoOpRelayController::Connect(string host, uint16_t port, string playerName, string password)
{
	client->Connect(std::move(host), port, std::move(playerName), std::move(password));
}



void CoOpRelayController::Disconnect()
{
	client->Disconnect();
	lastDrawStats = {};
}



bool CoOpRelayController::RequestResync()
{
	if(!client || !client->RequestResync())
		return false;

	Messages::Add({"Co-op resync requested.", GameData::MessageCategories().Get("normal")});
	return true;
}



void CoOpRelayController::SetSimulationActive(bool active)
{
	if(client)
		client->SetSimulationActive(active);
}



void CoOpRelayController::StepHost()
{
	if(host && host->IsRunning() && discovery)
		discovery->SetPlayerCount(static_cast<int>(host->PlayerCount()));
}



void CoOpRelayController::Step(PlayerInfo &player)
{
	StepHost();
	if(!client)
		return;
	client->Step(player);
	for(const CoOpRelay::SharedMissionEvent &event : client->TakeMissionEvents())
		Messages::Add({"Co-op mission: " + MissionEventDescription(event), GameData::MessageCategories().Get("normal")});
	for(const CoOpRelay::SharedResourceEvent &event : client->TakeResourceEvents())
	{
		const string localPlayerId = client->PlayerId();
		ApplyLocalResourceEvent(player, localPlayerId, appliedResourceActions, event);
		if(IsResourceEventVisibleTo(localPlayerId, event))
			Messages::Add({ResourceEventDescription(event), GameData::MessageCategories().Get("normal")});
	}
}



void CoOpRelayController::DrawFlightOverlay(const PlayerInfo &player, const Engine &engine) const
{
	lastDrawStats = {};
	if(!IsConnected() || !player.GetSystem())
		return;

	const string localSystem = player.GetSystem()->TrueName();
	vector<CoOpRelay::RemotePresence> remotes = Remotes().All();
	const vector<string> &warnings = DesyncWarnings();
	const CoOpRelay::SystemAuthority *authority = Authorities().Get(localSystem);
	vector<CoOpRelay::SharedNPCSnapshot> localNPCs = SharedNPCs().InSystem(localSystem);
	if(remotes.empty() && RecentEvents().empty() && warnings.empty() && !authority && localNPCs.empty())
		return;

	const Font &font = FontSet::Get(14);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &medium = *GameData::Colors().Get("medium");
	const Color &inactive = *GameData::Colors().Get("inactive");
	const Color marker(.1f, .55f, .75f, .85f);
	const Color back(0.f, .45f);

	const Point viewCenter = engine.ViewCenter();
	const double zoom = engine.ViewportZoom();

	const int rosterLines = min(static_cast<int>(remotes.size()), 5);
	const int eventLines = min(static_cast<int>(RecentEvents().size()), 2);
	const int warningLines = warnings.empty() ? 0 : 1;
	const int authorityLines = authority ? 1 : 0;
	const int npcLines = localNPCs.empty() ? 0 : 1;
	lastDrawStats.rosterRows = rosterLines;
	lastDrawStats.eventRows = eventLines;
	class Rectangle roster = Rectangle::FromCorner(
		Screen::TopRight() + Point(-285., 18.),
		Point(265., 28. + 18. * authorityLines + 18. * npcLines + 18. * warningLines
			+ 18. * rosterLines + 18. * eventLines));
	FillShader::Fill(roster, back);
	font.Draw("Co-op Relay", roster.TopLeft() + Point(8., 5.), bright);
	if(authority)
		font.Draw(AuthorityStatus(*authority, Remotes(), IsSystemAuthority(localSystem)),
			roster.TopLeft() + Point(8., 25.), medium);
	if(!localNPCs.empty())
		font.Draw("Shared NPCs: " + to_string(localNPCs.size()), roster.TopLeft() + Point(8., 25. + 18. * authorityLines),
			medium);
	if(!warnings.empty())
		font.Draw(FitText(font, "Warning: " + warnings.back(), 245.),
			roster.TopLeft() + Point(8., 25. + 18. * authorityLines + 18. * npcLines), bright);

	int line = 0;
	for(const CoOpRelay::RemotePresence &presence : remotes)
	{
		const CoOpRelay::PlayerSnapshot snapshot = presence.Interpolate(.5);
		if(presence.IsInSystem(localSystem))
		{
			++lastDrawStats.sameSystemContacts;
			Point screen = (snapshot.position - viewCenter) * zoom;
			if(screen.X() > Screen::Left() && screen.X() < Screen::Right()
					&& screen.Y() > Screen::Top() && screen.Y() < Screen::Bottom())
			{
				++lastDrawStats.visibleFlightContacts;
				const bool hasProxy = engine.HasCoOpPlayerProxy(snapshot.playerId);
				if(!hasProxy)
				{
					const Ship *model = snapshot.shipModel.empty() ? nullptr : GameData::Ships().Find(snapshot.shipModel);
					if(!model)
						model = player.Flagship();
					if(!model)
						model = GameData::Ships().Find("Sparrow");
					if(model && model->GetSprite())
						DrawRemoteShipSprite(*model, screen, zoom, snapshot.facing);
				}
				LineShader::Draw(screen + Point(-8., 0.), screen + Point(8., 0.), 1.3f, marker);
				LineShader::Draw(screen + Point(0., -8.), screen + Point(0., 8.), 1.3f, marker);
				LineShader::Draw(screen + Point(-10., -10.), screen + Point(10., -10.), 1.f, marker);
				font.Draw(PresenceName(snapshot), screen + Point(10., -18.), marker);
			}
		}

		if(line < rosterLines)
			font.Draw(PresenceStatus(snapshot, localSystem),
				roster.TopLeft() + Point(8.,
					25. + 18. * authorityLines + 18. * npcLines + 18. * warningLines + 18. * line),
				presence.IsInSystem(localSystem) ? medium : inactive);
		++line;
	}

	line = 0;
	for(auto it = RecentEvents().rbegin(); it != RecentEvents().rend() && line < eventLines; ++it, ++line)
	{
		string text = string(CoOpRelay::ToString(it->type)) + " - " + it->system;
		font.Draw(text, roster.TopLeft() + Point(8.,
			25. + 18. * authorityLines + 18. * npcLines + 18. * warningLines
				+ 18. * rosterLines + 18. * line),
			medium);
	}
}



bool CoOpRelayController::ShouldSuppressLocalNPCSpawns(const PlayerInfo &player) const
{
	if(!IsConnected() || !player.GetSystem())
		return false;

	const string system = player.GetSystem()->TrueName();
	const CoOpRelay::SystemAuthority *authority = Authorities().Get(system);
	return authority && authority->IsActive() && !IsSystemAuthority(system);
}



bool CoOpRelayController::IsConnected() const
{
	return client->IsConnected();
}



int CoOpRelayController::DirectPeerCount() const
{
	return client ? client->DirectPeerCount() : 0;
}



bool CoOpRelayController::IsHostRunning() const
{
	return (host && host->IsRunning()) || (detachedHost && detachedHost->IsRunning());
}



size_t CoOpRelayController::HostPlayerCount() const
{
	return host ? host->PlayerCount() : 0;
}



string CoOpRelayController::PlayerId() const
{
	return client ? client->PlayerId() : string();
}



bool CoOpRelayController::PublishPeerEndpoint(string host, uint16_t port)
{
	if(!client || host.empty() || !port)
		return false;
	CoOpRelay::PeerEndpoint endpoint;
	endpoint.sequence = nextPeerEndpointSequence++;
	endpoint.playerId = client->PlayerId();
	endpoint.host = std::move(host);
	endpoint.port = port;
	if(!endpoint.IsValid())
		return false;
	return client->PublishPeerEndpoint(endpoint);
}



bool CoOpRelayController::PublishSharedNPC(const CoOpRelay::SharedNPCSnapshot &snapshot)
{
	return client && client->PublishSharedNPC(snapshot);
}



bool CoOpRelayController::PublishSharedNPCDamage(const CoOpRelay::SharedNPCDamage &damage)
{
	return client && client->PublishSharedNPCDamage(damage);
}



bool CoOpRelayController::PublishSharedCombatHit(const CoOpRelay::SharedCombatHit &hit)
{
	return client && client->PublishSharedCombatHit(hit);
}



bool CoOpRelayController::PublishSharedWeaponFire(const CoOpRelay::SharedWeaponFire &fire)
{
	return client && client->PublishSharedWeaponFire(fire);
}



vector<CoOpRelay::SharedNPCDamage> CoOpRelayController::TakeNPCDamageReports()
{
	return client ? client->TakeNPCDamageReports() : vector<CoOpRelay::SharedNPCDamage>();
}



vector<CoOpRelay::SharedCombatHit> CoOpRelayController::TakeCombatHits()
{
	return client ? client->TakeCombatHits() : vector<CoOpRelay::SharedCombatHit>();
}



vector<CoOpRelay::SharedWeaponFire> CoOpRelayController::TakeWeaponFires()
{
	return client ? client->TakeWeaponFires() : vector<CoOpRelay::SharedWeaponFire>();
}



bool CoOpRelayController::PublishSharedNPCBoarding(const CoOpRelay::SharedNPCBoarding &boarding)
{
	return client && client->PublishSharedNPCBoarding(boarding);
}



bool CoOpRelayController::PublishSharedNPCBoardingRequest(string npcId, string detail)
{
	if(!client || !client->IsConnected() || npcId.empty())
		return false;

	const CoOpRelay::SharedNPCSnapshot *snapshot = client->SharedNPCs().Get(npcId);
	if(!snapshot)
		return false;

	CoOpRelay::SharedNPCBoarding boarding;
	boarding.sequence = nextBoardingRequestSequence++;
	boarding.npcId = std::move(npcId);
	boarding.playerId = client->PlayerId();
	boarding.ownerId = snapshot->ownerId;
	boarding.system = snapshot->system;
	boarding.action = CoOpRelay::BoardingAction::REQUEST;
	boarding.detail = std::move(detail);
	return boarding.IsValid() && client->PublishSharedNPCBoarding(boarding);
}



vector<CoOpRelay::SharedNPCBoarding> CoOpRelayController::TakeNPCBoardingReports()
{
	return client ? client->TakeNPCBoardingReports() : vector<CoOpRelay::SharedNPCBoarding>();
}



bool CoOpRelayController::PublishSharedMissionEvent(const CoOpRelay::SharedMissionEvent &event)
{
	return client && client->PublishSharedMissionEvent(event);
}



vector<CoOpRelay::SharedMissionEvent> CoOpRelayController::TakeMissionEvents()
{
	return client ? client->TakeMissionEvents() : vector<CoOpRelay::SharedMissionEvent>();
}



bool CoOpRelayController::PublishSharedResourceEvent(const CoOpRelay::SharedResourceEvent &event)
{
	return client && client->PublishSharedResourceEvent(event);
}



vector<CoOpRelay::SharedResourceEvent> CoOpRelayController::TakeResourceEvents()
{
	return client ? client->TakeResourceEvents() : vector<CoOpRelay::SharedResourceEvent>();
}



bool CoOpRelayController::PublishResourceAssist(CoOpRelay::ResourceActionType type, double amount, string targetPlayerId)
{
	if(!client || !client->IsConnected() || amount <= 0.)
		return false;

	const string playerId = client->PlayerId();
	if(playerId.empty())
		return false;

	CoOpRelay::SharedResourceEvent event;
	event.sequence = nextResourceAssistSequence++;
	event.actionId = playerId + ":assist:" + to_string(event.sequence);
	event.playerId = playerId;
	event.targetPlayerId = std::move(targetPlayerId);
	event.type = type;
	event.status = CoOpRelay::ResourceActionStatus::REQUEST;
	event.resource = ResourceName(type);
	event.amount = amount;
	event.detail = ResourceAssistSentText(type) + " Awaiting acceptance.";
	if(!event.IsValid() || !PublishSharedResourceEvent(event))
		return false;

	Messages::Add({"Co-op resource request sent.", GameData::MessageCategories().Get("normal")});
	return true;
}



vector<CoOpRelay::SharedResourceEvent> CoOpRelayController::PendingResourceRequests() const
{
	vector<CoOpRelay::SharedResourceEvent> result;
	if(!client || !client->IsConnected())
		return result;

	const string localPlayerId = client->PlayerId();
	if(localPlayerId.empty())
		return result;

	const vector<CoOpRelay::SharedResourceEvent> events = client->ResourceEvents().All();
	for(const CoOpRelay::SharedResourceEvent &event : events)
	{
		if(event.status != CoOpRelay::ResourceActionStatus::REQUEST || event.playerId == localPlayerId)
			continue;
		if(!event.targetPlayerId.empty() && event.targetPlayerId != localPlayerId)
			continue;
		if(find(appliedResourceActions.begin(), appliedResourceActions.end(), event.actionId)
				!= appliedResourceActions.end())
			continue;
		const bool alreadyAnswered = any_of(events.begin(), events.end(),
			[&event](const CoOpRelay::SharedResourceEvent &response) {
				return response.actionId == event.actionId
					&& (response.status == CoOpRelay::ResourceActionStatus::CONFIRMED
						|| response.status == CoOpRelay::ResourceActionStatus::REJECTED);
			});
		if(alreadyAnswered)
			continue;
		result.push_back(event);
	}
	return result;
}



bool CoOpRelayController::RespondToResourceRequest(PlayerInfo &player, const string &actionId, bool accept)
{
	if(!client || !client->IsConnected() || actionId.empty())
		return false;

	const string localPlayerId = client->PlayerId();
	if(localPlayerId.empty())
		return false;

	vector<CoOpRelay::SharedResourceEvent> requests = PendingResourceRequests();
	auto it = find_if(requests.begin(), requests.end(), [&actionId](const CoOpRelay::SharedResourceEvent &event) {
		return event.actionId == actionId;
	});
	if(it == requests.end())
		return false;

	const CoOpRelay::SharedResourceEvent &request = *it;
	if(accept)
	{
		CoOpRelay::SharedResourceEvent applied = request;
		applied.targetPlayerId = localPlayerId;
		applied.status = CoOpRelay::ResourceActionStatus::APPLIED;
		applied.detail = ResourceAssistReceivedText(request.type);
		if(!ApplyLocalResourceEvent(player, localPlayerId, appliedResourceActions, applied))
		{
			Messages::Add({"Co-op resource request could not be applied.", GameData::MessageCategories().Get("normal")});
			return false;
		}
	}
	else
		appliedResourceActions.push_back(request.actionId);

	CoOpRelay::SharedResourceEvent response = request;
	response.sequence = nextResourceAssistSequence++;
	response.playerId = localPlayerId;
	response.targetPlayerId = request.playerId;
	response.status = accept ? CoOpRelay::ResourceActionStatus::CONFIRMED : CoOpRelay::ResourceActionStatus::REJECTED;
	response.amount = accept ? request.amount : 0.;
	response.detail = accept ? "Co-op resource request accepted." : "Co-op resource request rejected.";
	if(!response.IsValid() || !PublishSharedResourceEvent(response))
		return false;

	Messages::Add({accept ? ResourceAssistReceivedText(request.type) : "Co-op resource request rejected.",
		GameData::MessageCategories().Get("normal")});
	return true;
}



string CoOpRelayController::StatusText() const
{
	return client->StatusText();
}



string CoOpRelayController::HostStatusText() const
{
	if(detachedHost && detachedHost->IsRunning())
		return detachedHost->StatusText();

	string text = host->StatusText();
	if(host->IsRunning())
	{
		const size_t count = host->PlayerCount();
		text += " - " + to_string(count) + (count == 1 ? " player" : " players");
	}
	return text;
}



const CoOpRelay::PresenceStore &CoOpRelayController::Remotes() const
{
	static const CoOpRelay::PresenceStore empty;
	return client ? client->Remotes() : empty;
}



const CoOpRelay::AuthorityStore &CoOpRelayController::Authorities() const
{
	static const CoOpRelay::AuthorityStore empty;
	return client ? client->Authorities() : empty;
}



const CoOpRelay::SharedNPCStore &CoOpRelayController::SharedNPCs() const
{
	static const CoOpRelay::SharedNPCStore empty;
	return client ? client->SharedNPCs() : empty;
}



const CoOpRelay::SharedMissionEventLog &CoOpRelayController::MissionEvents() const
{
	static const CoOpRelay::SharedMissionEventLog empty;
	return client ? client->MissionEvents() : empty;
}



const CoOpRelay::SharedResourceEventLog &CoOpRelayController::ResourceEvents() const
{
	static const CoOpRelay::SharedResourceEventLog empty;
	return client ? client->ResourceEvents() : empty;
}



bool CoOpRelayController::IsSystemAuthority(const string &system) const
{
	return client && client->IsSystemAuthority(system);
}



const vector<CoOpRelay::PlayerEvent> &CoOpRelayController::RecentEvents() const
{
	static const vector<CoOpRelay::PlayerEvent> empty;
	return client ? client->RecentEvents() : empty;
}



const vector<string> &CoOpRelayController::DesyncWarnings() const
{
	static const vector<string> empty;
	return client ? client->DesyncWarnings() : empty;
}



CoOpRelay::Diagnostics CoOpRelayController::DiagnosticsFor(const PlayerInfo &player) const
{
	CoOpRelay::Diagnostics diagnostics = client ? client->GetDiagnostics() : CoOpRelay::Diagnostics();
	if(host && host->IsRunning())
	{
		const CoOpRelay::Diagnostics hostDiagnostics = host->GetDiagnostics();
		diagnostics.serverWorldEnabled = hostDiagnostics.serverWorldEnabled;
		diagnostics.serverTickRate = hostDiagnostics.serverTickRate;
		diagnostics.staleProxyCount += hostDiagnostics.staleProxyCount;
		diagnostics.duplicatePreventionCount += hostDiagnostics.duplicatePreventionCount;
		if(!diagnostics.connectedPlayers)
			diagnostics.connectedPlayers = hostDiagnostics.connectedPlayers;
	}

	const System *system = player.GetSystem();
	if(system)
		if(const CoOpRelay::SystemAuthority *authority = Authorities().Get(system->TrueName()))
			diagnostics.authorityOwner = authority->ownerId;
	return diagnostics;
}



CoOpRelayController::DrawStats CoOpRelayController::LastDrawStats() const
{
	return lastDrawStats;
}



vector<CoOpRelayController::DiscoveredRelay> CoOpRelayController::DiscoveredRelays() const
{
	return discovery ? discovery->Relays() : vector<DiscoveredRelay>();
}
