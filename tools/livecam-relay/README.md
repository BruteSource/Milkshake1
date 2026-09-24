# livecam-relay

Local relay that pulls explore.org's officially-operated live nature cam
network (YouTube Live) and re-serves it to the CYD-Milkshake board as plain
local-network MJPEG at ~1fps.

## Why this exists

The ESP32-S3 has no H.264/video decoder and can't demux HLS -- it can only
decode individual JPEGs (`LGFX::drawJpg`). YouTube Live streams are H.264 in
HLS segments, so something with real CPU has to do that decode. This host
resolves the stream (`yt-dlp`), downloads segments over HTTPS via `requests`
(ffmpeg's own HTTPS client crashed in testing -- see git log), and runs
ffmpeg only on local bytes to pull out JPEG frames, then serves those as a
standard multipart MJPEG stream over plain HTTP (no TLS) on the LAN.

On-demand: with 35 cameras, each `CameraSource` only starts pulling when a
client requests it and stops itself after `IDLE_TIMEOUT_S` idle, so idle
cameras cost nothing.

## Deployment (currently: `sean@192.168.0.168`, a Linux Mint laptop)

- `~/.local/bin/ffmpeg` -- static build (johnvansickle.com), needed because
  the distro's ffmpeg wasn't installed and this avoids an apt/sudo dependency.
- `~/.local/bin/yt-dlp` -- standalone binary, needed because the distro's
  apt-packaged yt-dlp (2024.04.09) is too old for current YouTube and fails
  to resolve streams; this is a fresh build from GitHub releases.
- `~/livecam-relay/` -- `relay_server.py` + `cameras.py`, deployed via sftp.
- Runs as a systemd service, `livecam-relay.service` (this file, copied to
  `/etc/systemd/system/`), enabled for boot, `Restart=on-failure`.

To redeploy after editing `relay_server.py`/`cameras.py` locally:
```
scp relay_server.py cameras.py sean@192.168.0.168:~/livecam-relay/
ssh sean@192.168.0.168 sudo systemctl restart livecam-relay
```

## API

- `GET /cameras` -- JSON `{categories: [...], cameras: [{id, title, category}]}`
- `GET /live/<id>.jpg` -- single current frame (grid thumbnails)
- `GET /live/<id>.mjpg` -- persistent multipart MJPEG stream (the viewer)

## Refreshing the camera roster

`cameras.py` was hand-compiled by running
`yt-dlp --flat-playlist -j <channel>/streams` against explore.org's YouTube
channels and keeping only `live_status == "is_live"` entries. Streams do go
offline or get replaced over time -- re-run that against the channels listed
in `cameras.py`'s header comment to refresh.
