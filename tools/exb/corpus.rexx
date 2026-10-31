/* corpus.rexx -- generates the hostile .exb test corpus (REXX port of the
 * corpus() in exbpack.py). The repository stores this generator, not the
 * binaries. Output is byte-identical to the Python reference.
 * usage: regina ./tools/exb/corpus.rexx OUTDIR
 * NOTE: use PARSE ARG, never ARG (ARG uppercases its input in REXX). */
numeric digits 20
parse arg outdir
if outdir = '' then do
  say 'usage: regina ./tools/exb/corpus.rexx OUTDIR'
  exit 2
end
call init_table
count = 0
PAGE = 4096
VA   = x2d('8000000000')
LIM  = x2d('700000000000')
CODE = copies('90'x, 15) || 'C3'x
SEG0 = 64; SEG1 = 96; CAP0 = 128; CAP1 = 144; DATA0 = 160; DATA1 = 176

address system 'mkdir -p' outdir
mfn = outdir'/manifest.txt'
call stream mfn, 'c', 'close'
call stream mfn, 'c', 'open write replace'

/* ---- valid images ---- */
call base 2
call emit 'valid_full', 'OK', 1
call base 0
call emit 'valid_nocaps', 'OK', 1
call base 3
call emit 'valid_optional_unknown_cap', 'OK', 1
call bss_image
call emit 'valid_bss_only_segment', 'OK', 1
call base 4
call emit 'valid_max_stack', 'OK', 1

/* ---- size guards ---- */
call base 2
full = img
img = ''
call emit 'empty', 'E_SIZE', 0
img = substr(full, 1, 40)
call emit 'truncated_header', 'E_SIZE', 0
img = substr(full, 1, 63)
call emit 'only_63_bytes', 'E_SIZE', 0

/* ---- header ---- */
call base 2; call put8 0, 126;             call emit 'bad_magic', 'E_MAGIC', 1
call base 2; call put8 4, 2;               call emit 'bad_version', 'E_VERSION', 1
call base 2; call put8 5, 2;               call emit 'bad_endian', 'E_VERSION', 1
call base 2; call put8 6, 65;              call emit 'bad_header_size', 'E_HEADER', 1
call base 2; call put32 8, 1;              call emit 'flags_nonzero', 'E_HEADER', 1
call base 2; call put8 55, 1;              call emit 'reserved_nonzero', 'E_HEADER', 1
call base 2; call put8 48, 1;              call emit 'kind_kernel', 'E_KIND', 1
call base 2; call put32 12, 2;             call emit 'target_other', 'E_TARGET', 1
call base 2; call put32 24, length(img)+1; call fixcrc
             call emit 'filesize_field_too_big', 'E_SIZE', 0
call base 2; call flip_bit DATA1;          call emit 'bad_crc', 'E_CRC', 0

/* ---- segment table ---- */
call base 2; call put32 36, 0;             call emit 'segcount_zero', 'E_SEGTAB', 1
call base 2; call put32 36, 9;             call emit 'segcount_nine', 'E_SEGTAB', 1
call base 2; call put32 32, length(img)-8; call emit 'segtab_past_end', 'E_SEGTAB', 1
call base 2; call put32 32, 32;            call emit 'segtab_inside_header', 'E_SEGTAB', 1
call base 2; call put32 32, x2d('FFFFFFF0'); call emit 'segtab_offset_wraps', 'E_SEGTAB', 1

/* ---- one segment field at a time ---- */
call base 2; call put32 SEG1+24, 7;       call emit 'seg_write_and_exec', 'E_WX', 1
call base 2; call put32 SEG1+24, 2;       call emit 'seg_write_without_read', 'E_SEG', 1
call base 2; call put32 SEG1+24, 9;       call emit 'seg_unknown_flag', 'E_SEG', 1
call base 2; call put32 SEG1+28, 1;       call emit 'seg_reserved_nonzero', 'E_SEG', 1
call base 2; call put64 SEG1, VA+PAGE+8;  call emit 'seg_unaligned_vaddr', 'E_SEG', 1
call base 2; call put64 SEG1, 4096;       call emit 'seg_below_user_range', 'E_SEG', 1
call base 2; call put64 SEG1, x2d('200000'); call emit 'seg_in_kernel_range', 'E_SEG', 1
call base 2; call put64 SEG1, LIM;        call emit 'seg_at_limit', 'E_SEG', 1
call base 2; call put64 SEG1, LIM-PAGE;   call put64 SEG1+8, 2*PAGE
             call emit 'seg_ends_past_limit', 'E_SEG', 1
