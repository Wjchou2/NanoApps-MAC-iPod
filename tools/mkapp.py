#!/usr/bin/env python3
"""Build homebrew .app bundles + packs — see sdk/hb_app_bundle.h.

A bundle just PACKAGES pre-built section bytes (icon bitmap, label Str blob,
screen SilverDB, code .hbapp) plus a manifest; the runtime registrar unpacks and
drives the existing mechanisms.

  mkapp.py bundle <out.app> --id N --kind K --sbid NAME --label-id L \
      [--screen-id S --layout-id Y] [--os-handler H] \
      [--label-tag T --label FILE] [--screen-tag T --screen FILE] \
      [--icon FILE] [--code FILE]

  mkapp.py pack <out.pack> <a.app> <b.app> ...
"""
import sys, struct, argparse

BUNDLE_MAGIC = 0x50414248   # 'HBAP'
PACK_MAGIC   = 0x4b504248   # 'HBPK'
VERSION = 1
SEC = {"icon": 1, "label": 2, "screen": 3, "code": 4}

# hb_app_bundle_t: magic,ver(H),kind(H),id,label_id,screen_id,layout_id,
#                  os_handler,label_tag,screen_tag,sbid_off,name_off,bundle_len,
#                  section_count
HDR_FMT = "<IHHIIIIIIIIIII"
HDR_SIZE = struct.calcsize(HDR_FMT)     # 52
SEC_FMT = "<III"                        # kind, off, len
SEC_SIZE = struct.calcsize(SEC_FMT)     # 12


def _align4(n):
    return (n + 3) & ~3


def make_label_db(label_id, text):
    """A minimal SilverDB with one 'Str ' resource (id, NUL-terminated ASCII) —
    the home-label string the OS resolves by id. Format per silverdb_format.md:
    header(version,header_len,section_count) | section_meta(type,count,ids_inc,
    res_meta_off) | resource_meta(id,data_off,size) | contents."""
    s = text.encode("ascii") + b"\0"
    res_meta_off = 0xc + 16              # after header + 1 section meta
    header_len = res_meta_off + 12       # + 1 resource meta -> contents start
    out = bytearray()
    out += struct.pack("<III", 3, header_len, 1)
    out += struct.pack("<IIII", 0x53747220, 1, 1, res_meta_off)   # 'Str '
    out += struct.pack("<III", label_id, 0, len(s))
    out += s
    return bytes(out)


def build_bundle(a):
    sections = []
    if a.icon:
        sections.append((SEC["icon"], open(a.icon, "rb").read()))
    if a.label_text is not None:
        sections.append((SEC["label"], make_label_db(a.label_id, a.label_text)))
    elif a.label:
        sections.append((SEC["label"], open(a.label, "rb").read()))
    for name in ("screen", "code"):
        path = getattr(a, name)
        if path:
            sections.append((SEC[name], open(path, "rb").read()))
    sbid = a.sbid.encode("ascii") + b"\0"
    # CFBundleName (ASCII) — the human file name for the executable/icon on disk.
    name = (a.name if a.name is not None else a.sbid).encode("ascii") + b"\0"

    sec_table_off = HDR_SIZE
    sbid_off = sec_table_off + SEC_SIZE * len(sections)
    name_off = sbid_off + len(sbid)
    cur = _align4(name_off + len(name))

    descs, data = [], bytearray()
    for kind, payload in sections:
        descs.append((kind, cur, len(payload)))
        data += payload
        pad = _align4(len(payload)) - len(payload)
        data += b"\0" * pad
        cur += len(payload) + pad

    out = bytearray(cur)
    struct.pack_into(HDR_FMT, out, 0,
                     BUNDLE_MAGIC, VERSION, a.kind, a.id, a.label_id,
                     a.screen_id, a.layout_id, a.os_handler,
                     a.label_tag, a.screen_tag, sbid_off, name_off, cur,
                     len(sections))
    for i, (kind, off, ln) in enumerate(descs):
        struct.pack_into(SEC_FMT, out, sec_table_off + i * SEC_SIZE, kind, off, ln)
    out[sbid_off:sbid_off + len(sbid)] = sbid
    out[name_off:name_off + len(name)] = name
    out[_align4(name_off + len(name)):] = data

    open(a.out, "wb").write(out)
    print(f"wrote {a.out}: id=0x{a.id:08x} kind={a.kind} sbid={a.sbid} "
          f"name={name[:-1].decode('ascii')} "
          f"sections={[k for k,_,_ in descs]} bytes={cur}")


def build_pack(out_path, app_paths):
    bundles = [open(p, "rb").read() for p in app_paths]
    n = len(bundles)
    head = struct.calcsize("<III") + 4 * n          # pack header + offset table
    head = _align4(head)
    offs, blob, cur = [], bytearray(), head
    for b in bundles:
        offs.append(cur)
        blob += b
        pad = _align4(len(b)) - len(b)
        blob += b"\0" * pad
        cur += len(b) + pad

    out = bytearray(head) + blob
    struct.pack_into("<III", out, 0, PACK_MAGIC, VERSION, n)
    for i, o in enumerate(offs):
        struct.pack_into("<I", out, 12 + 4 * i, o)
    open(out_path, "wb").write(out)
    print(f"wrote {out_path}: {n} apps, {len(out)} bytes")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "pack":
        build_pack(sys.argv[2], sys.argv[3:])
        return
    if cmd != "bundle":
        sys.exit(__doc__)
    p = argparse.ArgumentParser()
    p.add_argument("out")
    p.add_argument("--id", type=lambda x: int(x, 0), required=True)
    p.add_argument("--kind", type=int, default=0)
    p.add_argument("--sbid", required=True)
    p.add_argument("--name", default=None,
                   help="CFBundleName — disk file name for the exe + icon")
    p.add_argument("--label-id", dest="label_id", type=lambda x: int(x, 0), default=0)
    p.add_argument("--screen-id", dest="screen_id", type=lambda x: int(x, 0), default=0)
    p.add_argument("--layout-id", dest="layout_id", type=lambda x: int(x, 0), default=0)
    p.add_argument("--os-handler", dest="os_handler", type=lambda x: int(x, 0), default=0)
    p.add_argument("--label-tag", dest="label_tag", type=lambda x: int(x, 0), default=0)
    p.add_argument("--screen-tag", dest="screen_tag", type=lambda x: int(x, 0), default=0)
    p.add_argument("--icon", default=None)
    p.add_argument("--label", default=None)
    p.add_argument("--label-text", dest="label_text", default=None,
                   help="build a Str label DB inline from this text")
    p.add_argument("--screen", default=None)
    p.add_argument("--code", default=None)
    build_bundle(p.parse_args(sys.argv[2:]))


if __name__ == "__main__":
    main()
