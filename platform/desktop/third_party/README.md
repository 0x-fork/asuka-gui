# Vendored libraries

Built with the shared C (src/barcode_vendor.c includes their sources),
unchanged, each under its own license (the LICENSE beside it):

- `quirc/`: quirc 1.2 (https://github.com/dlbeer/quirc, tag `v1.2`), ISC:
  `lib/*.c` and `lib/*.h`. QR codes.
- `libdmtx/`: libdmtx 0.7.8 (https://github.com/dmtx/libdmtx, tag
  `v0.7.8`), BSD 2-clause: `dmtx.c` (which includes the others), its
  sources and `dmtx.h`, `dmtxstatic.h`. Data Matrix.

They compile with the repository's flags, but for the warnings
barcode_vendor.c turns off for them alone. To update one: replace its
files with a newer release's, and change the version here and in
barcode_vendor.c.
