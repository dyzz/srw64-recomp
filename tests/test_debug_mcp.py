"""Debug interface clients: socket JSON-RPC, key steps, waits, events and MCP (docs/guide/debug-interface.md)."""
from __future__ import annotations

import base64
import json
from pathlib import Path
import socket
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from recomp.debug import session as debug_session  # noqa: E402
from recomp.debug.mcp_server import TOOLS, Server  # noqa: E402
from recomp.debug.session import Client, HostError, Session, run_keys, satisfied  # noqa: E402

PNG = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=")


class FakeHost:
    """A debug socket that answers like the host, recording every request."""

    def __init__(self, run: Path):
        self.run = run
        self.requests: list[dict] = []
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.bind(str(run / "debug.sock"))
        self.listener.listen(4)
        self.status = {"vi": 120, "dialogue": {"active": False, "boxes": []}, "intro": {"title_major": 3, "step": {}},
                       "name_page": {"visible": False}}
        threading.Thread(target=self.serve, daemon=True).start()

    def answer(self, method: str, params: dict):
        if method == "status":
            return self.status
        if method == "keys":
            return {"held": [], "vi": self.status["vi"]}
        if method == "screenshot":
            path = self.run / "shot.png"
            path.write_bytes(PNG)
            return {"path": str(path), "width": 1, "height": 1, "overlays": []}
        if method == "fail":
            raise ValueError("refused")
        return {"method": method, "params": params}

    def serve(self):
        while True:
            try:
                connection, _ = self.listener.accept()
            except OSError:
                return
            with connection:
                buffer = b""
                while True:
                    chunk = connection.recv(4096)
                    if not chunk:
                        break
                    buffer += chunk
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        request = json.loads(line)
                        self.requests.append(request)
                        try:
                            body = {"result": self.answer(request["method"], request["params"])}
                        except ValueError as error:
                            body = {"error": {"code": -32000, "message": str(error)}}
                        connection.sendall(json.dumps({"jsonrpc": "2.0", "id": request["id"], **body}).encode() + b"\n")

    def close(self):
        self.listener.close()


class DebugClientTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir="/tmp")
        self.run = Path(self.directory.name)
        self.host = FakeHost(self.run)
        self.addCleanup(self.directory.cleanup)
        self.addCleanup(self.host.close)

    def test_calls_are_json_rpc_lines_and_errors_raise(self):
        client = Client(self.run / "debug.sock")
        self.assertEqual(client.call("status")["vi"], 120)
        self.assertEqual(client.call("echo", a=1), {"method": "echo", "params": {"a": 1}})
        with self.assertRaisesRegex(HostError, "refused"):
            client.call("fail")
        first, second = self.host.requests[:2]
        self.assertEqual((first["jsonrpc"], first["method"], first["params"]), ("2.0", "status", {}))
        self.assertEqual(second["id"], first["id"] + 1)
        client.close()

    def test_missing_socket_is_a_host_error(self):
        with self.assertRaises(HostError):
            Client(self.run / "none.sock").call("status")

    def test_key_steps(self):
        client = Client(self.run / "debug.sock")
        self.addCleanup(client.close)
        results = run_keys(client, [{"press": "e+return", "hold_ms": 50}, {"wait_ms": 1}, {"down": "e+z"}])
        self.assertEqual(len(results), 3)
        sent = [r["params"] for r in self.host.requests]
        self.assertEqual(sent, [{"press": "e+return", "hold_ms": 50}, {"down": "e+z"}])
        with self.assertRaises(HostError):
            run_keys(client, [{"press": "i", "repeat": 3}])
        with self.assertRaises(HostError):
            run_keys(client, [{"wait_ms": -1}])

    def test_wait_conditions(self):
        status = {"vi": 500, "dialogue": {"active": True, "boxes": [{"active": True, "text": "「状況を確認するぞ」"}]},
                  "intro": {"title_major": 3, "step": {"active": False}}, "name_page": {"visible": False}}
        self.assertTrue(satisfied(status, {"vi": 400, "dialogue_active": True, "text": "状況", "title_major": 3}))
        self.assertFalse(satisfied(status, {"vi": 600}))
        self.assertFalse(satisfied(status, {"name_page": True}))
        with self.assertRaises(HostError):
            satisfied(status, {"colour": "red"})

    def test_events_follow_a_cursor_and_filter_kinds(self):
        log = self.run / "dialogue-events.jsonl"
        log.write_text('{"kind":"fragment","vi":1}\n{"kind":"font","size":14}\n')
        session = Session(self.run)
        rows, cursor = session.events("dialogue", 0, ["font"])
        self.assertEqual((rows, cursor), ([{"kind": "font", "size": 14}], 2))
        with log.open("a") as out:
            out.write('{"kind":"font","size":15}\n{"kind":"fo')  # the last line is still being written
        rows, cursor = session.events("dialogue", cursor)
        self.assertEqual((rows, cursor), ([{"kind": "font", "size": 15}], 4))
        with self.assertRaises(HostError):
            session.events("nope")

    def test_attach_uses_the_current_run(self):
        current = self.run / "current"
        current.write_text(str(self.run) + "\n")
        original = debug_session.CURRENT
        debug_session.CURRENT = current
        self.addCleanup(setattr, debug_session, "CURRENT", original)
        attached = Session.attach()
        self.addCleanup(attached.client.close)
        self.assertEqual(attached.run, self.run)
        (self.run / "debug.sock").unlink()
        with self.assertRaises(HostError):
            Session.attach()


class McpServerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir="/tmp")
        self.run = Path(self.directory.name)
        self.host = FakeHost(self.run)
        self.addCleanup(self.directory.cleanup)
        self.addCleanup(self.host.close)
        self.server = Server()
        self.addCleanup(lambda: self.server.session and self.server.session.client.close())

    def call(self, name, arguments=None):
        return self.server.handle({"jsonrpc": "2.0", "id": 9, "method": "tools/call",
                                   "params": {"name": name, "arguments": arguments or {}}})["result"]

    def test_handshake_and_tool_list(self):
        reply = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                    "params": {"protocolVersion": "2025-03-26", "capabilities": {}}})
        self.assertEqual(reply["result"]["protocolVersion"], "2025-03-26")
        self.assertIn("tools", reply["result"]["capabilities"])
        self.assertIsNone(self.server.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}))
        tools = self.server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})["result"]["tools"]
        names = {tool["name"] for tool in tools}
        self.assertTrue({"srw64_launch", "srw64_keys", "srw64_screenshot", "srw64_click", "srw64_quit"} <= names)
        for tool in TOOLS:
            self.assertEqual(tool["inputSchema"]["type"], "object", tool["name"])
        self.assertEqual(self.server.handle({"jsonrpc": "2.0", "id": 3, "method": "nope"})["error"]["code"], -32601)

    def test_tool_calls_reach_the_host(self):
        self.server.session = Session(self.run)
        status = self.call("srw64_status")
        self.assertFalse(status["isError"])
        self.assertEqual(json.loads(status["content"][0]["text"])["vi"], 120)
        self.call("srw64_keys", {"press": "i", "hold_ms": 10})
        self.call("srw64_click", {"text": "开始故事"})
        self.assertEqual([r["method"] for r in self.host.requests], ["status", "keys", "ui.click"])
        self.assertEqual(self.host.requests[2]["params"], {"text": "开始故事"})

    def test_screenshot_returns_the_image(self):
        self.server.session = Session(self.run)
        content = self.call("srw64_screenshot")["content"]
        self.assertEqual((content[0]["type"], content[0]["mimeType"]), ("image", "image/png"))
        self.assertEqual(base64.b64decode(content[0]["data"]), PNG)

    def test_errors_are_tool_errors_not_crashes(self):
        self.server.session = Session(self.run)
        result = self.call("srw64_keys", {})
        self.assertTrue(result["isError"])
        self.assertTrue(self.call("srw64_nope")["isError"])


if __name__ == "__main__":
    unittest.main()
