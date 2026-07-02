Endless Sky Co-op Friend Test Build
===================================

How to play:
1. Double-click "Play Co-op.bat".
2. In Endless Sky, open Co-op Relay.
3. Host creates a named room with a password and clicks Play.
4. Friends join the room by LAN discovery, Tailscale IP, or direct public IP.

How to update:
1. Close the game.
2. Double-click "Update Game.bat".
3. Launch the game again.

If update-config.txt says CHANGE_ME, ask the host for the GitHub owner and repo.

Port forwarding:
- Default port: 5050.
- If playing across different homes without Tailscale, the host should forward TCP/UDP 5050 to their gaming PC.

Current test focus:
- Joining the same passworded room.
- Seeing each other in the same system.
- PvP damage and weapon visuals.
- Shared NPC combat.
- Jumping, landing, saving, and loading without breaking normal single-player.

Known rule:
- This is a friend-trust co-op build, not ranked competitive PvP.
