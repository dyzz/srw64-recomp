#!/usr/bin/env python3
"""Serve the generated SRW64 viewer on loopback only."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse

ROOT=Path(__file__).resolve().parents[2]


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self) -> None:
        self.send_header('Cache-Control','no-cache')
        self.send_header('X-Content-Type-Options','nosniff')
        super().end_headers()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=0)
    args=parser.parse_args()
    folder=ROOT/'build/model-viewer'
    if not (folder/'index.html').is_file():raise RuntimeError('Build the model viewer first')
    server=ThreadingHTTPServer(('127.0.0.1',args.port),partial(Handler,directory=str(folder)))
    print(f'Local URL: http://127.0.0.1:{server.server_port}/',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:server.server_close()
