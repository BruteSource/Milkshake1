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

On-demand: with 88 cameras, pulling all of them constantly would be
wasteful. Each CameraSource only starts its background puller (segment
fetch + ffmpeg extract) when a client requests it, and stops itself after
IDLE_TIMEOUT_S with no viewers.

Pre-warmed URLs: resolving a YouTube Live stream's signed HLS URL
(`yt-dlp -g`) takes ~2s and was the single biggest cold-start cost --
every grid cell that hadn't been viewed recently paid it on top of the
segment fetch, and a 4-cell page did this serially. A lightweight
background thread (`_url_warmer`) keeps every camera's URL pre-resolved
on a rolling basis regardless of viewer activity (cheap: just a metadata
call, no video pulled), so a cold viewer request only ever pays for the
segment fetch + ffmpeg extract, not the resolve.

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
TARGET_FPS = 2  # frames extracted per second of source video -- try bumping
                 # this now that URL-warming cut latency; the real ceiling
                 # if any is the ESP32's own JPEG decode+draw speed, not
                 # this relay, so watch the device for lag/backlog before
                 # pushing higher
IDLE_TIMEOUT_S = 90  # stop pulling a camera this long after its last viewer leaves
URL_REFRESH_S = 20 * 60  # re-resolve each camera's HLS URL this often (well under the ~6h signed-URL expiry)


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
        with self.lock:
            self._hls_url = url
            self._hls_resolved_at = time.time()

    def _latest_segment_url(self):
        with self.lock:
            stale = self._hls_url is None or time.time() - self._hls_resolved_at > URL_REFRESH_S
        if stale:
            self._resolve_hls()  # normally already warm -- see _url_warmer

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

    def _fetch_next_segment(self, deadline):
        """Poll for a new HLS segment and fetch it, retrying until one
        appears (the playlist doesn't roll over instantly) or deadline."""
        while time.time() < deadline and not self._idle_too_long():
            try:
                seg_url = self._latest_segment_url()
                if seg_url is not None:
                    resp = requests.get(seg_url, timeout=10)
                    resp.raise_for_status()
                    return resp.content
            except Exception as e:
                print(f"[{self.cfg['id']}] segment fetch error: {e}")
            time.sleep(1)
        return None

    def _run(self):
        # Fetch+decode of each ~5-6s HLS segment was happening after all of
        # the previous segment's frames were published, so playback stalled
        # for a beat every segment boundary (~every 6-10 frames at 2fps).
        # Fix: prefetch the next segment on a background thread while the
        # current segment's frames are still being paced out, so the fetch
        # overlaps with playback instead of blocking it.
        print(f"[{self.cfg['id']}] starting")
        next_bytes = None
        while not self._idle_too_long():
            try:
                if next_bytes is not None:
                    seg_bytes = next_bytes
                    next_bytes = None
                else:
                    seg_bytes = self._fetch_next_segment(time.time() + 10)
                    if seg_bytes is None:
                        continue

                frames = self._extract_frames(seg_bytes)
                if not frames:
                    continue

                prefetch = {}
                publish_span = len(frames) / TARGET_FPS
                t = threading.Thread(
                    target=lambda: prefetch.__setitem__(
                        "bytes", self._fetch_next_segment(time.time() + publish_span + 5)),
                    daemon=True, name=f"cam-{self.cfg['id']}-prefetch")
                t.start()

                for f in frames:
                    self._publish(f)
                    time.sleep(1.0 / TARGET_FPS)
                    if self._idle_too_long():
                        break

                t.join(timeout=5)
                next_bytes = prefetch.get("bytes")
            except Exception as e:
                print(f"[{self.cfg['id']}] error: {e}")
                time.sleep(5)
        with self.lock:
            self._running = False
            self.frame = None
            # Deliberately NOT clearing _hls_url here -- _url_warmer keeps it
            # fresh independently of viewer activity, so the next cold start
            # only pays for the segment fetch, not a fresh yt-dlp resolve.
        print(f"[{self.cfg['id']}] idled out, stopped")


SOURCES = {c["id"]: CameraSource(c) for c in CAMERAS}


def _url_warmer():
    """Keeps every camera's HLS URL resolved, regardless of viewer
    activity, so a cold viewer request skips the ~2s yt-dlp resolve step.
    Staggered (a few seconds between each) so a full pass doesn't fire ~90
    yt-dlp processes at YouTube simultaneously; a full pass takes a few
    minutes, well under URL_REFRESH_S."""
    stagger_s = max(2.0, URL_REFRESH_S / max(len(SOURCES), 1) * 0.5)
    while True:
        for src in SOURCES.values():
            try:
                src._resolve_hls()
            except Exception as e:
                print(f"[warmer] {src.cfg['id']} resolve failed: {e}")
            time.sleep(stagger_s)


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
    threading.Thread(target=_url_warmer, daemon=True, name="url-warmer").start()
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"livecam-relay listening on :{PORT} ({len(SOURCES)} cameras, on-demand, URL-warmed)")
    server.serve_forever()
