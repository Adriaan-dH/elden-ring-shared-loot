"""Exercise the diagnostic bridge over sockets and real subprocesses."""
import asyncio
import importlib.util
import os
import pathlib
import secrets
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "diagnostic_network.py"
spec = importlib.util.spec_from_file_location("diagnostic_network", SCRIPT)
network = importlib.util.module_from_spec(spec)
spec.loader.exec_module(network)
LIBRARY = os.environ.get("SL_TEST_LIBRARY", str(ROOT / "build" / "libSharedLoot.so"))

class NetworkTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.token = secrets.token_hex(16)
        self.relay = network.Relay(self.token)
        self.server = await asyncio.start_server(self.relay.connection, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]
        self.writers = []

    async def asyncTearDown(self):
        for writer in self.writers:
            writer.close()
        await asyncio.gather(*(writer.wait_closed() for writer in self.writers))
        self.server.close()
        await self.server.wait_closed()

    async def test_sharing_and_late_join(self):
        await network.self_test(LIBRARY)

    async def test_bad_token_and_spoofed_origin(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self.writers.append(writer)
        await network.send(writer, {"peer": 101, "token": "wrong"})
        self.assertEqual(await asyncio.wait_for(reader.read(), 3), b"")
        reader, writer = await network.connect(self.port, 101, self.token)
        self.writers.append(writer)
        await network.send(writer, {"origin": 202, "sequence": 1, "item": 100,
                                    "quantity": 1, "source": 1, "reserved": 0})
        self.assertEqual(await asyncio.wait_for(reader.read(), 3), b"")
        self.assertEqual(self.relay.events, 0)

    async def test_separate_processes(self):
        common = [sys.executable, str(SCRIPT), "peer", "--library", LIBRARY,
                  "--port", str(self.port), "--token", self.token]
        receiver = await asyncio.create_subprocess_exec(
            *common, "--peer", "202", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        sender = None
        try:
            self.assertEqual((await asyncio.wait_for(receiver.stdout.readline(), 5)).strip(), b"READY")
            sender = await asyncio.create_subprocess_exec(
                *common, "--peer", "101", "--pickup", "100", "3", "1", "--receive", "0",
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            sender_out, sender_err = await asyncio.wait_for(sender.communicate(), 10)
            receiver_out, receiver_err = await asyncio.wait_for(receiver.communicate(), 10)
            self.assertEqual(sender.returncode, 0, sender_err.decode())
            self.assertEqual(receiver.returncode, 0, receiver_err.decode())
            self.assertIn(b'"inventory": {"100": 3}', receiver_out)
        finally:
            for process in (receiver, sender):
                if process and process.returncode is None:
                    process.kill()
                    await process.wait()

if __name__ == "__main__":
    unittest.main()
