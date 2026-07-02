#!/usr/bin/env python3
"""Smoke-test the built Endless Sky executable as a standalone Co-op Relay."""

from __future__ import annotations

import argparse
import asyncio
import os
from pathlib import Path
import socket
import sys


class SmokeFailure(RuntimeError):
    pass


def default_executable(repo: Path) -> Path:
    if os.name == "nt":
        return repo / "build" / "vanilla-msvc" / "Debug" / "Endless Sky.exe"
    return repo / "build" / "endless-sky"


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def snapshot(
    player_id: str,
    sequence: int,
    name: str,
    system: str = "Sol",
    landed_planet: str = "",
    ship_model: str = "Shuttle",
) -> str:
    return (
        f"snapshot\t3\t{sequence}\t{player_id}\t{name}\t{system}\t"
        f"10\t-20\t0.5\t1.5\t45\t{landed_planet}\t{ship_model}\t123\t0.9\t0.8\t0.7\t0.6\t0.5\t1"
    )


def require_snapshot_line(
    line: str,
    player_id: str,
    sequence: int,
    name: str,
    system: str,
    landed_planet: str = "",
    ship_model: str = "Shuttle",
) -> None:
    fields = line.split("\t")
    if len(fields) != 20 or fields[0] != "snapshot":
        raise SmokeFailure(f"expected snapshot line, got: {line}")
    expected = {
        1: "3",
        2: str(sequence),
        3: player_id,
        4: name,
        5: system,
        11: landed_planet,
        12: ship_model,
        19: "1",
    }
    for index, value in expected.items():
        if fields[index] != value:
            raise SmokeFailure(f"unexpected snapshot field {index}: {line}")


def require_authority_line(line: str, system: str, owner_id: str) -> None:
    fields = line.split("\t")
    if len(fields) != 6 or fields[0] != "authority":
        raise SmokeFailure(f"expected authority line, got: {line}")
    expected = {
        1: "3",
        3: system,
        4: owner_id,
    }
    for index, value in expected.items():
        if fields[index] != value:
            raise SmokeFailure(f"unexpected authority field {index}: {line}")


def combat_hit(attacker_id: str, target_id: str, sequence: int, system: str = "Sol") -> str:
    return (
        f"combat-hit\t3\t{sequence}\t{attacker_id}\t{target_id}\t{system}\t"
        "Shuttle\tPlayer\t0.1\t0.2\t0\t0\t0.05\t0\t0\tPvP smoke hit"
    )


def npc_damage(npc_id: str, reporter_id: str, sequence: int, system: str = "Sol") -> str:
    return (
        f"npc-damage\t3\t{sequence}\t{npc_id}\tcoop-server\t{system}\t{reporter_id}\t"
        "1\t1\t0\t0\t0\t1\t1"
    )


def require_combat_hit_line(line: str, attacker_id: str, target_id: str, sequence: int) -> None:
    fields = line.split("\t")
    if len(fields) not in (16, 22) or fields[0] != "combat-hit":
        raise SmokeFailure(f"expected combat-hit line, got: {line}")
    expected = {
        1: "3",
        2: str(sequence),
        3: attacker_id,
        4: target_id,
        5: "Sol",
        15: "PvP smoke hit",
    }
    for index, value in expected.items():
        if fields[index] != value:
            raise SmokeFailure(f"unexpected combat-hit field {index}: {line}")
    numeric_expected = {
        8: 0.1,
        9: 0.2,
        12: 0.05,
    }
    for index, value in numeric_expected.items():
        try:
            parsed = float(fields[index])
        except ValueError as error:
            raise SmokeFailure(f"unexpected combat-hit numeric field {index}: {line}") from error
        if abs(parsed - value) > 1e-9:
            raise SmokeFailure(f"unexpected combat-hit numeric field {index}: {line}")
    if len(fields) == 22:
        for index in (17, 18, 19, 20, 21):
            try:
                float(fields[index])
            except ValueError as error:
                raise SmokeFailure(f"unexpected combat-hit visual field {index}: {line}") from error


