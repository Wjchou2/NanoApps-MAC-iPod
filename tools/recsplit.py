#!/usr/bin/env python3
"""recsplit.py — expand a packed screen recording (.rec) into per-frame images.

A .rec file (written by sdk/hb_record.c) is a small header + a delta-compressed
frame stream:

  header (32 bytes): "HBREC1\\0\\0" | u32 w | u32 h | u32 frames | u32 nbytes |
                     u32 bpp (stored bytes/pixel: 4 = XRGB8888, 0 = legacy RGB565) | 4 reserved
  then per frame:    u32 oplen | ops
  ops are u16 tokens over the frame's pixels (row-major, w*h):
     bit15 = 1 -> COPY: low 15 bits = count, followed by `count` literals (bpp bytes each)
     bit15 = 0 -> SKIP: low 15 bits = count unchanged pixels (kept from prev frame)
  The keyframe (frame 0) diffs against an all-zero frame, so it stores everything.

Usage:
  tools/recsplit.py rec0000.rec [outdir]      # -> outdir/frameNNNN.png (+ recording.gif)
"""
import sys, struct, os

def xrgb_to_rgb(buf):
    """recon[] holds 0xRRGGBB ints (both formats normalised on read) -> packed RGB."""
    out = bytearray(len(buf) * 3)
    for i, c in enumerate(buf):
        out[i*3]   = (c >> 16) & 0xff
        out[i*3+1] = (c >> 8) & 0xff
        out[i*3+2] = c & 0xff
    return bytes(out)

def main(argv):
    if len(argv) < 2:
        print(__doc__); return 1
    path = argv[1]
    outdir = argv[2] if len(argv) > 2 else os.path.splitext(path)[0] + "_frames"
    data = open(path, "rb").read()
    if data[:6] != b"HBREC1":
        print("not a HBREC1 file"); return 1
    w, h, frames, nbytes, bpp = struct.unpack_from("<IIIII", data, 8)
    if bpp not in (2, 4):
        bpp = 2                            # legacy files (pre-XRGB) stored RGB565
    # streamed .rec files leave frames=0 (unknown up front); decode to EOF instead.
    print(f"{w}x{h}, {frames if frames else '?'} frames, {bpp*8}bpp, {len(data)} bytes")
    os.makedirs(outdir, exist_ok=True)

    try:
        from PIL import Image
        have_pil = True
    except ImportError:
        have_pil = False
        print("(Pillow not found -> writing .ppm; `pip install pillow` for png/gif)")

    def lit565(off):                      # RGB565 u16 -> 0xRRGGBB
        (c,) = struct.unpack_from("<H", data, off)
        r = (c >> 11) & 0x1f; g = (c >> 5) & 0x3f; b = c & 0x1f
        return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2))
    def litxrgb(off):                     # XRGB8888 u32 -> 0xRRGGBB (alpha already 0)
        return struct.unpack_from("<I", data, off)[0] & 0xffffff
    lit = litxrgb if bpp == 4 else lit565

    n = w * h
    recon = [0] * n                       # 0xRRGGBB per pixel, persists across frames
    p = 32                                # past the header
    imgs = []
    f = -1
    while p + 4 <= len(data):             # decode frames until EOF
        f += 1
        (oplen,) = struct.unpack_from("<I", data, p); p += 4
        if oplen == 0 or p + oplen > len(data): break
        end = p + oplen
        px = 0
        while p < end and px < n:
            (tok,) = struct.unpack_from("<H", data, p); p += 2
            cnt = tok & 0x7fff
            if tok & 0x8000:              # COPY literals
                for _ in range(cnt):
                    if px >= n: break
                    recon[px] = lit(p); p += bpp; px += 1
            else:                         # SKIP -> keep recon[]
                px += cnt
        p = end
        rgb = xrgb_to_rgb(recon)
        if have_pil:
            im = Image.frombytes("RGB", (w, h), rgb)
            im.save(os.path.join(outdir, f"frame{f:04d}.png"))
            imgs.append(im)
        else:
            with open(os.path.join(outdir, f"frame{f:04d}.ppm"), "wb") as fp:
                fp.write(f"P6\n{w} {h}\n255\n".encode()); fp.write(rgb)
    print(f"wrote {f + 1} frames to {outdir}/")
    if have_pil and imgs:
        gif = outdir + "/recording.gif"
        imgs[0].save(gif, save_all=True, append_images=imgs[1:], duration=50, loop=0)
        print(f"wrote {gif}")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
