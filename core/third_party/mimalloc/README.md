# mimalloc

mimalloc 3.6.1 (tag `v3.6.1`, https://github.com/microsoft/mimalloc, GitHub's
archive of the tag, SHA-256 2672e6a99d4579fb4bb32d9de0d63aa424ce600d31fbfaf06eb74d940d2e07a6),
Microsoft's, MIT licensed (LICENSE): its src/ and include/, unchanged.
core/src/base/alloc.c compiles it in (src/static.c: the whole allocator,
one file) as what ak_alloc, ak_realloc, ak_free and the small blocks
take memory from, with the core's flags, its exported names renamed to
the core's (core/src/base/mimalloc_names.h: ak_mi_...), never overriding
the process's malloc. Not under AddressSanitizer or MemorySanitizer (the
core's tests) or on the web, which keep the C library's.

Written by internal/mimallocgen (`go generate ./internal/mimallocgen`). To
update: change the version and SHA-256 there and run it, which also
writes mimalloc_names.h again.
