#!/usr/bin/env bash
set -euo pipefail

app=${1:?usage: capture_gif.sh APP OUTPUT [FRAMES] [FPS]}
output=${2:?usage: capture_gif.sh APP OUTPUT [FRAMES] [FPS]}
frames=${3:-48}
fps=${4:-30}

for command in Xvfb xdpyinfo ffmpeg; do
    command -v "$command" >/dev/null || {
        echo "capture-gif requires $command" >&2
        exit 1
    }
done

display=:99
video=$(mktemp --suffix=.mp4)
app_log=$(mktemp)
xvfb_pid=
ffmpeg_pid=
cleanup() {
    if [[ -n "$ffmpeg_pid" ]]; then
        kill -INT "$ffmpeg_pid" 2>/dev/null || true
        wait "$ffmpeg_pid" 2>/dev/null || true
    fi
    if [[ -n "$xvfb_pid" ]]; then
        kill "$xvfb_pid" 2>/dev/null || true
        wait "$xvfb_pid" 2>/dev/null || true
    fi
    rm -f "$video" "$app_log"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
xvfb_pid=$!
for _ in {1..50}; do
    if xdpyinfo -display "$display" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
xdpyinfo -display "$display" >/dev/null 2>&1 || {
    echo "Xvfb did not start" >&2
    exit 1
}

DISPLAY="$display" "$app" --frames "$((frames + 30))" --fps "$fps" >"$app_log" 2>&1 &
app_pid=$!
sleep 0.25
DISPLAY="$display" ffmpeg -loglevel error -y \
    -f x11grab -video_size 1920x1080 -framerate "$fps" -i "$display.0" \
    -frames:v "$frames" -c:v libx264 -pix_fmt yuv420p "$video" &
ffmpeg_pid=$!
wait "$ffmpeg_pid"
ffmpeg_pid=
if ! wait "$app_pid"; then
    cat "$app_log" >&2
    exit 1
fi

mkdir -p "$(dirname "$output")"
ffmpeg -loglevel error -y -i "$video" \
    -vf "fps=$fps,scale=960:-1:flags=lanczos,split[s0][s1];[s0]palettegen=max_colors=256[p];[s1][p]paletteuse=dither=sierra2_4a" \
    "$output"
test -s "$output"
file "$output" | grep -qi 'GIF' || {
    echo "capture did not produce a GIF: $output" >&2
    exit 1
}
echo "GIF captured: $output"
