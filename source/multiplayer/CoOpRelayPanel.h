/* CoOpRelayPanel.h
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

#include "../Panel.h"

#include <cstddef>
#include <cstdint>
#include <string>

class Color;
class Font;
class PlayerInfo;
class Rectangle;
class UI;



// Basic UI for the separate Co-op Relay multiplayer mode.
class CoOpRelayPanel final : public Panel {
public:
	CoOpRelayPanel(PlayerInfo &player, UI &gamePanels);

	void Step() final;
	void Draw() final;

protected:
	bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) final;

private:
	void DrawButton(const Rectangle &bounds, const std::string &label, bool enabled, SDL_Keycode key);
	void DrawCentered(const Font &font, const std::string &text, double y, const Color &color) const;
	void NameRoom();
	void SetRoomName(const std::string &name);
	void PasswordRoom();
	void SetRoomPassword(const std::string &password);
	void PortRoom();
	void SetRoomPort(const std::string &port);
	void HostRoom();
	void PlayHost();
	void JoinLocal();
	void JoinAddress(const std::string &address);
	void SelectDiscovered(std::size_t index);
	void JoinSelected();
	void JoinSelectedWithPassword(const std::string &password);
	void SelectResourceTarget(std::string playerId);
	void RequestResync();
	void SendRefuelAssist();
	void SendRepairAssist();
	void SendFuelAssist();
	void SendEnergyAssist();
	void AcceptResourceRequest();
	void RejectResourceRequest();
	void EnterGame();

private:
	PlayerInfo &player;
	UI &gamePanels;
	std::string roomName;
	std::string roomPassword;
	uint16_t roomPort = 5050;
	std::string pendingJoinHost;
	uint16_t pendingJoinPort = 0;
	int selectedDiscovery = -1;
	std::string resourceTargetPlayerId;
};
