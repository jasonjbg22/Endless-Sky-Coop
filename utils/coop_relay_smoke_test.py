#!/usr/bin/env python3
"""Smoke-test the Co-op Relay server with two real TCP clients."""

from __future__ import annotations

import asyncio
import sys

sys.dont_write_bytecode = True

from coop_relay_server import RelayState, handle_client


SNAPSHOT_1 = (
    "snapshot\t3\t1\tplayer-1\tPlayer A\tSol\t10\t-20\t0.5\t1.5\t45\t"
    "\tShuttle\t123\t0.9\t0.8\t0.7\t0.6\t0.5\t1"
)
SNAPSHOT_2 = (
    "snapshot\t3\t2\tplayer-1\tPlayer A\tAlpha Centauri\t50\t60\t2\t3\t90\t"
    "New Boston\tShuttle\t123\t0.9\t0.8\t0.2\t0.6\t0.5\t0"
)
JUMP_EVENT = "event\t3\t3\tplayer-1\tjumped\tAlpha Centauri\t\tSol"


async def read_line(reader: asyncio.StreamReader) -> str:
    line = await asyncio.wait_for(reader.readline(), 2)
    return line.decode("utf-8").rstrip("\r\n")


async def connect_player(port: int, name: str) -> tuple[asyncio.StreamReader, asyncio.StreamWriter, str]:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(f"join\t{name}\n".encode("utf-8"))
    await writer.drain()
    return reader, writer, await read_line(reader)


async def main() -> None:
    state = RelayState()
    server = await asyncio.start_server(lambda r, w: handle_client(r, w, state), "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]

    async with server:
        reader_a, writer_a, welcome_a = await connect_player(port, "Player A")
        assert welcome_a == "welcome\t3\tplayer-1", welcome_a

        writer_a.write((SNAPSHOT_1 + "\n").encode("utf-8"))
        await writer_a.drain()

        reader_b, writer_b, welcome_b = await connect_player(port, "Player B")
        assert welcome_b == "welcome\t3\tplayer-2", welcome_b
        assert await read_line(reader_b) == SNAPSHOT_1

        writer_a.write((SNAPSHOT_2 + "\n").encode("utf-8"))
        await writer_a.drain()
        assert await read_line(reader_b) == SNAPSHOT_2

        writer_a.write((JUMP_EVENT + "\n").encode("utf-8"))
        await writer_a.drain()
        assert await read_line(reader_b) == JUMP_EVENT

        writer_b.write(
            b"snapshot\t3\t4\tplayer-1\tBad\tSol\t0\t0\t0\t0\t0\t\tShuttle\t0\t1\t1\t1\t1\t0\t1\n"
        )
        await writer_b.drain()
        assert await read_line(reader_b) == "error\tplayer id mismatch"

        writer_a.close()
        writer_b.close()
        await writer_a.wait_closed()
        await writer_b.wait_closed()

        server.close()
        await server.wait_closed()

    print("two-client relay smoke passed")


if __name__ == "__main__":
    asyncio.run(main())