def parse_server_npc_line(line: str) -> dict[str, object] | None:
    if not line.startswith("npc\t"):
        return None
    fields = line.split("\t")
    if len(fields) != 23:
        raise SmokeFailure(f"expected npc line, got: {line}")
    try:
        sequence = int(fields[2])
        position = (float(fields[6]), float(fields[7]))
        removed = bool(int(fields[22]))
    except ValueError as error:
        raise SmokeFailure(f"unexpected npc numeric field: {line}") from error
    return {
        "sequence": sequence,
        "npc_id": fields[3],
        "owner_id": fields[4],
        "system": fields[5],
        "position": position,
        "removed": removed,
    }


def resource_event_matches(line: str, npc_id: str, target_player_id: str) -> bool:
    if not line.startswith("resource-event\t"):
        return False
    fields = line.split("\t")
    if len(fields) != 11:
        raise SmokeFailure(f"expected resource-event line, got: {line}")
    try:
        amount = float(fields[9])
    except ValueError as error:
        raise SmokeFailure(f"unexpected resource-event amount: {line}") from error
    return (
        fields[3].startswith(f"server-destroy-reward:{npc_id}:")
        and fields[4] == "coop-server"
        and fields[5] == target_player_id
        and fields[6] == "credit-reward"
        and fields[7] == "applied"
        and fields[8] == "credits"
        and amount > 0.
    )


async def read_line(reader: asyncio.StreamReader, timeout: float = 5.0) -> str:
    line = await asyncio.wait_for(reader.readline(), timeout)
    if not line:
        raise SmokeFailure("connection closed while waiting for relay line")
    return line.decode("utf-8", errors="replace").rstrip("\r\n")


async def read_until(reader: asyncio.StreamReader, prefix: str, timeout: float = 5.0) -> str:
    deadline = asyncio.get_running_loop().time() + timeout
    while True:
        remaining = deadline - asyncio.get_running_loop().time()
        if remaining <= 0:
            raise SmokeFailure(f"timed out waiting for {prefix} line")
        line = await read_line(reader, timeout=remaining)
        if line.startswith(prefix):
            return line


async def require_landed_server_world_npc_motion(
    reader: asyncio.StreamReader,
    label: str,
    timeout: float = 10.0,
) -> None:
    deadline = asyncio.get_running_loop().time() + timeout
    last_positions: dict[str, tuple[float, float]] = {}
    seen_sequences: set[tuple[str, int]] = set()

    while True:
        remaining = deadline - asyncio.get_running_loop().time()
        if remaining <= 0:
            raise SmokeFailure(f"{label} did not observe moving server-world NPCs while both players were landed")

        line = await read_line(reader, timeout=remaining)
        npc = parse_server_npc_line(line)
        if not npc:
            continue
        if npc["owner_id"] != "coop-server" or npc["system"] != "Sol" or npc["removed"]:
            continue

        npc_id = str(npc["npc_id"])
        sequence = int(npc["sequence"])
        position = npc["position"]
        if not isinstance(position, tuple):
            raise SmokeFailure(f"unexpected npc position payload: {line}")

        sequence_key = (npc_id, sequence)
        if sequence_key in seen_sequences:
            raise SmokeFailure(f"{label} saw duplicate server-world NPC sequence {npc_id}/{sequence}")
        seen_sequences.add(sequence_key)

        previous = last_positions.get(npc_id)
        last_positions[npc_id] = position
        if previous:
            dx = position[0] - previous[0]
            dy = position[1] - previous[1]
            if dx * dx + dy * dy > 1e-6:
                return


async def read_server_world_npc_id(reader: asyncio.StreamReader, label: str, timeout: float = 10.0) -> str:
    deadline = asyncio.get_running_loop().time() + timeout
    while True:
        remaining = deadline - asyncio.get_running_loop().time()
        if remaining <= 0:
            raise SmokeFailure(f"{label} did not observe a server-world NPC")
        line = await read_line(reader, timeout=remaining)
        npc = parse_server_npc_line(line)
        if not npc:
            continue
        if npc["owner_id"] == "coop-server" and npc["system"] == "Sol" and not npc["removed"]:
            return str(npc["npc_id"])


