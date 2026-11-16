# md4c

md4c 0.5.2 (tag `release-0.5.2`, https://github.com/mity/md4c), MIT
licensed (LICENSE.md): `src/md4c.c`, `src/md4c.h`, `src/entity.c` and
`src/entity.h`, unchanged. core/src/std/markdown.c compiles them in
(asuka/markdown's reader, on every platform), its two exported names
renamed to the core's (`ak_md_parse`, `ak_md_entity_lookup`); they
build with the core's flags. To update: replace the four files with a
newer release's and change the version here and in markdown.c.
