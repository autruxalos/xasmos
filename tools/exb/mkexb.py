import sys, struct, zlib
elf = open(sys.argv[1], "rb").read()
ent = struct.unpack_from("<Q", elf, 24)[0]
pho = struct.unpack_from("<Q", elf, 32)[0]
phes, phn = struct.unpack_from("<HH", elf, 54)
segs = []
for i in range(phn):
    pt, pf, po, pv, pp, pfs, pms, pa = struct.unpack_from("<IIQQQQQQ", elf, pho + i * phes)
    if pt == 1 and pms > 0:
        fl = (1 if (pf & 4) else 0) | (2 if (pf & 2) else 0) | (4 if (pf & 1) else 0)
        segs.append((pv, pms, elf[po:po+pfs], fl))
cur = 64 + len(segs) * 32
recs = bytearray()
pay = bytearray()
for pv, pms, d, fl in segs:
    fsz = len(d)
    recs += struct.pack("<QQIIII", pv, pms, cur if fsz > 0 else 0, fsz, fl, 0)
    pay += d
    cur += fsz
hdr = struct.pack("<4sBBBBIIQIIIIIIBBBB12s", bytes([0x7F,0x45,0x58,0x42]), 1, 1, 64, 0, 0, 1, ent, cur, 0, 64, len(segs), 0, 0, 3, 0, 0, 0, bytes(12))
img = bytearray(hdr + recs + pay)
struct.pack_into("<I", img, 28, zlib.crc32(bytes(img)) & 0xFFFFFFFF)
open(sys.argv[1], "wb").write(img)
sym = sys.argv[4][1]
lines = [f"static const unsigned char {sym}[] = {{"]
for i in range(0, len(img), 12):
    lines.append("  " + ", ".join(f"0x{b:02X}" for b in img[i:i+12]) + ",")
lines.append("};")
lines.append(f"static const unsigned long {sym}_len = {len(img)};")
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
