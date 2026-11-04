"""Local diagnostic transport; NOT wired to the game or Seamless sessions.

Length-delimited JSON over TCP. The relay is loopback-only with no history.
Two peers load the real native core, exchange synthetic pickups, and apply them
to mock inventories. No Elden Ring process, password, save, or memory is read.
"""
import argparse
import asyncio
import ctypes as c
import json
import pathlib
import secrets

class Event(c.Structure):
    _fields_ = [("origin", c.c_uint64), ("sequence", c.c_uint64),
                ("item", c.c_uint32), ("quantity", c.c_uint32),
                ("source", c.c_uint32), ("reserved", c.c_uint32)]

Grant = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_uint32)

class Core:
    def __init__(self, library, peer):
        self.lib = c.CDLL(str(pathlib.Path(library).resolve()))
        self.lib.SL_ApiVersion.restype = c.c_uint32
        if self.lib.SL_ApiVersion() != 1:
            raise RuntimeError("Unsupported core ABI")
        self.lib.SL_Create.argtypes = [c.c_uint64]
        self.lib.SL_Create.restype = c.c_void_p
        self.lib.SL_Destroy.argtypes = [c.c_void_p]
        self.lib.SL_Capture.argtypes = [c.c_void_p, c.c_uint32, c.c_uint32, c.c_uint32]
        self.lib.SL_NextOutgoing.argtypes = [c.c_void_p, c.POINTER(Event)]
        self.lib.SL_Receive.argtypes = [c.c_void_p, c.POINTER(Event)]
        self.lib.SL_ApplyNext.argtypes = [c.c_void_p, Grant, c.c_void_p]
        self.context = self.lib.SL_Create(peer)
        if not self.context:
            raise RuntimeError("Cannot allocate core")
        self.inventory = {}
        self.callback = Grant(self._grant)

    def close(self):
        if self.context:
            self.lib.SL_Destroy(self.context)
            self.context = None

    def _grant(self, _, item, quantity):
        # Mimic an acquisition hook firing inside the game grant call.
        if self.lib.SL_Capture(self.context, item, quantity, 1) != 2:
            return 0
        self.inventory[item] = self.inventory.get(item, 0) + quantity
        return 1

    def capture(self, item, quantity, source):
        if self.lib.SL_Capture(self.context, item, quantity, source) != 0:
            raise RuntimeError("Capture rejected")
        # Simulated original pickup: the collector already owns the reward.
        self.inventory[item] = self.inventory.get(item, 0) + quantity
        event = Event()
        if self.lib.SL_NextOutgoing(self.context, c.byref(event)) != 0:
            raise RuntimeError("Missing outgoing event")
        return {name: getattr(event, name) for name, _ in Event._fields_}

    def receive(self, value):
        event = Event(**value)
        result = self.lib.SL_Receive(self.context, c.byref(event))
        if result == 3:
            return "duplicate"
        if result != 0:
            raise RuntimeError(f"Receive rejected: {result}")
        if self.lib.SL_ApplyNext(self.context, self.callback, None) != 0:
            raise RuntimeError("Mock grant failed")
        outgoing = Event()
        if self.lib.SL_NextOutgoing(self.context, c.byref(outgoing)) != 1:
            raise RuntimeError("Grant rebroadcast loop")
        return "granted"

async def send(writer, value):
    data = json.dumps(value, separators=(",", ":")).encode()
    if len(data) > 4096:
        raise ValueError("Message too large")
    writer.write(len(data).to_bytes(4, "big") + data)
    await writer.drain()

async def read(reader):
    length = int.from_bytes(await reader.readexactly(4), "big")
    if not 0 < length <= 4096:
        raise ValueError("Invalid frame size")
    value = json.loads(await reader.readexactly(length))
    if not isinstance(value, dict):
        raise ValueError("Expected object")
    return value

def valid_event(event, peer):
    bounds = {"origin": (peer, peer), "sequence": (1, 2**64 - 1),
              "item": (1, 2**32 - 1), "quantity": (1, 999),
              "source": (1, 2), "reserved": (0, 0)}
    return isinstance(event, dict) and event.keys() == bounds.keys() and all(
        type(event[key]) is int and low <= event[key] <= high
        for key, (low, high) in bounds.items())

