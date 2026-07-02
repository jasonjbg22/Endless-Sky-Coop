#!/usr/bin/env python3
"""Small line-oriented relay server for Endless Sky Co-op Relay presence.

The relay is intentionally not a gameplay server. It assigns peer ids, caches
the latest player snapshot and peer endpoint lines for each peer, and broadcasts
snapshot/event/endpoint lines to other connected peers.
"""

from __future__ import annotations

import argparse
import asyncio
from dataclasses import dataclass, field
import os


PROTOCOL_VERSION = "3"


@dataclass
class Client:
    player_id: str
    name: str
    writer: asyncio.StreamWriter


@dataclass
class RelayState:
    next_player_number: int = 1
    clients: dict[str, Client] = field(default_factory=dict)
    latest_snapshots: dict[str, str] = field(default_factory=dict)
    latest_peer_endpoints: dict[str, str] = field(default_factory=dict)
    seen_peer_endpoint_players: set[str] = field(default_factory=set)
    transcript: list[str] = field(default_factory=list)
    debug: bool = False

    def log(self, message: str) -> None:
        self.transcript.append(message)
        if len(self.transcript) > 200:
            del self.transcript[: len(self.transcript) - 200]
        if self.debug:
            print(f"relay: {message}", flush=True)

    def join(self, name: str, writer: asyncio.StreamWriter) -> Client:
        player_id = f"player-{self.next_player_number}"
        self.next_player_number += 1
        client = Client(player_id=player_id, name=name, writer=writer)
        self.clients[player_id] = client
        self.log(f"joined {player_id} {name}")
        return client

    def leave(self, player_id: str) -> None:
        if player_id in self.clients:
            self.log(f"left {player_id}")
        self.clients.pop(player_id, None)
        self.latest_snapshots.pop(player_id, None)
        self.latest_peer_endpoints.pop(player_id, None)


def split_fields(line: str) -> list[str]:
    return line.rstrip("\r\n").split("\t")


def player_id_from_relay_line(line: str) -> str | None:
    fields = split_fields(line)
    if not fields:
        return None
    if fields[0] == "snapshot" and len(fields) == 20:
        return fields[3]
    if fields[0] == "event" and len(fields) == 8:
        return fields[3]
    if fields[0] == "peer-endpoint" and len(fields) == 7:
        return fields[3]
    return None


def describe_relay_line(line: str) -> str:
    fields = split_fields(line)
    if not fields:
        return "empty"
    if fields[0] == "snapshot" and len(fields) == 20:
        landed = fields[11] or "flying"
        return f"snapshot system={fields[5]} landed={landed} fuel={fields[15]}"
    if fields[0] == "event" and len(fields) == 8:
        return f"event type={fields[4]} system={fields[5]} planet={fields[6]}"
    if fields[0] == "peer-endpoint" and len(fields) == 7:
        return f"peer-endpoint host={fields[4]} port={fields[5]}"
    return fields[0]


def control_sequence(line: str, message_type: str) -> str | None:
    fields = split_fields(line)
    if len(fields) == 3 and fields[0] == message_type and fields[1] == PROTOCOL_VERSION and fields[2].isdigit():
        return fields[2]
    return None


async def send_line(writer: asyncio.StreamWriter, line: str) -> None:
    writer.write((line.rstrip("\r\n") + "\n").encode("utf-8"))
    await writer.drain()


async def broadcast(state: RelayState, sender_id: str, line: str) -> None:
    disconnected: list[str] = []
    for player_id, client in state.clients.items():
        if player_id == sender_id:
            continue
        try:
            await send_line(client.writer, line)
        except ConnectionError:
            disconnected.append(player_id)
    for player_id in disconnected:
        state.leave(player_id)


async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter, state: RelayState) -> None:
    client: Client | None = None
    try:
        try:
            first = await reader.readline()
        except (ConnectionError, OSError):
            return
        if not first:
            return
        fields = split_fields(first.decode("utf-8", errors="replace"))
        if (len(fields) not in (2, 3)) or fields[0] != "join":
            state.log(f"rejected malformed join: {fields}")
            await send_line(writer, "error\texpected join")
            return

        client = state.join(fields[1], writer)
        await send_line(writer, f"welcome\t{PROTOCOL_VERSION}\t{client.player_id}")
        for player_id, snapshot in state.latest_snapshots.items():
            if player_id != client.player_id:
                await send_line(writer, snapshot)
        for player_id, endpoint in state.latest_peer_endpoints.items():
            if player_id != client.player_id:
                await send_line(writer, endpoint)

        while True:
            try:
                raw = await reader.readline()
            except (ConnectionError, OSError):
                break
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if sequence := control_sequence(line, "ping"):
                await send_line(writer, f"pong\t{PROTOCOL_VERSION}\t{sequence}")
                continue
            if control_sequence(line, "resync"):
                continue

            sender_id = player_id_from_relay_line(line)
            if sender_id != client.player_id:
                state.log(f"rejected line from {client.player_id}: {line}")
                await send_line(writer, "error\tplayer id mismatch")
                continue
            if line.startswith("snapshot\t"):
                state.latest_snapshots[client.player_id] = line
            if line.startswith("peer-endpoint\t"):
                state.latest_peer_endpoints[client.player_id] = line
                state.seen_peer_endpoint_players.add(client.player_id)
            state.log(f"{client.player_id} -> {describe_relay_line(line)}")
            await broadcast(state, client.player_id, line)
    finally:
        if client:
            state.leave(client.player_id)
        writer.close()
        if os.name != "nt":
            try:
                await writer.wait_closed()
            except Exception:
                pass


async def serve(host: str, port: int) -> None:
    state = RelayState()
    server = await asyncio.start_server(
        lambda reader, writer: handle_client(reader, writer, state),
        host,
        port,
    )
    addresses = ", ".join(str(socket.getsockname()) for socket in server.sockets or [])
    print(f"Co-op relay listening on {addresses}", flush=True)
    async with server:
        await server.serve_forever()


def main() -> None:
    parser = argparse.ArgumentParser(description="Run an Endless Sky Co-op Relay presence server.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=24872)
    args = parser.parse_args()
    asyncio.run(serve(args.host, args.port))


if __name__ == "__main__":
    main()