call base 2; call put64 SEG1, x2d('FFFFFFFFFFFFF000'); call emit 'seg_vaddr_near_2_64', 'E_SEG', 1
call base 2; call put64 SEG1+8, x2d('8000000000000000'); call emit 'seg_mem_2_63', 'E_SEG', 1
call base 2; call put64 SEG1+8, x2d('FFFFFFFFFFFFFFFF'); call emit 'seg_mem_all_ones', 'E_SEG', 1
call base 2; call put64 SEG1+8, 0;       call emit 'seg_mem_zero', 'E_SEG', 1
call base 2; call put32 SEG1+20, 2*PAGE+1; call emit 'seg_filesz_gt_mem', 'E_SEG', 1
call base 2; call put32 SEG1+16, length(img)-2; call emit 'seg_data_past_end', 'E_SEG', 1
call base 2; call put32 SEG1+16, 8;      call emit 'seg_data_inside_header', 'E_SEG', 1
call base 2; call put32 SEG1+16, x2d('FFFFFFFF'); call emit 'seg_data_offset_wraps', 'E_SEG', 1
call base 2; call put32 SEG1+20, 0;      call emit 'seg_no_data_but_offset', 'E_SEG', 1
call base 2; call put32 SEG1+16, SEG0+8; call emit 'seg_data_overlaps_segtab', 'E_OVERLAP', 1
call base 2; call put32 SEG1+16, DATA0;  call emit 'seg_data_overlaps_other_data', 'E_OVERLAP', 1
call base 2; call put64 SEG1, VA;        call emit 'seg_vaddr_overlap', 'E_OVERLAP', 1
call base 2; call put64 SEG0+8, 2*PAGE;  call emit 'seg_vaddr_overlap_by_growth', 'E_OVERLAP', 1

/* ---- too many pages: its own image ---- */
seg.1.vaddr = VA; seg.1.mem = 4096*PAGE; seg.1.data = 'C3'x; seg.1.flags = 5
seg.2.vaddr = VA + 4096*PAGE; seg.2.mem = PAGE; seg.2.data = ''; seg.2.flags = 3
img = build(2, 0, VA, 0)
call emit 'too_many_pages', 'E_LIMIT', 1

/* ---- entry ---- */
call base 2; call put64 16, VA+PAGE;      call emit 'entry_in_data_segment', 'E_ENTRY', 1
call base 2; call put64 16, VA+16;        call emit 'entry_past_initialized_bytes', 'E_ENTRY', 1
call base 2; call put64 16, 0;            call emit 'entry_zero', 'E_ENTRY', 1
call base 2; call put64 16, x2d('FFFFFFFFFFFFFFFF'); call emit 'entry_all_ones', 'E_ENTRY', 1

/* ---- capabilities ---- */
call base 2; call put32 44, 9;            call emit 'cap_count_nine', 'E_CAP', 1
call base 0; call put32 40, 64;           call emit 'cap_offset_without_caps', 'E_CAP', 1
call base 2; call put32 40, length(img)-8; call emit 'cap_table_past_end', 'E_CAP', 1
call base 2; call put32 CAP0, 99;         call emit 'cap_unknown_mandatory', 'E_CAP', 1
call base 2; call put32 CAP0, 0;          call emit 'cap_kind_zero', 'E_CAP', 1
call base 2; call put32 CAP1, 1;          call emit 'cap_duplicate_stack', 'E_CAP', 1
call base 2; call put64 CAP0+8, 4097;     call emit 'cap_stack_unaligned', 'E_CAP', 1
call base 2; call put64 CAP0+8, 0;        call emit 'cap_stack_zero', 'E_CAP', 1
call base 2; call put64 CAP0+8, 65*PAGE;  call emit 'cap_stack_too_big', 'E_CAP', 1
call base 2; call put64 CAP1+8, 1048576+1; call emit 'cap_heap_too_big', 'E_CAP', 1
call base 2; call put32 CAP1+4, 1;       call emit 'cap_reserved_nonzero', 'E_CAP', 1
call base 2; call put32 40, SEG0+8;      call emit 'cap_table_overlaps_segtab', 'E_OVERLAP', 1

call stream mfn, 'c', 'close'
say 'corpus:' count 'images in' outdir
exit 0

/* ======================= builders and helpers ======================= */

/* Base images, as base() in exbpack.py. v: 2 default caps, 0 none,
 * 3 unknown optional cap, 4 maximum stack. */
