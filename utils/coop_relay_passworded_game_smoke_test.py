#!/usr/bin/env python3
"""Smoke-test two game clients joining a passworded executable Co-op Relay."""

from __future__ import annotations

import argparse
import asyncio
import os
from pathlib import Path
import socket
import sys

from coop_relay_game_smoke_test import SmokeFailure, clear_copied_save_state, default_executable, run_client


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


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
                raise SmokeFailure("passworded executable relay did not open its TCP port")
            await asyncio.sleep(0.1)


async def run_smoke(exe: Path, repo: Path, timeout: float, debug: bool) -> None:
    if not exe.exists():
        raise SmokeFailure(f"Endless Sky executable not found: {exe}")

    port = free_port()
    password = "game-smoke-pass"
    relay = await asyncio.create_subprocess_exec(
        str(exe),
        "--coop-relay-server",
        str(port),
        "--coop-relay-room",
        "Passworded Game Smoke",
        "--coop-relay-password",
        password,
        cwd=repo,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
    )

    try:
        await wait_for_server(port, timeout=max(20.0, min(timeout, 60.0)))
        endpoint = f"127.0.0.1:{port}"
        config_source = repo / "tests" / "integration" / "config"
        if not config_source.exists():
            raise SmokeFailure(f"Integration config not found: {config_source}")

        import shutil
        import tempfile

        with tempfile.TemporaryDirectory(prefix="endless-sky-passworded-coop-smoke-") as temp_dir:
            temp = Path(temp_dir)
            config_a = temp / "config-a"
            config_b = temp / "config-b"
            shutil.copytree(config_source, config_a)
            shutil.copytree(config_source, config_b)
            clear_copied_save_state(config_a)
            clear_copied_save_state(config_b)

            results = await asyncio.gather(
                run_client(exe, repo, config_a, "Co-op Relay Password Smoke A", endpoint, timeout, password, debug),
                run_client(exe, repo, config_b, "Co-op Relay Password Smoke B", endpoint, timeout, password, debug),
            )

        failures: list[str] = []
        for role, (return_code, output) in zip(("A", "B"), results):
            if return_code:
                failures.append(f"Client {role} failed with exit code {return_code}:\n{output.strip()}")
        if failures:
            raise SmokeFailure("\n\n".join(failures))
    finally:
        relay.terminate()
        try:
            await asyncio.wait_for(relay.communicate(), timeout=timeout)
        except asyncio.TimeoutError:
            relay.kill()
            await relay.communicate()


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=default_executable(repo))
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--debug", action="store_true",
        help="pass --debug through to the Endless Sky integration clients")
    args = parser.parse_args()

    try:
        asyncio.run(run_smoke(args.exe.resolve(), repo, args.timeout, args.debug))
    except SmokeFailure as error:
        print(error, file=sys.stderr)
        return 1

    print("passworded executable co-op relay game smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
