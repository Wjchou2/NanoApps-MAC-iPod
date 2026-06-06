#!/usr/bin/env python3
"""Pack a relocatable homebrew app ELF into a loadable .hbapp blob.

The app is built non-PIC, linked at base 0, with -Wl,-q (keep relocations) and
-e hb_blob_main. This tool flattens it and extracts the ABS32 relocations so the
runtime loader (sdk/hb_reloc.c) can drop it at any OS-heap address and rebase.

  ELF in  -> .hbapp out:
    header: magic 'HRL1', entry_off, image_size, span_size, reloc_count, align
    image:  image_size bytes (text+rodata+data laid out from VMA 0, gaps zeroed)
    relocs: reloc_count * u32  (VMA offsets to add the load base to)

Only R_ARM_ABS32 relocations against allocated sections are emitted; PC-relative
branch relocs (THM_CALL/JUMP24) are intra-blob and need no fixup. The blob must
have NO undefined symbols (fully self-contained — fold in libgcc).

Usage: mkrelocapp.py <in.elf> <out.hbapp> [entry_symbol]
"""
import sys, struct
try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.constants import SH_FLAGS
    from elftools.elf.sections import SymbolTableSection
    from elftools.elf.relocation import RelocationSection
except ModuleNotFoundError:
    sys.exit("error: pyelftools is required to pack .hbapp blobs.\n"
             "       install it with:  python3 -m pip install --user pyelftools\n"
             "       (or just run ./start, which installs build dependencies for you)")

R_ARM_ABS32 = 2
MAGIC = 0x31_4C_52_48  # 'HRL1' little-endian


def main(in_path, out_path, entry_sym="hb_blob_main"):
    with open(in_path, "rb") as f:
        elf = ELFFile(f)

        # 1) no undefined (external) symbols allowed — blob must be self-contained
        undef = []
        entry_off = None
        for sec in elf.iter_sections():
            if not isinstance(sec, SymbolTableSection):
                continue
            for sym in sec.iter_symbols():
                if sym.name == entry_sym and sym["st_info"]["type"] == "STT_FUNC":
                    entry_off = sym["st_value"] & ~1  # strip thumb bit
                if sym["st_shndx"] == "SHN_UNDEF" and sym.name:
                    undef.append(sym.name)
        if undef:
            sys.exit(f"error: blob has undefined (external) symbols: {sorted(set(undef))[:8]}\n"
                     f"       link it self-contained (fold in libgcc).")
        if entry_off is None:
            sys.exit(f"error: entry symbol '{entry_sym}' not found")

        # 2) image span: lowest..highest VMA across ALLOC sections
        alloc = [s for s in elf.iter_sections()
                 if (s["sh_flags"] & SH_FLAGS.SHF_ALLOC) and s["sh_size"] > 0]
        if not alloc:
            sys.exit("error: no allocatable sections")
        lo = min(s["sh_addr"] for s in alloc)
        if lo != 0:
            sys.exit(f"error: lowest section VMA is {lo:#x}, expected 0 (link -Ttext 0)")
        span = max(s["sh_addr"] + s["sh_size"] for s in alloc)
        # PROGBITS image (NOBITS/.bss excluded — zeroed at load)
        progbits_end = max((s["sh_addr"] + s["sh_size"] for s in alloc
                            if s["sh_type"] == "SHT_PROGBITS"), default=0)
        image = bytearray(progbits_end)
        for s in alloc:
            if s["sh_type"] == "SHT_PROGBITS":
                a = s["sh_addr"]
                image[a:a + s["sh_size"]] = s.data()

        # 3) ABS32 relocations against allocatable sections
        reloc_offs = []
        for sec in elf.iter_sections():
            if not isinstance(sec, RelocationSection):
                continue
            target = elf.get_section(sec["sh_info"])
            if target is None or not (target["sh_flags"] & SH_FLAGS.SHF_ALLOC):
                continue  # skip .rel.debug_* etc.
            for r in sec.iter_relocations():
                if r["r_info_type"] == R_ARM_ABS32:
                    reloc_offs.append(r["r_offset"])
        reloc_offs.sort()
        for off in reloc_offs:
            if off + 4 > progbits_end:
                sys.exit(f"error: reloc offset {off:#x} outside image")

    align = 16
    hdr = struct.pack("<IIIIII", MAGIC, entry_off, progbits_end, span,
                      len(reloc_offs), align)
    body = b"".join(struct.pack("<I", o) for o in reloc_offs)
    with open(out_path, "wb") as out:
        out.write(hdr)
        out.write(image)
        out.write(body)

    print(f"wrote {out_path}: entry@{entry_off:#x} image={progbits_end} "
          f"span={span} bss={span - progbits_end} relocs={len(reloc_offs)}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "hb_blob_main")
