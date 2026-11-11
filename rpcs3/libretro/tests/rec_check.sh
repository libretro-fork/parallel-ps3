#!/bin/sh
# cellRec's recordings (test_record) read back by FFmpeg, the library
# util/media_encode replaced: the codecs ffprobe names, each video frame's
# time, every frame at the recording's size and the audio at 48 kHz
# stereo, compared by
# test_record --compare.
set -eu
D=${1:-rec_out}
rm -rf "$D"
mkdir -p "$D"
./test_record "$D"
for f in "$D"/rec_*.mp4; do
	b=${f%.mp4}
	ffprobe -v error -show_entries stream=codec_name -of default=nw=1 "$f" > "$b.probe"
	ffprobe -v error -select_streams v:0 -show_entries frame=pts_time -of csv=p=0 "$f" > "$b.pts"
	ffmpeg -nostdin -hide_banner -loglevel error -xerror -i "$f" -map 0:v:0 \
		-fps_mode passthrough -pix_fmt yuv420p -f rawvideo -y "$b.yuv"
	ffmpeg -nostdin -hide_banner -loglevel error -xerror -i "$f" -map 0:a:0 \
		-ac 2 -ar 48000 -f f32le -y "$b.f32"
done
./test_record --compare "$D"
