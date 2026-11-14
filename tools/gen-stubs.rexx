/* gen-stubs.rexx -- prints a C file with an abort stub for every undefined
 * symbol of an object file. The test links those stubs, so any kernel symbol
 * the test reaches stops the run instead of returning a false result.
 * Usage: rexx tools/gen-stubs.rexx OBJ.o ["prefix ..."] > stubs.c
 * The optional prefixes name symbols that the test links from real objects.
 * NOTE: use PARSE ARG, never ARG (ARG uppercases its input in REXX). */
parse arg obj extra
if obj = '' then do
  say 'usage: rexx tools/gen-stubs.rexx OBJ.o'
  exit 2
end

tmp  = 'build/host/nm_undef.txt'
libc = 'memcpy memset memmove memcmp abort __stack_chk_fail'

address system 'nm -u' obj '>' tmp
if rc <> 0 then do
  say 'gen-stubs: nm failed on' obj
  exit 1
end

say '#include <stdlib.h>'
do while lines(tmp) > 0
  line = strip(linein(tmp))
  if line = '' then iterate
  name = word(line, words(line))
  /* libc is linked by the test; the nvme_q/nvme_xq objects are linked too */
  if wordpos(name, libc) > 0 then iterate
  if pos('nvme_q_', name) = 1 | pos('nvme_xq_', name) = 1 then iterate
  skip = 0
  do k = 1 to words(extra)
    if pos(word(extra, k), name) = 1 then skip = 1
  end
  if skip then iterate
  say 'void' name '(void) { abort(); }'
end
call stream tmp, 'c', 'close'
exit 0
