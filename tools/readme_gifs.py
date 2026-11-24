# SPDX-License-Identifier: GPL-2.0-or-later
"""Cuts the README GIFs (docs/media) out of screen recordings made with tools\\record.ps1.

    python -I tools/readme_gifs.py [--rec build/rec] [--only NAME,...]

Needs ffmpeg and gifski on PATH. Recordings (1920x1080, 30 fps; scenarios in tools/showcase/, run with tools\\record.ps1):
  tbc.mp4     showcase_fight.lua  turn-based fight, PSTF_TBC_AUTOPARTY=1
  blood.mp4   showcase_blood.lua  real-time melee + every blood type ([tbc] start/join_radius 0 while recording)
  dlg.mp4     tests/ingame/dlgroll.lua
  hammer.mp4  showcase_hammer.lua (save 000000990: night off, then night lit; runs 200 frames)
Times are seconds into the recording: re-check them after a new recording (the log line times minus
'autoload: world ready' give them, ~0.3 s later in the video).
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "media")

# name: (inputs [(file, start, duration)], filter, fps, quality)
CLIPS = {
    "tbc_fight.gif": ([("tbc.mp4", 19.4, 12.5)], "scale=720:-2:flags=lanczos", 20, 75),
    "combat.gif": ([("tbc.mp4", 19.6, 9.0)], "crop=1120:630:400:150,scale=640:-2:flags=lanczos", 20, 80),
    "blood.gif": ([("blood.mp4", 21.0, 8.5)], "crop=960:540:800:230,scale=640:-2:flags=lanczos", 20, 80),
    "dialogue_roll.gif": ([("dlg.mp4", 12.3, 10.0)], "crop=960:1080:480:0,scale=480:-2:flags=lanczos", 20, 80),
    "lighting_spell.gif": ([("hammer.mp4", 11.2, 4.4), ("hammer.mp4", 16.0, 4.4)],
                           "[0:v]crop=960:600:220:260,scale=450:-2:flags=lanczos,drawbox=x=8:y=8:w=150:h=34:color=black@0.7:t=fill,drawtext=fontfile='C\\:/Windows/Fonts/georgiab.ttf':text='Vanilla':x=16:y=14:fontsize=22:fontcolor=0xE8C478[a];"
                           "[1:v]crop=960:600:220:260,scale=450:-2:flags=lanczos,drawbox=x=8:y=8:w=150:h=34:color=black@0.7:t=fill,drawtext=fontfile='C\\:/Windows/Fonts/georgiab.ttf':text='PST Forever':x=16:y=14:fontsize=22:fontcolor=0xE8C478[b];"
                           "[a][b]hstack=inputs=2", 20, 80),
}


def make(name, rec):
    inputs, vf, fps, quality = CLIPS[name]
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error"]
    for f, start, dur in inputs:
        cmd += ["-ss", str(start), "-t", str(dur), "-i", os.path.join(rec, f)]
    cmd += ["-filter_complex" if len(inputs) > 1 else "-vf", f"{vf},fps={fps}" if len(inputs) == 1 else f"{vf}[c];[c]fps={fps}",
            "-f", "yuv4mpegpipe", "-"]
    out = os.path.join(OUT, name)
    gif = ["gifski", "--fps", str(fps), "--quality", str(quality), "--lossy-quality", "50",
           "--motion-quality", "50", "-o", out, "-"]
    ff = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    subprocess.run(gif, stdin=ff.stdout, check=True)
    ff.wait()
    print(f"{name}: {os.path.getsize(out) / 1048576:.1f} MB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rec", default=os.path.join(ROOT, "build", "rec"))
    ap.add_argument("--only")
    a = ap.parse_args()
    for tool in ("ffmpeg", "gifski"):
        if not shutil.which(tool):
            sys.exit(f"{tool} not on PATH")
    names = a.only.split(",") if a.only else list(CLIPS)
    for n in names:
        missing = [f for f, _, _ in CLIPS[n][0] if not os.path.exists(os.path.join(a.rec, f))]
        if missing:
            print(f"skip {n}: {', '.join(missing)} missing")
            continue
        make(n, a.rec)
    return 0


if __name__ == "__main__":
    sys.exit(main())
