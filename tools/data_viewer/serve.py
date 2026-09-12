#!/usr/bin/env python3
"""Serve only the generated original-data catalog on loopback."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--directory", type=Path, default=ROOT / "build/original-data")
    args = parser.parse_args()
    if not (args.directory / "manifest.json").is_file():
        raise SystemExit("Run tools/content/extract_original.py first")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(args.directory.resolve())))
    print(f"Local URL: http://127.0.0.1:{server.server_port}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