async def require_server_world_npc_destroyed_reward(
    reader: asyncio.StreamReader,
    label: str,
    npc_id: str,
    target_player_id: str,
    timeout: float = 10.0,
) -> None:
    deadline = asyncio.get_running_loop().time() + timeout
    saw_removed = False
    saw_reward = False
    while True:
        if saw_removed and saw_reward:
            return
        remaining = deadline - asyncio.get_running_loop().time()
        if remaining <= 0:
            missing = []
            if not saw_removed:
                missing.append("authoritative NPC removal")
            if not saw_reward:
                missing.append("credit reward")
            raise SmokeFailure(f"{label} did not observe {', '.join(missing)} for destroyed {npc_id}")

        line = await read_line(reader, timeout=remaining)
        npc = parse_server_npc_line(line)
        if npc and npc["npc_id"] == npc_id and npc["owner_id"] == "coop-server" and npc["removed"]:
            saw_removed = True
            continue
        if resource_event_matches(line, npc_id, target_player_id):
            saw_reward = True


async def exercise_server_world_npc_combat(
    reader_a: asyncio.StreamReader,
    writer_a: asyncio.StreamWriter,
    reader_b: asyncio.StreamReader,
) -> None:
    npc_id = await read_server_world_npc_id(reader_a, "client A")
    writer_a.write((npc_damage(npc_id, "player-1", 1) + "\n").encode("utf-8"))
    await writer_a.drain()
    await asyncio.gather(
        require_server_world_npc_destroyed_reward(reader_a, "client A", npc_id, "player-1"),
        require_server_world_npc_destroyed_reward(reader_b, "client B", npc_id, "player-2"),
    )


async def exercise_landed_server_world(
    reader_a: asyncio.StreamReader,
    writer_a: asyncio.StreamWriter,
    reader_b: asyncio.StreamReader,
    writer_b: asyncio.StreamWriter,
) -> None:
    writer_a.write((snapshot("player-1", 2, "Host Player", landed_planet="Earth") + "\n").encode("utf-8"))
    writer_b.write((snapshot("player-2", 2, "Player B", landed_planet="Earth") + "\n").encode("utf-8"))
    await writer_a.drain()
    await writer_b.drain()

    landed_a = await read_until(reader_b, "snapshot\t")
    require_snapshot_line(landed_a, "player-1", 2, "Host Player", "Sol", "Earth")
    landed_b = await read_until(reader_a, "snapshot\t")
    require_snapshot_line(landed_b, "player-2", 2, "Player B", "Sol", "Earth")

    await require_landed_server_world_npc_motion(reader_a, "client A")
    await require_landed_server_world_npc_motion(reader_b, "client B")


async def connect_player(
    port: int,
    name: str,
    password: str,
) -> tuple[asyncio.StreamReader, asyncio.StreamWriter, str]:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(f"join\t{name}\t{password}\n".encode("utf-8"))
    await writer.drain()
    welcome = await read_line(reader)
    return reader, writer, welcome


async def wait_for_server(port: int, timeout: float) -> None:
    deadline = asyncio.get_running_loop().time() + timeout
    while True:
        try:
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
            writer.close()
            await writer.wait_closed()
            reader.feed_eof()
            return
        except (ConnectionError, OSError):
            if asyncio.get_running_loop().time() >= deadline:
                raise SmokeFailure("standalone relay did not open its TCP port")
            await asyncio.sleep(0.1)


