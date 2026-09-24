#!/usr/bin/env python3
"""Local relay: pulls real live camera streams (YouTube Live via yt-dlp,
explore.org's officially-operated nature cam network -- see cameras.py)
and re-serves them to the CYD-Milkshake board as plain local-network
MJPEG, at ~1 frame/sec.

Why this exists: the board can only decode individual JPEGs, not any real
video codec. A YouTube HLS stream is H.264 in .ts segments, so this host
resolves the stream (yt-dlp), downloads segments over HTTPS via `requests`
(ffmpeg's own HTTPS client crashed in testing), and runs ffmpeg only on
local bytes to pull out JPEG frames. Those frames are served as a standard
multipart MJPEG stream over plain HTTP (no TLS) on the LAN.

On-demand: with 35 cameras, pulling all of them constantly would be
wasteful. Each CameraSource only starts its background puller when a
client requests it, and stops itself after IDLE_TIMEOUT_S with no viewers.

Usage: python3 relay_server.py
Then point the board at http://<this-host-LAN-IP>:8090/live/<id>.mjpg
"""
import json
import re
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import requests

from cameras import CAMERAS, CATEGORIES

FFMPEG = str(Path.home() / ".local/bin/ffmpeg")
YTDLP = str(Path.home() / ".local/bin/yt-dlp")
PORT = 8090
TARGET_FPS = 1  # frames extracted per second of source video
IDLE_TIMEOUT_S = 30  # stop pulling a camera this long after its last viewer leaves


class CameraSource:
    """On-demand background puller for one camera. Idle until a viewer
    calls touch(); keeps a single 'latest frame' slot and wakes any
    waiting HTTP clients when it changes; stops itself after
    IDLE_TIMEOUT_S with no viewers."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.lock = threading.Condition()
        self.frame = None       # latest JPEG bytes
        self.frame_seq = 0      # increments on every new frame
        self._hls_url = None
        self._hls_resolved_at = 0
        self._last_seg_seq = None
        self._viewers = 0
        self._running = False
        self._last_touch = 0

    def touch(self):
        """Call on every client request; starts the puller if idle."""
        with self.lock:
            self._last_touch = time.time()
            if not self._running:
                self._running = True
                threading.Thread(target=self._run, daemon=True,
                                  name=f"cam-{self.cfg['id']}").start()

    def _resolve_hls(self):
        out = subprocess.run(
            [YTDLP, "-g", "-f", self.cfg["format"], self.cfg["youtube_url"]],
            capture_output=True, text=True, timeout=30,
        )
        url = out.stdout.strip().splitlines()[-1] if out.stdout.strip() else None
        if not url:
            raise RuntimeError(f"yt-dlp resolve failed: {out.stderr[-500:]}")
        self._hls_url = url
        self._hls_resolved_at = time.time()

    def _latest_segment_url(self):
        if self._hls_url is None or time.time() - self._hls_resolved_at > 1800:
            self._resolve_hls()

        resp = requests.get(self._hls_url, timeout=10)
        resp.raise_for_status()
        lines = [l for l in resp.text.splitlines() if l and not l.startswith("#")]
        if not lines:
            return None
        seg_url = lines[-1]
        m = re.search(r"/sq/(\d+)/", seg_url)
        seq = int(m.group(1)) if m else None
        if seq is not None and seq == self._last_seg_seq:
            return None  # no new segment yet
        self._last_seg_seq = seq
        return seg_url

    def _extract_frames(self, ts_bytes):
        proc = subprocess.run(
            [FFMPEG, "-y", "-loglevel", "error", "-i", "pipe:0",
             "-vf", f"fps={TARGET_FPS}", "-f", "image2pipe", "-vcodec", "mjpeg", "pipe:1"],
            input=ts_bytes, capture_output=True, timeout=20,
        )
        data = proc.stdout
        starts = [m.start() for m in re.finditer(b"\xff\xd8\xff", data)]
        return [data[s:(starts[i + 1] if i + 1 < len(starts) else len(data))]
                for i, s in enumerate(starts)]

    def _publish(self, jpeg_bytes):
        with self.lock:
            self.frame = jpeg_bytes
            self.frame_seq += 1
            self.lock.notify_all()

    def _idle_too_long(self):
        with self.lock:
            return time.time() - self._last_touch > IDLE_TIMEOUT_S

    def _run(self):
        print(f"[{self.cfg['id']}] starting")
        while not self._idle_too_long():
            try:
                seg_url = self._latest_segment_url()
                if seg_url is None:
                    time.sleep(2)
                    continue
                seg_resp = requests.get(seg_url, timeout=10)
                seg_resp.raise_for_status()
                frames = self._extract_frames(seg_resp.content)
                for f in frames:
                    self._publish(f)
                    time.sleep(1.0 / TARGET_FPS)
                    if self._idle_too_long():
                        break
            except Exception as e:
                print(f"[{self.cfg['id']}] error: {e}")
                time.sleep(5)
        with self.lock:
            self._running = False
            self.frame = None
            self._hls_url = None  # force re-resolve next time (avoid stale expired urls)
        print(f"[{self.cfg['id']}] idled out, stopped")


SOURCES = {c["id"]: CameraSource(c) for c in CAMERAS}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[http] {self.address_string()} " + fmt % args)

    def do_GET(self):
        if self.path == "/cameras":
            body = json.dumps({
                "categories": CATEGORIES,
                "cameras": [{"id": c["id"], "title": c["title"], "category": c["category"]}
                            for c in CAMERAS],
            }).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        m = re.match(r"^/live/([a-zA-Z0-9_-]+)\.(mjpg|jpg)$", self.path)
        if not m:
            self.send_error(404)
            return
        cam_id, kind = m.group(1), m.group(2)
        src = SOURCES.get(cam_id)
        if not src:
            self.send_error(404, "unknown camera id")
            return
        src.touch()

        if kind == "jpg":
            # Give a cold-started source a moment to produce its first frame.
            with src.lock:
                if src.frame is None:
                    src.lock.wait(timeout=15)
                frame = src.frame
            if frame is None:
                self.send_error(503, "no frame yet")
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(frame)))
            self.end_headers()
            self.wfile.write(frame)
            return

        # MJPEG multipart stream.
        boundary = "cydframe"
        self.send_response(200)
        self.send_header("Content-Type", f"multipart/x-mixed-replace; boundary={boundary}")
        self.end_headers()
        last_seq = None
        try:
            while True:
                src.touch()
                with src.lock:
                    while src.frame_seq == last_seq or src.frame is None:
                        if not src.lock.wait(timeout=15):
                            raise TimeoutError
                    frame = src.frame
                    last_seq = src.frame_seq
                header = (
                    f"--{boundary}\r\n"
                    f"Content-Type: image/jpeg\r\n"
                    f"Content-Length: {len(frame)}\r\n\r\n"
                ).encode()
                self.wfile.write(header)
                self.wfile.write(frame)
                self.wfile.write(b"\r\n")
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass


if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"livecam-relay listening on :{PORT} ({len(SOURCES)} cameras, on-demand)")
    server.serve_forever()
