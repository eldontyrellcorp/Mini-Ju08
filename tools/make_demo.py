#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The demonstration video (Full HD, 30 fps): host/ju_demo.c renders the sounds and a Berlin-School piece with the firmware's engine and panel
UI; this puts the 240 x 240 screen, the captions and a scope into 1920 x 1080 and encodes it with the audio.
  python3 tools/make_demo.py [out.mp4]        (needs gcc, ffmpeg, Pillow, numpy)"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "Mini-Ju08-demo.mp4")
W, H, FPS, FS = 1920, 1080, 30, 44100
BG0, BG1 = (14, 14, 18), (30, 24, 20)
CREAM, DIM, ORANGE, RED = (235, 230, 215), (150, 146, 136), (240, 124, 28), (226, 44, 38)


def font(bold, size):
    p = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else "")
    return ImageFont.truetype(p, size)


def run(tmp):
    src = os.path.join(ROOT, "host")
    exe = os.path.join(tmp, "ju_demo")
    fs = os.path.join(ROOT, "firmware", "src")
    subprocess.check_call(["gcc", "-std=gnu11", "-O2", "-w", "-I" + fs, os.path.join(src, "ju_demo.c"), os.path.join(fs, "ju_dsp.c"),
                           os.path.join(fs, "ju_fxdef.c"), os.path.join(fs, "ju_pinfo.c"), "-o", exe])
    with open(os.path.join(tmp, "frames.rgb"), "wb") as f:
        subprocess.check_call([exe, os.path.join(tmp, "a.raw"), os.path.join(tmp, "caps.txt")], stdout=f)


def caps(tmp):
    out = []
    for line in open(os.path.join(tmp, "caps.txt")):
        t, a, b = line.rstrip("\n").split("\t")
        out.append((float(t), a, b))
    return out


def background():
    g = np.zeros((H, W, 3), dtype=np.uint8)
    t = np.linspace(0, 1, H)[:, None, None]
    g[:] = (np.array(BG0) * (1 - t) + np.array(BG1) * t).astype(np.uint8)
    im = Image.fromarray(g)
    d = ImageDraw.Draw(im)
    d.rectangle((52, 52, 52 + 976, 52 + 976), outline=(70, 70, 76), width=3)
    d.text((1100, 70), "MINI-JU08", font=font(True, 84), fill=CREAM)
    d.text((1104, 170), "A Jupiter-8 synthesizer for the M-VAVE FM-1", font=font(False, 30), fill=DIM)
    d.text((1104, 214), "firmware 1.00  -  rendered with the firmware's own engine and panel", font=font(False, 22), fill=DIM)
    d.rectangle((1100, 262, 1860, 266), fill=ORANGE)
    d.text((1100, 820), "SCOPE", font=font(True, 20), fill=DIM)
    d.rectangle((1100, 850, 1860, 1030), outline=(70, 70, 76), width=2)
    return im


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        run(tmp)
        cp = caps(tmp)
        audio = np.fromfile(os.path.join(tmp, "a.raw"), dtype="<i2").reshape(-1, 2).astype(np.float32).mean(1) / 32768
        total = len(audio) / FS
        nfr = os.path.getsize(os.path.join(tmp, "frames.rgb")) // (240 * 240 * 3)
        bg = background()
        fb, fn, fs_ = font(True, 56), font(False, 30), font(True, 20)
        enc = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", "-",
                                "-f", "s16le", "-ar", str(FS), "-ac", "2", "-i", os.path.join(tmp, "a.raw"),
                                "-af", "loudnorm=I=-16:TP=-1.5,afade=t=in:d=0.3", "-c:v", "libx264", "-preset", "medium", "-crf", "19", "-pix_fmt", "yuv420p",
                                "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", OUT], stdin=subprocess.PIPE)
        fr = open(os.path.join(tmp, "frames.rgb"), "rb")
        for k in range(nfr):
            t = k / FPS
            raw = np.frombuffer(fr.read(240 * 240 * 3), dtype=np.uint8).reshape(240, 240, 3).copy()
            raw[9:21, 172:240] = (10, 10, 12)                      # the CPU figure is not shown: the video is rendered on a PC
            im = bg.copy()
            im.paste(Image.fromarray(raw).resize((960, 960), Image.NEAREST), (60, 60))
            d = ImageDraw.Draw(im)
            cur = max((c for c in cp if c[0] <= t), key=lambda c: c[0], default=cp[0])
            title, sub = cur[1], cur[2]
            d.text((1100, 300), title, font=fb, fill=CREAM)
            # long captions wrap
            words, line, y = sub.split(), "", 392
            for w in words:
                if d.textlength(line + " " + w, font=fn) > 760:
                    d.text((1100, y), line.strip(), font=fn, fill=ORANGE); y += 42; line = ""
                line += " " + w
            d.text((1100, y), line.strip(), font=fn, fill=ORANGE)
            # the timeline: the chapters and the playhead
            x0, x1, ty = 1100, 1860, 700
            d.rectangle((x0, ty, x1, ty + 8), fill=(60, 60, 66))
            for c in cp:
                d.rectangle((x0 + int((x1 - x0) * c[0] / total), ty - 6, x0 + int((x1 - x0) * c[0] / total) + 2, ty + 14), fill=DIM)
            d.rectangle((x0, ty, x0 + int((x1 - x0) * t / total), ty + 8), fill=ORANGE)
            d.text((x0, ty + 26), "%d:%02d / %d:%02d" % (t // 60, t % 60, total // 60, total % 60), font=fs_, fill=DIM)
            # the scope: the last 1500 samples of the mix
            e = int(t * FS)
            seg = audio[max(0, e - 1500):e]
            if len(seg) > 10:
                n = 760
                idx = np.linspace(0, len(seg) - 1, n).astype(int)
                ys = 940 - (seg[idx] / 0.6 * 80).clip(-84, 84)
                d.line([(1100 + i, float(ys[i])) for i in range(n)], fill=(240, 124, 28), width=2)
            enc.stdin.write(im.tobytes())
        enc.stdin.close()
        enc.wait()
    print(OUT, os.path.getsize(OUT) // 1024, "KiB")


main()
