#!/bin/sh
# Makes fixtures/media/: music and video files FFmpeg writes, the old
# library's decode of each music file (as utils::audio_decoder had it
# resample: 48 kHz stereo, float, then stored as s16le) and what ffprobe
# reads from each (expected.txt). FFmpeg 6.1.1 made the committed ones.
set -eu
D=fixtures/media
mkdir -p "$D"
T="-metadata title=Tïtle☃ -metadata artist=Ärtist -metadata album=Albüm -metadata genre=Rock -metadata track=3/12 -metadata disc=1/2 -metadata date=2004-05-01"
two="sine=f=440:d=0.25:sample_rate=48000[a];sine=f=1000:d=0.25:sample_rate=48000[b];[a][b]amerge=inputs=2"
six="sine=f=300:d=0.25:sample_rate=48000[a];sine=f=500:d=0.25:sample_rate=48000[b];sine=f=700:d=0.25:sample_rate=48000[c];sine=f=80:d=0.25:sample_rate=48000[d];sine=f=1100:d=0.25:sample_rate=48000[e];sine=f=1300:d=0.25:sample_rate=48000[f];[a][b][c][d][e][f]join=inputs=6:channel_layout=5.1:map=0.0-FL|1.0-FR|2.0-FC|3.0-LFE|4.0-BL|5.0-BR"
ff() { ffmpeg -nostdin -hide_banner -loglevel error "$@"; }
ff -filter_complex "$two" -ar 44100 -c:a libmp3lame -b:a 96k -id3v2_version 3 $T -y "$D/music.mp3"
ff -filter_complex "$two" -c:a aac -b:a 96k $T -y "$D/music.m4a"
ff -filter_complex "$two" -ar 44100 -c:a wmav2 -b:a 96k $T -y "$D/music.wma"
ff -filter_complex "$six" -c:a flac $T -y "$D/music.flac"
ff -filter_complex "$two" -ar 22050 -ac 1 -c:a pcm_s16le $T -y "$D/music.wav"
ff -filter_complex "$two" -ar 32000 -c:a libvorbis $T -y "$D/music.ogg"
for f in "$D"/music.*; do
	ff -i "$f" -ac 2 -ar 48000 -c:a pcm_f32le -f f32le -y "$f.f32"
	python3 -c 'import sys,struct
d=open(sys.argv[1],"rb").read(); n=len(d)//4
v=struct.unpack("<%df"%n,d)
open(sys.argv[2],"wb").write(struct.pack("<%dh"%n,*[max(-32768,min(32767,int(round(x*32768)))) for x in v]))' "$f.f32" "$f.s16"
	rm "$f.f32"
done
V="-f lavfi -i testsrc2=s=64x48:r=25:d=0.5"
A="-f lavfi -i sine=f=440:d=0.5:sample_rate=48000"
ff $V $A -c:v mpeg4 -c:a aac -y "$D/video_mpeg4.mp4"
ff $V $A -c:v libx264 -c:a aac -y "$D/video_avc.mp4"
ff $V $A -c:v libx264 -c:a libmp3lame -y "$D/video_avc.mkv"
ff $V $A -c:v mpeg2video -c:a mp2 -f vob -y "$D/video_mpeg2.mpg"
ff $V $A -c:v mpeg2video -c:a ac3 -f vob -y "$D/video_mpeg2_ac3.vob"
ff $V $A -c:v mpeg1video -c:a mp2 -f mpeg -y "$D/video_mpeg1.mpg"
ff $V $A -c:v mpeg4 -vtag XVID -c:a libmp3lame -y "$D/video_xvid.avi"
ff $V $A -c:v libx264 -c:a ac3 -y "$D/video_avc.avi"
ff $V $A -c:v wmv2 -c:a wmav2 -y "$D/video_wmv2.wmv"
for f in "$D"/music.mp3 "$D"/music.m4a "$D"/music.wma "$D"/music.flac "$D"/music.wav "$D"/music.ogg "$D"/video_*; do
	echo "== $(basename "$f")"
	ffprobe -v error -show_entries format=duration:stream=codec_name,codec_type,sample_rate:format_tags:stream_tags -of default=nw=1 "$f"
done > "$D/expected.txt"
