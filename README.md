# Learning C

My journey from JavaScript to C. Small programs that each explore one low-level concept: memory, files, sockets, the Linux camera API and image processing — building up to a working **motion detector that sends Telegram alerts**.

## Highlight: live motion detector

[`camera/live-motion-complete.c`](camera/live-motion-complete.c) talks directly to a webcam through **Video4Linux2** (no OpenCV):

1. Opens `/dev/video0` and sets up memory-mapped (`mmap`) capture buffers
2. Decodes each MJPEG frame to grayscale with **libjpeg-turbo**
3. Compares it pixel by pixel with the previous frame
4. When enough pixels change, encodes a snapshot and uploads it to a **Telegram bot** with **libcurl** (MIME API)

```bash
sudo apt install libcurl4-openssl-dev libturbojpeg0-dev
gcc camera/live-motion-complete.c -o live-motion -lcurl -lturbojpeg

export TELEGRAM_BOT_TOKEN="..."
export TELEGRAM_CHAT_ID="..."
./live-motion
```

## Overview

| Folder | What it explores |
|---|---|
| `strings/` | C strings, `strcpy` / `strcmp` |
| `file/` | Reading a file into a buffer and parsing CSV by hand |
| `http-requests/` | Raw HTTP POST over a TCP socket (`getaddrinfo`, `connect`, `send`) — uploads an image to a small Express server in `http-server/node-server/` |
| `http-server/` | A minimal HTTP server with `socket` / `bind` / `listen` / `accept` that serves JSON (module without `main`, used in `database/main.c`) |
| `database/` | Querying PostgreSQL with **libpq** and serving the rows as JSON over HTTP |
| `camera/camera.c` | First contact with V4L2: open the device, set the format, grab a frame |
| `camera/klik_camera.c` | Take a photo on keypress (terminal in raw mode, non-blocking input with `select`) |
| `camera/video.c`, `video_mp4.c` | Record 150 frames (5 s @ 30 fps) using a ring of 8 buffers |
| `camera/test-motion.c` | Motion detection on still images with [stb_image](https://github.com/nothings/stb) |
| `camera/live-motion.c` | First live version of the motion detector |
| `camera/live-motion-complete.c` | Cleaned-up final version (see above) |

## Building

Most programs build with a plain `gcc file.c -o name`. Extra libraries:

| Program | Flags |
|---|---|
| `database/main.c` | `-lpq` (and set `DATABASE_URL`) |
| `camera/live-motion*.c` | `-lcurl -lturbojpeg -lm` |
| `camera/test-motion.c` | `-lm` |

Tested on Linux (Ubuntu) with a USB webcam.

## What I learned

- Managing memory and buffer sizes myself instead of relying on a garbage collector
- How HTTP really works underneath frameworks like Express: it's just text over a socket
- Talking to hardware through `ioctl` and memory-mapped buffers
- Keeping secrets out of source code (environment variables instead of hardcoded tokens)