async def run_smoke(exe: Path, repo: Path, timeout: float, server_world: bool) -> None:
    if not exe.exists():
        raise SmokeFailure(f"Endless Sky executable not found: {exe}")

    port = free_port()
    password = "smoke-pass"
    command = [
        str(exe),
        "--coop-relay-server",
        str(port),
        "--coop-relay-room",
        "Headless Smoke",
        "--coop-relay-password",
        password,
    ]
    if server_world:
        command.append("--coop-server-world")

    process = await asyncio.create_subprocess_exec(
        *command,
        cwd=repo,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
    )

    try:
        await wait_for_server(port, timeout=max(20.0, timeout))

        wrong_reader, wrong_writer = await asyncio.open_connection("127.0.0.1", port)
        wrong_writer.write(b"join\tWrong\tbad-password\n")
        await wrong_writer.drain()
        reject = await read_line(wrong_reader)
        if reject != "reject\t3\tRoom password rejected":
            raise SmokeFailure(f"unexpected password reject line: {reject}")
        wrong_writer.close()
        await wrong_writer.wait_closed()

        reader_a, writer_a, welcome_a = await connect_player(port, "Host Player", password)
        if welcome_a != "welcome\t3\tplayer-1":
            raise SmokeFailure(f"unexpected first welcome: {welcome_a}")

        writer_a.write((snapshot("player-1", 1, "Host Player") + "\n").encode("utf-8"))
        await writer_a.drain()
        await asyncio.sleep(0.2)

        reader_b, writer_b, welcome_b = await connect_player(port, "Player B", password)
        if welcome_b != "welcome\t3\tplayer-2":
            raise SmokeFailure(f"unexpected second welcome: {welcome_b}")
        cached_a = await read_line(reader_b)
        require_snapshot_line(cached_a, "player-1", 1, "Host Player", "Sol")
        authority = await read_until(reader_b, "authority\t")
        require_authority_line(authority, "Sol", "coop-server" if server_world else "player-1")

        writer_b.write((snapshot("player-2", 1, "Player B") + "\n").encode("utf-8"))
        await writer_b.drain()
        await read_until(reader_a, "snapshot\t")

        if server_world:
            await exercise_server_world_npc_combat(reader_a, writer_a, reader_b)

        writer_a.write((combat_hit("player-1", "player-2", 1) + "\n").encode("utf-8"))
        await writer_a.drain()
        pvp_hit = await read_until(reader_b, "combat-hit\t")
        require_combat_hit_line(pvp_hit, "player-1", "player-2", 1)

        if server_world:
            await exercise_landed_server_world(reader_a, writer_a, reader_b, writer_b)

        writer_a.close()
        await writer_a.wait_closed()

        writer_b.write((
            snapshot("player-2", 3, "Player B", "Alpha Centauri", ship_model="Sparrow") + "\n").encode("utf-8"))
        await writer_b.drain()
        await asyncio.sleep(0.2)

        reader_c, writer_c, welcome_c = await connect_player(port, "Player C", password)
        if welcome_c != "welcome\t3\tplayer-3":
            raise SmokeFailure(f"unexpected third welcome after host-like client left: {welcome_c}")
        cached_b = await read_line(reader_c)
        require_snapshot_line(cached_b, "player-2", 3, "Player B", "Alpha Centauri", ship_model="Sparrow")

        writer_b.close()
        writer_c.close()
        await writer_b.wait_closed()
        await writer_c.wait_closed()
    finally:
        process.terminate()
        try:
            await asyncio.wait_for(process.communicate(), timeout=timeout)
        except asyncio.TimeoutError:
            process.kill()
            await process.communicate()

    if process.returncode not in (0, 1, -15, 15):
        raise SmokeFailure(f"standalone relay exited unexpectedly with code {process.returncode}")


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=default_executable(repo))
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--server-world", action="store_true",
        help="enable the experimental standalone server-world mode")
    args = parser.parse_args()

    try:
        asyncio.run(run_smoke(args.exe.resolve(), repo, args.timeout, args.server_world))
    except SmokeFailure as error:
        print(error, file=sys.stderr)
        return 1

    if args.server_world:
        print("headless executable co-op relay server-world smoke passed")
    else:
        print("headless executable co-op relay smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