base: parse arg v
  seg.1.vaddr = VA;        seg.1.mem = PAGE;     seg.1.data = CODE;      seg.1.flags = 5
  seg.2.vaddr = VA + PAGE; seg.2.mem = 2*PAGE;   seg.2.data = 'data-seg'; seg.2.flags = 3
  select
    when v = 2 then do
      cap.1.kind = 1; cap.1.val = 8192
      cap.2.kind = 2; cap.2.val = 16
      ncap = 2
    end
    when v = 0 then ncap = 0
    when v = 3 then do
      cap.1.kind = 1; cap.1.val = 4096
      cap.2.kind = 2147483648 + 77; cap.2.val = 123
      ncap = 2
    end
    when v = 4 then do
      cap.1.kind = 1; cap.1.val = 64*PAGE
      ncap = 1
    end
  end
  img = build(2, ncap, VA, 0)
  return

/* valid_bss_only_segment: a code page and 64 pages of .bss, no caps. */
bss_image:
  seg.1.vaddr = VA;        seg.1.mem = PAGE;      seg.1.data = copies('C3'x, 4); seg.1.flags = 5
  seg.2.vaddr = VA + PAGE; seg.2.mem = 64*PAGE;   seg.2.data = '';               seg.2.flags = 3
  img = build(2, 0, VA, 0)
  return

/* Patches the current image in place. Offsets are 0-based, as in the format. */
put8: parse arg off, v
  img = overlay(d2c(v, 1), img, off + 1)
  return
put32: parse arg off, v
  img = overlay(p32(v), img, off + 1)
  return
put64: parse arg off, v
  img = overlay(p64(v), img, off + 1)
  return

/* Flips bit 0 of the byte at off, without fixing the checksum. */
flip_bit: parse arg off
  v = c2d(substr(img, off + 1, 1))
  if v // 2 = 1 then v = v - 1
  else v = v + 1
  call put8 off, v
  return

fixcrc:
  img = overlay(p32(crc32img(img)), img, 29)
  return

fixall:
  img = overlay(p32(length(img)), img, 25)
  call fixcrc
  return

/* Writes the current image and its manifest line. dofix=1 recomputes size and
 * checksum first, as Img.fix() does in exbpack.py. */
emit: parse arg name, verdict, dofix
  if dofix = 1 then call fixall
  fn = outdir'/'name'.exb'
  call stream fn, 'c', 'close'
  call stream fn, 'c', 'open write replace'
  call charout fn, img
  call stream fn, 'c', 'close'
  call lineout mfn, name verdict
  count = count + 1
  return

/* Port of Img.__init__: lays out header, segment table, capability table and
 * data, then computes the size and checksum. Reads the stems seg.i and cap.i. */
build: parse arg nseg, ncap, entry, fkind
  so = 64
  co = 0
  if ncap > 0 then co = 64 + 32*nseg
  pos = roundup16(64 + 32*nseg + 16*ncap)
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

/* ---- encoding ---- */
p16: procedure; parse arg v; return reverse(d2c(v, 2))
p32: procedure; parse arg v; return reverse(d2c(v, 4))
p64: procedure; parse arg v; return reverse(d2c(v, 8))
roundup16: procedure; parse arg x; n = x + 15; return n - (n // 16)
zeros: procedure; parse arg n; return copies('00'x, n)

/* ---- CRC-32, nibble table (same algorithm as exb.c) ---- */
init_table:
  t.0  = x2d('00000000'); t.1  = x2d('1DB71064'); t.2  = x2d('3B6E20C8'); t.3  = x2d('26D930AC')
  t.4  = x2d('76DC4190'); t.5  = x2d('6B6B51F4'); t.6  = x2d('4DB26158'); t.7  = x2d('5005713C')
  t.8  = x2d('EDB88320'); t.9  = x2d('F00F9344'); t.10 = x2d('D6D6A3E8'); t.11 = x2d('CB61B38C')
  t.12 = x2d('9B64C2B0'); t.13 = x2d('86D3D2D4'); t.14 = x2d('A00AE278'); t.15 = x2d('BDBDF21C')
  return

u32xor: procedure
  parse arg a, b
  return c2d(bitxor(d2c(a, 4), d2c(b, 4)))

/* CRC-32 with bytes 28..31 treated as zero (1-based 29..32). */
crc32img: procedure expose t.
  parse arg s
  crc = x2d('FFFFFFFF')
  do k = 1 to length(s)
    if k >= 29 & k <= 32 then byte = 0
    else byte = c2d(substr(s, k, 1))
    crc = u32xor(crc, byte)
    idx = crc // 16; crc = crc % 16; crc = u32xor(crc, t.idx)
    idx = crc // 16; crc = crc % 16; crc = u32xor(crc, t.idx)
  end
  return u32xor(crc, x2d('FFFFFFFF'))