class Relay:
    def __init__(self, token):
        self.token = token
        self.peers = {}
        self.events = 0
        self.lock = asyncio.Lock()

    async def connection(self, reader, writer):
        peer = None
        try:
            hello = await asyncio.wait_for(read(reader), 5)
            async with self.lock:
                if (hello.get("token") != self.token or
                    type(hello.get("peer")) is not int or
                    not 0 < hello["peer"] < 2**64 or
                    hello["peer"] in self.peers or len(self.peers) >= 8):
                    return
                peer = hello["peer"]
                self.peers[peer] = writer
                await send(writer, {"ready": peer})
            while True:
                event = await read(reader)
                if not valid_event(event, peer):
                    raise ValueError("Invalid event or spoofed origin")
                async with self.lock:
                    self.events += 1
                    for other, target in list(self.peers.items()):
                        if other != peer:
                            try:
                                await asyncio.wait_for(send(target, event), 5)
                            except (ConnectionError, asyncio.TimeoutError):
                                target.close()
        except (asyncio.IncompleteReadError, ConnectionError, ValueError,
                asyncio.TimeoutError, UnicodeError, json.JSONDecodeError):
            pass
        finally:
            if peer is not None:
                async with self.lock:
                    self.peers.pop(peer, None)
            writer.close()
            await writer.wait_closed()

async def connect(port, peer, token):
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    await send(writer, {"peer": peer, "token": token})
    if await asyncio.wait_for(read(reader), 3) != {"ready": peer}:
        raise RuntimeError("Handshake failed")
    return reader, writer

async def self_test(library):
    token = secrets.token_hex(16)
    relay = Relay(token)
    server = await asyncio.start_server(relay.connection, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    a, b, late = Core(library, 101), Core(library, 202), Core(library, 303)
    writers = []
    try:
        ra, wa = await connect(port, 101, token)
        rb, wb = await connect(port, 202, token)
        writers += [wa, wb]
        first = a.capture(100, 3, 1)
        await send(wa, first)
        assert b.receive(await asyncio.wait_for(read(rb), 3)) == "granted"
        # Duplicate network delivery does not grant again.
        await send(wa, first)
        assert b.receive(await asyncio.wait_for(read(rb), 3)) == "duplicate"
        # B collects its own remaining world copy; everyone gets another copy.
        await send(wb, b.capture(100, 3, 2))
        assert a.receive(await asyncio.wait_for(read(ra), 3)) == "granted"
        assert a.inventory == b.inventory == {100: 6}
        rl, wl = await connect(port, 303, token)
        writers.append(wl)
        try:
            await asyncio.wait_for(read(rl), 0.1)
            raise AssertionError("Unexpected late-join replay")
        except asyncio.TimeoutError:
            pass
        await send(wa, a.capture(200, 1, 1))
        b.receive(await asyncio.wait_for(read(rb), 3))
        late.receive(await asyncio.wait_for(read(rl), 3))
        assert late.inventory == {200: 1}
        assert a.inventory == b.inventory == {100: 6, 200: 1}
        print("PASS: TCP framing, two-way sharing, quantities, duplicate delivery, repeat pickups, no catch-up, no echo")
    finally:
        for writer in writers:
            writer.close()
        await asyncio.gather(*(w.wait_closed() for w in writers))
        server.close()
        await server.wait_closed()
        a.close(); b.close(); late.close()

async def run(args):
    if args.command == "self-test":
        await self_test(args.library)
        return
    if args.command == "relay":
        relay = Relay(args.token)
        async with await asyncio.start_server(relay.connection, "127.0.0.1", args.port) as server:
            print(f"Diagnostic relay listening on 127.0.0.1:{args.port}", flush=True)
            await server.serve_forever()
    else:
        core = Core(args.library, args.peer)
        reader, writer = await connect(args.port, args.peer, args.token)
        try:
            print("READY", flush=True)
            # Deterministic synthetic pickup, useful for separate-process smoke tests.
            if args.pickup:
                item, quantity, source = args.pickup
                await send(writer, core.capture(item, quantity, source))
            for _ in range(args.receive):
                value = await asyncio.wait_for(read(reader), 10)
                print(json.dumps({"result": core.receive(value), "inventory": core.inventory}), flush=True)
        finally:
            writer.close()
            await writer.wait_closed()
            core.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["relay", "peer", "self-test"])
    parser.add_argument("--library", default="build/libSharedLoot.so")
    parser.add_argument("--port", type=int, default=49231)
    parser.add_argument("--token", default="local-diagnostic-only")
    parser.add_argument("--peer", type=int, default=101)
    parser.add_argument("--pickup", type=int, nargs=3, metavar=("ITEM", "QTY", "SOURCE"))
    parser.add_argument("--receive", type=int, default=1)
    asyncio.run(run(parser.parse_args()))
