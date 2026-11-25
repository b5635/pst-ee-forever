# SPDX-License-Identifier: GPL-2.0-or-later
"""Cuts the YouTube Short (1080x1920, 60 fps, H.264 + AAC) from the clips of tools\\showcase\\record_short.ps1.

    python -I tools/showcase/make_short.py [--rec build/rec/short] [--out short.mp4] [--preview]

Layout: the clip's square crop (centred on the action) in the middle, the same frame blurred and darkened behind
it, a gold title in the band above, a line under it. Clips cross-fade; the game sound fades with them, the music
(sounds/... lofi.mp3) runs under everything and dips while someone speaks. Loudness -14 LUFS (YouTube).
Each SEGMENTS row: clip, start s, length s, focus x/y (0..1 of the 16:9 frame), zoom (1 = the full frame
height), title, subtitle, game volume, music volume, transition into the next row (xfade name or 'cut').
Times come from build/rec/short/<clip>.events.txt (log notes at their second in the video).
--preview renders at half size, fast (look at the cut before the full render).
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MUSIC = os.path.join(ROOT, "sounds", "planescape main title ambient guitar lofi.mp3")
MUSIC_START = 0.0
FONT = "C\\:/Windows/Fonts/georgiab.ttf"
FONT2 = "C\\:/Windows/Fonts/georgia.ttf"
GOLD = "0xE8C478"
W, H, FPS = 1080, 1920, 60
XF = 0.4  # cross-fade length

#            clip     start  len   fx    fy    zoom  title                     subtitle                         game music next
SEGMENTS = [
    ("hammer",   6.0,  3.0, 0.50, 0.45, 1.30, "PLANESCAPE: TORMENT",     "like you've never seen it",       0.8, 0.8, "fade"),
    ("hammer",   3.0,  2.6, 0.50, 0.45, 1.30, "BEFORE",                  "the original lighting",           0.8, 0.8, "cut"),
    ("hammer",   0.9,  2.2, 0.50, 0.45, 1.30, "AFTER",                   "dynamic lighting",                0.8, 0.8, "smoothleft"),
    ("spells",  14.0,  5.0, 0.48, 0.45, 1.10, "TURN-BASED COMBAT",       "initiative, turns, the real rules", 0.9, 0.6, "fade"),
    ("spells",  28.5,  5.0, 0.47, 0.55, 1.25, "AX OF TORMENT",           "cast it on your turn",            0.9, 0.6, "fade"),
    ("spells",  37.6,  4.0, 0.46, 0.45, 1.25, "SPIRITUAL HAMMER",        "Fall-from-Grace joins in",        0.9, 0.6, "smoothleft"),
    ("moves",   20.5,  4.0, 0.47, 0.40, 1.30, "TRIP",                    "knock them off their feet",       0.9, 0.6, "fade"),
    ("moves",   29.0,  3.5, 0.42, 0.45, 1.20, "ATTACKS OF OPPORTUNITY",  "walk away, get punished",         0.9, 0.6, "fade"),
    ("voice",   15.4,  6.0, 0.50, 0.45, 1.20, "VOICED EXAMINE TEXT",     "click it, hear it",               1.0, 0.2, "fade"),
    ("voice",   24.8,  7.5, 0.50, 0.62, 1.00, "MORTE SPEAKS",            "\"How did you die, Morte?\"",     1.0, 0.2, "fade"),
    ("voice",   64.3,  6.0, 0.50, 0.62, 1.00, "AND SO DOES ANNAH",       "voiced companion dialogue",       1.0, 0.2, "fade"),
    ("dice",    13.6,  7.0, 0.50, 0.48, 1.00, "ROLL FOR IT",             "ability checks in dialogue",      0.9, 0.5, "fade"),
    ("party8",  14.5,  6.0, 0.00, 0.50, 1.00, "8-MEMBER PARTY",          "bring everyone",                  0.8, 0.7, "fade"),
]
NAME = "Planescape: Torment EE Forever Beta"
# a line under the footage for every segment of a clip
NOTES = {"voice": "Voices not included: you generate them yourself"}
OUTRO = (("PLANESCAPE: TORMENT", "EE FOREVER BETA"), "a free mod for Planescape: Torment EE", "github.com/b5635/pst-ee-forever", 4.0)


def esc(s):
    return s.replace("\\", "\\\\").replace(":", "\\:").replace("'", "’").replace(",", "\\,")


def fit(txt, size, width, k):
    # largest size <= size whose text fits 90 % of the width (k = average glyph width / font size)
    return int(min(size, width * 0.9 / max(1, len(txt) * k)))


def text(label, txt, size, y, font, start=0.15, color=GOLD):
    # pops in: fades over 0.25 s and slides 30 px up
    a = f"if(lt(t\\,{start})\\,0\\,min(1\\,(t-{start})/0.25))"
    yy = f"{y}+30*(1-min(1\\,max(0\\,(t-{start})/0.25)))"
    return (f"drawtext=fontfile='{font}':text='{esc(txt)}':fontsize={size}:fontcolor={color}:alpha='{a}'"
            f":borderw=4:bordercolor=black@0.85:shadowx=0:shadowy=4:shadowcolor=black@0.6"
            f":x=(w-text_w)/2:y='{yy}'")


def build(rec, out, preview):
    scale = 0.5 if preview else 1.0
    w, h = int(W * scale) // 2 * 2, int(H * scale) // 2 * 2
    sq = w  # the game footage: full width, 4:5
    fh = int(w * 5 / 4) // 2 * 2
    sq_y = (h - fh) // 2 + int(40 * scale)
    inputs, fc, vl, al, lens, mvol = [], [], [], [], [], []
    for i, (clip, ss, dur, fx, fy, zoom, title, sub, gv, mv, _) in enumerate(SEGMENTS):
        inputs += ["-ss", f"{ss}", "-t", f"{dur + XF}", "-i", os.path.join(rec, clip + ".mp4")]
        ch = f"ih/{zoom}"           # crop height, source pixels
        cw = f"ih/{zoom}*4/5"       # 4:5
        crop = f"crop={cw}:{ch}:'max(0\\,min(iw-{cw}\\,iw*{fx}-{cw}/2))':'max(0\\,min(ih-{ch}\\,ih*{fy}-{ch}/2))'"
        fc.append(f"[{i}:v]fps={FPS},setpts=PTS-STARTPTS,split[a{i}][b{i}]")
        fc.append(f"[a{i}]scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h},boxblur=20:2,eq=brightness=-0.18[bg{i}]")
        fc.append(f"[b{i}]{crop},scale={sq}:{fh}:flags=lanczos[fg{i}]")
        fc.append(f"[bg{i}][fg{i}]overlay=0:{sq_y},"
                  f"drawbox=x=0:y={sq_y - 3}:w=iw:h=3:color={GOLD}@0.9:t=fill,drawbox=x=0:y={sq_y + fh}:w=iw:h=3:color={GOLD}@0.9:t=fill,"
                  + text("t", title, fit(title, int(96 * scale), w, 0.74), int(sq_y - 215 * scale), FONT) + ","
                  + text("s", sub, fit(sub, int(52 * scale), w, 0.5), int(sq_y - 100 * scale), FONT2, 0.35, "white") + ","
                  + (text("n", NOTES[clip], fit(NOTES[clip], int(40 * scale), w, 0.5), int(sq_y + fh + 30 * scale), FONT2, 0.5, "white") + ","
                     if clip in NOTES else "")
                  + text("m", NAME, fit(NAME, int(40 * scale), w, 0.62), int(sq_y + fh + 110 * scale), FONT, 0.0)
                  + f",format=yuv420p,settb=1/{FPS}[v{i}]")
        fc.append(f"[{i}:a]asetpts=PTS-STARTPTS,aresample=48000,volume={gv}[ga{i}]")
        vl.append(f"v{i}")
        al.append(f"ga{i}")
        lens.append(dur + XF)
        mvol.append(mv)
    # outro card: the last clip's frame, blurred, with the name and the link
    n = len(SEGMENTS)
    title, sub, link, odur = OUTRO
    last = SEGMENTS[-1]
    inputs += ["-ss", f"{last[1] + last[2] - 0.1}", "-t", "0.1", "-i", os.path.join(rec, last[0] + ".mp4")]
    fc.append(f"[{n}:v]trim=end_frame=1,loop=loop={int((odur + XF) * FPS)}:size=1:start=0,fps={FPS},setpts=N/{FPS}/TB,"
              f"scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h},boxblur=24:2,eq=brightness=-0.3,"
              + text("t", title[0], fit(title[0], int(110 * scale), w, 0.74), int(h * 0.36), FONT, 0.2) + ","
              + text("u", title[1], fit(title[1], int(110 * scale), w, 0.74), int(h * 0.36 + 130 * scale), FONT, 0.4) + ","
              + text("s", sub, fit(sub, int(50 * scale), w, 0.5), int(h * 0.36 + 290 * scale), FONT2, 0.8, "white") + ","
              + text("l", link, fit(link, int(46 * scale), w, 0.5), int(h * 0.36 + 370 * scale), FONT2, 1.1)
              + f",format=yuv420p,settb=1/{FPS}[v{n}]")
    fc.append(f"anullsrc=r=48000:cl=stereo,atrim=duration={odur + XF}[ga{n}]")
    vl.append(f"v{n}")
    al.append(f"ga{n}")
    lens.append(odur + XF)
    mvol.append(0.9)
    # chain: each input is its length + XF; a join overlaps the next one by d ('cut' = one frame),
    # so the next starts at (output so far - d). A cut keeps the extra XF of footage at its end.
    cur_v, cur_a, length = vl[0], al[0], lens[0]
    starts = [0.0]
    for i in range(1, len(vl)):
        tr = SEGMENTS[i - 1][10] if i - 1 < n else "fade"
        d = 1.0 / FPS if tr == "cut" else XF
        tr = "fade" if tr == "cut" else tr
        fc.append(f"[{cur_v}][{vl[i]}]xfade=transition={tr}:duration={d:.4f}:offset={length - d:.4f}[xv{i}]")
        fc.append(f"[{cur_a}][{al[i]}]acrossfade=d={d:.4f}:c1=tri:c2=tri[xa{i}]")
        starts.append(length - d)
        length += lens[i] - d
        cur_v, cur_a = f"xv{i}", f"xa{i}"
    total = length
    # music: from MUSIC_START, its volume per segment (from the middle of each join), fades in/out
    vol = f"{mvol[-1]}"
    for i in range(len(starts) - 1, 0, -1):
        vol = f"if(lt(t\\,{starts[i] + XF / 2:.3f})\\,{mvol[i - 1]}\\,{vol})"
    inputs += ["-ss", f"{MUSIC_START}", "-i", MUSIC]
    mi = n + 1
    fc.append(f"[{mi}:a]aresample=48000,atrim=duration={total:.2f},volume='{vol}':eval=frame,"
              f"afade=t=in:d=0.8,afade=t=out:st={total - 2.0:.2f}:d=2.0,lowpass=f=16000[mus]")
    fc.append(f"[{cur_a}][mus]amix=inputs=2:duration=longest:normalize=0,loudnorm=I=-14:TP=-1.5:LRA=11,"
              f"volume=2.5dB,alimiter=limit=0.7:level=0[aout]")
    fc.append(f"[{cur_v}]fade=t=in:d=0.3[vout]")
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-stats", "-y"] + inputs + [
        "-filter_complex", ";".join(fc), "-map", "[vout]", "-map", "[aout]",
        "-c:v", "libx264", "-preset", "ultrafast" if preview else "slow", "-crf", "28" if preview else "17",
        "-profile:v", "high", "-pix_fmt", "yuv420p", "-r", str(FPS),
        "-c:a", "aac", "-b:a", "192k", "-ar", "48000", "-movflags", "+faststart", "-t", f"{total:.2f}", out]
    print(f"short: {len(SEGMENTS)} clips + outro, {total:.1f} s -> {out}")
    return subprocess.run(cmd).returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rec", default=os.path.join(ROOT, "build", "rec", "short"))
    ap.add_argument("--out", default=None)
    ap.add_argument("--preview", action="store_true")
    a = ap.parse_args()
    missing = sorted({s[0] for s in SEGMENTS if not os.path.exists(os.path.join(a.rec, s[0] + ".mp4"))})
    if missing:
        sys.exit("short: missing clips " + ", ".join(missing) + " (tools\\showcase\\record_short.ps1 -Only ...)")
    out = a.out or os.path.join(a.rec, "short_preview.mp4" if a.preview else "short.mp4")
    sys.exit(build(a.rec, out, a.preview))


if __name__ == "__main__":
    main()
