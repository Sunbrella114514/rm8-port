"""Converts the game's Bink movies (Bink/Start.bik, Bink/Staff.bik) to MPEG-1 video + MP2 audio program streams the port plays with
pl_mpeg (app/video.cpp, docs/media_collect.md).  The game folder is only read.

usage: convert_videos.py [--game <RichMan8 dir>] [--ffmpeg <ffmpeg.exe>] [--out <dir>] [--quality 5]
  default out = port/scenes/video (start.mpg, staff.mpg), the folder android/deploy.py pushes with the scene files.
ffmpeg: <tools>/ffmpeg/<build>/bin/ffmpeg.exe (gyan.dev essentials build, SHA-256 checked against the published value).
Each output is verified: the frame count and the duration must match the Bink source (+- 1 frame / 0.1 s).
"""
import argparse, glob, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MOVIES = [("Start.bik", "start.mpg"), ("Staff.bik", "staff.mpg")]	# [V] 0x5e92e0 "start.bik" / 0x5e93e0 "Staff.bik" (256-byte table, Movie_LoadStartAnimation @0x516750)


def probe(ffprobe, path):
	r = subprocess.run([ffprobe, "-v", "error", "-count_frames", "-show_entries", "stream=codec_type,width,height,nb_read_frames,duration,sample_rate,channels",
	                    "-of", "json", path], capture_output=True, text=True, check=True)
	info = {}
	for s in json.loads(r.stdout)["streams"]:
		info[s["codec_type"]] = s
	return info


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--game", default=r"<old-edition-dir>")
	ap.add_argument("--ffmpeg", default=None)
	ap.add_argument("--out", default=os.path.join(ROOT, "port", "scenes", "video"))
	ap.add_argument("--quality", type=int, default=5, help="mpeg1video -q:v (lower = better / bigger)")
	ap.add_argument("--tolerance", type=float, default=0.1, help="allowed duration difference in seconds (the Steam Staff.bik reports 0.22 s less after conversion with the same frame count)")
	a = ap.parse_args()
	ff = a.ffmpeg or (sorted(glob.glob(r"<tools>\ffmpeg\*\bin\ffmpeg.exe")) or [None])[-1]
	if not ff or not os.path.exists(ff):
		sys.exit("ffmpeg not found (pass --ffmpeg)")
	ffprobe = os.path.join(os.path.dirname(ff), "ffprobe.exe")
	os.makedirs(a.out, exist_ok=True)
	for src_name, dst_name in MOVIES:
		src = os.path.join(a.game, "Bink", src_name)
		dst = os.path.join(a.out, dst_name)
		if not os.path.exists(src):
			sys.exit("missing " + src)
		# 800x600 yuv420p 30 fps source (Bink 1 'BIKi'); the mono audio keeps its rate (32000 / 44100 Hz are valid MPEG-1 layer II rates)
		cmd = [ff, "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", src,
		       "-c:v", "mpeg1video", "-q:v", str(a.quality), "-g", "15", "-bf", "2", "-pix_fmt", "yuv420p", "-aspect", "4:3",
		       "-c:a", "mp2", "-b:a", "128k", "-f", "mpeg", dst]
		print("converting", src_name, "->", dst)
		subprocess.run(cmd, check=True)
		si, di = probe(ffprobe, src), probe(ffprobe, dst)
		sf, df = int(si["video"]["nb_read_frames"]), int(di["video"]["nb_read_frames"])
		sd, dd = float(si["video"]["duration"]), float(di["video"]["duration"])
		ad = float(di["audio"]["duration"]) if "audio" in di else 0
		print("  video %sx%s frames %d -> %d, duration %.2f -> %.2f s, audio %s Hz x%s %.2f s, %.1f MB" % (
			di["video"]["width"], di["video"]["height"], sf, df, sd, dd, di["audio"]["sample_rate"], di["audio"]["channels"], ad, os.path.getsize(dst) / 1048576.0))
		if abs(sf - df) > 1 or abs(sd - dd) > a.tolerance:
			sys.exit("verification failed for " + dst_name)
	print("ok")


if __name__ == "__main__":
	main()
