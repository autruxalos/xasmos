/* exbpack.rexx -- builds and converts .exb v1 images (REXX port of exbpack.py).
 * Acceptance test: output must be byte-identical to the Python tool.
 * NOTE: use PARSE ARG, never ARG (ARG uppercases its input in REXX). */
numeric digits 20
call init_table
parse arg cmd rest
select
  when cmd = 'elf2exb' then call do_elf2exb rest
  when cmd = 'base'    then call do_base rest
  otherwise
    say 'usage: regina ./tools/exb/exbpack.rexx elf2exb IN OUT [--stack N] [--heap-frames N] [--tokens N]'
    say '       regina ./tools/exb/exbpack.rexx base OUT'
    exit 2
end
exit 0

/* ---- little-endian packing; d2c is big-endian, so reverse ---------------- */
p16: procedure; parse arg v; return reverse(d2c(v, 2))
p32: procedure; parse arg v; return reverse(d2c(v, 4))
p64: procedure; parse arg v; return reverse(d2c(v, 8))
le:  procedure; parse arg s, off, n; return c2d(reverse(substr(s, off + 1, n)))
roundup16: procedure; parse arg x; n = x + 15; return n - (n // 16)
zeros: procedure; parse arg n; return copies('00'x, n)

/* ---- CRC-32, nibble table (same algorithm as exb.c) ----------------------- */
init_table:
  t.0  = x2d('00000000'); t.1  = x2d('1DB71064'); t.2  = x2d('3B6E20C8'); t.3  = x2d('26D930AC')
  t.4  = x2d('76DC4190'); t.5  = x2d('6B6B51F4'); t.6  = x2d('4DB26158'); t.7  = x2d('5005713C')
  t.8  = x2d('EDB88320'); t.9  = x2d('F00F9344'); t.10 = x2d('D6D6A3E8'); t.11 = x2d('CB61B38C')
  t.12 = x2d('9B64C2B0'); t.13 = x2d('86D3D2D4'); t.14 = x2d('A00AE278'); t.15 = x2d('BDBDF21C')
  return

u32xor: procedure
  parse arg a, b
  return c2d(bitxor(d2c(a, 4), d2c(b, 4)))

/* CRC-32 of an image with bytes 28..31 treated as zero (1-based 29..32). */
crc32img: procedure expose t.
  parse arg s
  crc = x2d('FFFFFFFF')
  do i = 1 to length(s)
    if i >= 29 & i <= 32 then byte = 0
    else byte = c2d(substr(s, i, 1))
    crc = u32xor(crc, byte)
    idx = crc // 16; crc = crc % 16; crc = u32xor(crc, t.idx)
    idx = crc // 16; crc = crc % 16; crc = u32xor(crc, t.idx)
  end
  return u32xor(crc, x2d('FFFFFFFF'))

/* ---- file I/O (binary) --------------------------------------------------- */
read_bin: procedure
  parse arg name
  call stream name, 'c', 'open read'
  size = stream(name, 'c', 'query size')
  if size = '' | size = 0 then do
    call stream name, 'c', 'close'
    return ''
  end
  data = charin(name, 1, size)
  call stream name, 'c', 'close'
  return data

write_bin: procedure
  parse arg name, data
  call stream name, 'c', 'close'
  call stream name, 'c', 'open write replace'
  call charout name, data
  call stream name, 'c', 'close'
  return

/* ---- the builder: port of Img.__init__ + fix() ----------------------------
 * Inputs (stems): nseg/ncap counts, seg.i.{vaddr,mem,flags,data}, cap.i.{kind,val}. */
build_image:
  parse arg nseg, ncap, entry, fkind
  so = 64
  co = 0
  if ncap > 0 then co = 64 + 32 * nseg
  pos = roundup16(64 + 32 * nseg + 16 * ncap)
  do i = 1 to nseg
    if length(seg.i.data) > 0 then do
      seg.i.off = pos
      pos = roundup16(pos + length(seg.i.data))
    end
    else seg.i.off = 0
  end
  total = pos

  segtab = ''
  do i = 1 to nseg
    segtab = segtab || p64(seg.i.vaddr) || p64(seg.i.mem) || p32(seg.i.off) ||,
             p32(length(seg.i.data)) || p32(seg.i.flags) || p32(0)
  end
  captab = ''
  do i = 1 to ncap
    captab = captab || p32(cap.i.kind) || p32(0) || p64(cap.i.val)
  end

  hdr = '7F'x || 'EXB' || '01'x || '01'x || p16(64) || p32(0) || p32(1) || p64(entry)
  hdr = hdr || p32(total) || p32(0) || p32(so) || p32(nseg) || p32(co) || p32(ncap)
  hdr = hdr || d2c(fkind, 1) || zeros(15)

  body = hdr || segtab || captab
  body = body || zeros(roundup16(length(body)) - length(body))
  do i = 1 to nseg
    if length(seg.i.data) > 0 then do
      body = body || seg.i.data
      body = body || zeros(roundup16(length(body)) - length(body))
    end
  end
  if length(body) <> total then do
    say 'internal: layout mismatch' length(body) total
    exit 3
  end
  body = overlay(p32(crc32img(body)), body, 29)
  return body

/* ---- base(): the same image exbpack.py's base() builds -------------------- */
do_base:
  parse arg out
  PAGE = 4096
  VA_MIN = x2d('8000000000')
  code = copies('90'x, 15) || 'C3'x
  seg.1.vaddr = VA_MIN;        seg.1.mem = PAGE;     seg.1.data = code;        seg.1.flags = 5
  seg.2.vaddr = VA_MIN + PAGE; seg.2.mem = 2 * PAGE; seg.2.data = 'data-seg';  seg.2.flags = 3
  cap.1.kind = 1; cap.1.val = 8192
  cap.2.kind = 2; cap.2.val = 16
  call write_bin out, build_image(2, 2, VA_MIN, 0)
  return

/* ---- elf2exb: port of elf2exb() ------------------------------------------ */
do_elf2exb:
  parse arg inp out opts
  stack = 0; heap = 0; tokens = 0
  nw = words(opts); k = 1
  do while k <= nw
    w = word(opts, k); v = word(opts, k + 1)
    select
      when w = '--stack'        then stack = v
      when w = '--heap-frames'  then heap = v
      when w = '--tokens'       then tokens = v
      otherwise say 'unknown option' w; exit 2
    end
    k = k + 2
  end

  VA_MIN = x2d('8000000000'); VA_LIMIT = x2d('700000000000')
  e = read_bin(inp)
  if length(e) < 64 | substr(e, 1, 4) <> '7F454C46'x | c2d(substr(e, 5, 1)) <> 2 | c2d(substr(e, 6, 1)) <> 1 then do
    say 'not a little-endian ELF64'; exit 2
  end
  entry = le(e, 24, 8); phoff = le(e, 32, 8); phentsize = le(e, 54, 2); phnum = le(e, 56, 2)

  nseg = 0
  do i = 0 to phnum - 1
    p = phoff + i * phentsize
    if le(e, p, 4) <> 1 then iterate
    pflags = le(e, p + 4, 4); poff = le(e, p + 8, 8); vaddr = le(e, p + 16, 8)
    filesz = le(e, p + 32, 8); memsz = le(e, p + 40, 8)
    if vaddr // 4096 <> 0 then do
      say 'segment at 0x'd2x(vaddr) 'is not page-aligned: link with xent.ld'; exit 2
    end
    if vaddr < VA_MIN | vaddr >= VA_LIMIT then do
      say 'segment at 0x'd2x(vaddr) 'is outside the user range: link with xent.ld'; exit 2
    end
    nseg = nseg + 1
    seg.nseg.vaddr = vaddr
    seg.nseg.mem = memsz
    seg.nseg.flags = 1 + bitand(pflags, 2) + 4 * bitand(pflags, 1)
    seg.nseg.data = substr(e, poff + 1, filesz)
  end

  ncap = 0
  if stack <> 0 then do;  ncap = ncap + 1; cap.ncap.kind = 1; cap.ncap.val = stack;  end
  if heap <> 0 then do;   ncap = ncap + 1; cap.ncap.kind = 2; cap.ncap.val = heap;   end
  if tokens <> 0 then do; ncap = ncap + 1; cap.ncap.kind = 3; cap.ncap.val = tokens; end

  call write_bin out, build_image(nseg, ncap, entry, 0)
  return
