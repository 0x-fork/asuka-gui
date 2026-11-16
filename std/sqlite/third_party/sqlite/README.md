# SQLite

SQLite 3.54.1's amalgamation (`sqlite-amalgamation-3540100.zip`,
https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip, SHA3-256
`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`):
`sqlite3.c` and `sqlite3.h`, unchanged. SQLite is in the public domain
(https://www.sqlite.org/copyright.html): no license applies to it, and
none is needed to ship it.

std/sqlite's C (its `asuka.toml`'s `[c]`) compiles it with `csrc/asq.c`
for every target, with the same compile-time options everywhere, and
the apps that import std/sqlite link it statically (on the web, its
worker's module). To update: replace the two files with a newer
release's and change the version and its hash here.
