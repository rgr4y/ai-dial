#!/usr/bin/env bash
# Re-encode an internet radio stream to 128k mono MP3 at 44.1kHz and serve it to the dial.
# The 1.fm source is 256k joint stereo. Mono halves the dial's MP3 decode work and skips the
# mixer's stereo-to-mono downmix. 44.1kHz matches the dial's I2S rate, so it isn't resampled.
# Filters for the small speaker: three cascaded 2-pole high-passes at 180Hz (36dB/octave; about -36dB at 90Hz) cut bass it
# can't reproduce, the main source of distortion. Then
# a 4:1 compressor above -18dB evens out loudness, and a 0.9 limiter stops clipping.
#
# Usage: scripts/restream.sh [source_url] [port]
# The dial plays http://mimi.lan:8000/blues.mp3
set -u
SRC="${1:-http://strm112.1.fm/blues_mobile_mp3}"
PORT="${2:-8000}"

# ffmpeg -listen 1 serves one client, then exits, so loop to accept the next connection.
while true; do
  echo "$(date +%T) source: $SRC"
  echo "$(date +%T) waiting for a client on http://0.0.0.0:$PORT/blues.mp3"
  ffmpeg -hide_banner -loglevel warning -stats \
    -reconnect 1 -reconnect_streamed 1 -reconnect_delay_max 5 \
    -i "$SRC" -vn -ac 1 -ar 44100 \
    -af "highpass=f=180,highpass=f=180,highpass=f=180,acompressor=threshold=-18dB:ratio=4:attack=5:release=100,alimiter=limit=0.9" \
    -c:a libmp3lame -b:a 128k \
    -f mp3 -content_type audio/mpeg -listen 1 "http://0.0.0.0:$PORT/blues.mp3"
  echo "$(date +%T) ffmpeg exited ($?), restarting in 1s"
  sleep 1
done
