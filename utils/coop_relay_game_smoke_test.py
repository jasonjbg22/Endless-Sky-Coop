#!/usr/bin/env python3
"""Smoke-test Co-op Relay with two real Endless Sky integration clients."""

from __future__ import annotations

import argparse
import asyncio
import os
from pathlib import Path
import shutil
import sys
import tempfile

from coop_relay_server import RelayState, handle_client


class SmokeFailure(RuntimeError):
    pass


def default_executable(repo: Path) -> Path:
    if os.name == "nt":
        return repo / "build" / "vanilla-msvc" / "Debug" / "Endless Sky.exe"
    return repo / "build" / "endless-sky"


def clear_copied_save_state(config: Path) -> None:
    saves = config / "saves"
    if saves.exists():
        shutil.rmtree(saves)
    saves.mkdir()
    recent = config / "recent.txt"
    if recent.exists():
        recent.unlink()


async def run_client(
    exe: Path,
    repo: Path,
    config: Path,
    test_name: str,
    endpoint: str,
    timeout: float,
    password: str = "",
    debug: bool = False,
) -> tuple[int, str]:
    command = [
        str(exe),
        "--config",
        str(config),
        "--resources",
        str(repo),
        "--test",
        test_name,
        "--coop-relay-connect",
        endpoint,
    ]
    if password:
        command.extend(["--coop-relay-password", password])
    if debug:
        command.append("--debug")

    process = await asyncio.create_subprocess_exec(
        *command,
        cwd=repo,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
    )
    try:
        output, _ = await asyncio.wait_for(process.communicate(), timeout=timeout)
    except asyncio.TimeoutError:
        process.terminate()
        try:
            output, _ = await asyncio.wait_for(process.communicate(), timeout=10)
        except asyncio.TimeoutError:
            process.kill()
            output, _ = await process.communicate()
        return 124, output.decode("utf-8", errors="replace")

    return process.returncode or 0, output.decode("utf-8", errors="replace")


async def run_smoke(exe: Path, repo: Path, timeout: float, rendered: bool, debug: bool,
    password: str = "") -> None:
    if not exe.exists():
        raise SmokeFailure(f"Endless Sky executable not found: {exe}")

    config_source = repo / "tests" / "integration" / "config"
    if not config_source.exists():
        raise SmokeFailure(f"Integration config not found: {config_source}")

    state = RelayState(debug=debug)
    server = await asyncio.start_server(
        lambda reader, writer: handle_client(reader, writer, state),
        "127.0.0.1",
        0,
    )
    port = server.sockets[0].getsockname()[1]
    endpoint = f"127.0.0.1:{port}"

    with tempfile.TemporaryDirectory(prefix="endless-sky-coop-smoke-") as temp_dir:
        temp = Path(temp_dir)
        config_a = temp / "config-a"
        config_b = temp / "config-b"
        shutil.copytree(config_source, config_a)
        shutil.copytree(config_source, config_b)
        clear_copied_save_state(config_a)
        clear_copied_save_state(config_b)

        async with server:
            test_a = "Co-op Relay Render Smoke A" if rendered else "Co-op Relay Smoke A"
            test_b = "Co-op Relay Render Smoke B" if rendered else "Co-op Relay Smoke B"
            results = await asyncio.gather(
                run_client(exe, repo, config_a, test_a, endpoint, timeout, password, debug),
                run_client(exe, repo, config_b, test_b, endpoint, timeout, password, debug),
            )

    failures: list[str] = []
    for role, (return_code, output) in zip(("A", "B"), results):
        if return_code:
            failures.append(f"Client {role} failed with exit code {return_code}:\n{output.strip()}")

    if failures:
        if state.transcript:
            failures.append("Relay transcript:\n" + "\n".join(state.transcript[-80:]))
        else:
            failures.append("Relay transcript: no client lines observed.")
        raise SmokeFailure("\n\n".join(failures))

    if len(state.seen_peer_endpoint_players) < 2:
        raise SmokeFailure(
            f"expected two peer endpoint advertisements, got {len(state.seen_peer_endpoint_players)}"
        )


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=default_executable(repo))
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--rendered", action="store_true",
        help="run the slower non-headless smoke that proves the flight overlay draw path")
    parser.add_argument("--debug", action="store_true",
        help="pass --debug through to the Endless Sky integration clients")
    parser.add_argument("--password", default="",
        help="pass a Co-op Relay room password through to the Endless Sky integration clients")
    args = parser.parse_args()

    try:
        asyncio.run(run_smoke(args.exe.resolve(), repo, args.timeout, args.rendered, args.debug, args.password))
    except SmokeFailure as error:
        print(error, file=sys.stderr)
        return 1

    if args.rendered:
        print("two-game-client rendered co-op relay smoke passed")
    else:
        print("two-game-client co-op relay smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
