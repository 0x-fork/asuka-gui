// platform/update's builtins (core/src/std/update.c, inflate.c) over what
// the CLI writes: testdata/update, made by internal/update's
// TestCoreTestdata (-update-core) with the code `asuka build` signs and
// makes updates with. A signature checked over a file, archives (gzip,
// tar with PAX names, links, modes, DEFLATE's stored and Huffman blocks)
// unpacked, a delta applied file by file and its tree compared with the
// new version's archive, bsdiff patches, and what must fail cleanly:
// damaged data, paths and links out of the tree, a delta applied to
// another app.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif
#include "t.h"
#include "std/std.h"

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define D "testdata/update/"

static ak_lib_fn fn(const char *name) {
	for (const ak_lib_entry *const *t = ak_std_libs; *t; t++)
		for (const ak_lib_entry *e = *t; e->name; e++)
			if (strcmp(e->name, name) == 0) return e->fn;
	fprintf(stderr, "no builtin %s\n", name);
	abort();
}

static ak_val S(const char *s) { return (ak_val){.p = ak_str_c(s)}; }
static ak_val I(int64_t i) { return (ak_val){.i = i}; }

// call calls a builtin, releasing its string and bytes arguments (refs: a
// bit per argument).
static ak_val call(const char *name, ak_val *args, uint32_t n, uint32_t refs) {
	ak_val r = fn(name)(args, n);
	for (uint32_t i = 0; i < n; i++)
		if (refs >> i & 1) ak_release(args[i].p);
	return r;
}

static char *failure(void) {
	ak_str *s = call("update.failure", NULL, 0, 0).p;
	static char buf[400];
	snprintf(buf, sizeof buf, "%s", s->data);
	ak_release(s);
	return buf;
}

static uint8_t *slurp(const char *path, size_t *n) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *p = malloc((size_t)len + 1);
	*n = fread(p, 1, (size_t)len, f);
	fclose(f);
	p[*n] = 0;
	return p;
}

static void spit(const char *path, const void *p, size_t n) {
	FILE *f = fopen(path, "wb");
	fwrite(p, 1, n, f);
	fclose(f);
}

static void unhex(const char *s, uint8_t *out, size_t n) {
	for (size_t i = 0; i < n; i++) {
		unsigned v;
		sscanf(s + 2 * i, "%2x", &v);
		out[i] = (uint8_t)v;
	}
}

// path_in is dir/name, in one of 16 buffers used in turn: copy what's
// kept (keep).
static char *path_in(const char *dir, const char *name) {
	static char bufs[16][1024];
	static int next;
	char *b = bufs[next++ & 15];
	snprintf(b, 1024, "%s/%s", dir, name);
	return b;
}

static char kept[8][1024];
static int nkept;
static char *keep(const char *p) {
	char *b = kept[nkept++ & 7];
	snprintf(b, 1024, "%s", p);
	return b;
}

static void sha_hex(const uint8_t *p, size_t n, char out[65]) {
	uint8_t d[32];
	ak_sha256(p, n, d);
	for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
}

static bool verify(const char *path, const uint8_t key[32], const uint8_t sig[64], size_t keylen) {
	return call("update.verifyFile", (ak_val[]){S(path), {.p = ak_bytes_new(key, keylen)}, {.p = ak_bytes_new(sig, 64)}}, 3, 7).i;
}

static void keys(uint8_t pub[32], uint8_t sig[64]) {
	size_t n;
	char *k = (char *)slurp(D "key.txt", &n);
	REQUIRE(k && n > 64 + 1 + 64 + 1 + 128);
	unhex(k + 65, pub, 32);
	unhex(k + 65 + 65, sig, 64);
	free(k);
}

TEST(verify_file) {
	uint8_t pub[32], sig[64];
	keys(pub, sig);
	CHECK(verify(D "new.tar.gz", pub, sig, 32));
	// Another file, a changed signature, another key: not signed.
	CHECK(!verify(D "old.tar.gz", pub, sig, 32));
	CHECK_EQ_STR(failure(), "the update is not signed with the app's key");
	sig[10] ^= 1;
	CHECK(!verify(D "new.tar.gz", pub, sig, 32));
	sig[10] ^= 1;
	pub[0] ^= 1;
	CHECK(!verify(D "new.tar.gz", pub, sig, 32));
	pub[0] ^= 1;
	CHECK(!verify(D "new.tar.gz", pub, sig, 31));
	CHECK(!verify(D "nothing.tar.gz", pub, sig, 32));
	// A byte of the archive changed.
	char *dir = ak_fs_temp_dir();
	size_t n;
	uint8_t *a = slurp(D "new.tar.gz", &n);
	a[n / 2] ^= 0x40;
	spit(path_in(dir, "changed.tar.gz"), a, n);
	CHECK(!verify(path_in(dir, "changed.tar.gz"), pub, sig, 32));
	free(a);
	ak_fs_remove_tree(dir);
	ak_free(dir);
}

static bool extract(const char *archive, const char *dir) { return call("update.extract", (ak_val[]){S(archive), S(dir)}, 2, 3).i; }

#if !defined(_WIN32)
// same_tree reports whether a and b hold the same names, links, files and
// permissions; it says how they differ.
static bool same_tree(const char *a, const char *b) {
	DIR *da = opendir(a);
	if (!da) {
		T_FAIL("%s: no directory", a);
		return false;
	}
	bool ok = true;
	int na = 0, nb = 0;
	struct dirent *e;
	while ((e = readdir(da))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		na++;
		char pa[1024], pb[1024];
		snprintf(pa, sizeof pa, "%s/%s", a, e->d_name);
		snprintf(pb, sizeof pb, "%s/%s", b, e->d_name);
		struct stat sa, sb;
		if (lstat(pa, &sa) != 0 || lstat(pb, &sb) != 0) {
			T_FAIL("%s: only in one tree", pa);
			ok = false;
			continue;
		}
		if ((sa.st_mode & S_IFMT) != (sb.st_mode & S_IFMT)) {
			T_FAIL("%s: a different kind", pa);
			ok = false;
		} else if (S_ISDIR(sa.st_mode)) {
			ok = same_tree(pa, pb) && ok;
		} else if (S_ISLNK(sa.st_mode)) {
			char la[512] = "", lb[512] = "";
			if (readlink(pa, la, sizeof la - 1) < 0 || readlink(pb, lb, sizeof lb - 1) < 0) {
				T_FAIL("%s: its link can't be read", pa);
				ok = false;
			} else if (strcmp(la, lb)) {
				T_FAIL("%s: links to %s, not %s", pa, la, lb);
				ok = false;
			}
		} else {
			size_t x, y;
			uint8_t *fa = slurp(pa, &x), *fb = slurp(pb, &y);
			if (x != y || memcmp(fa, fb, x) || (sa.st_mode & 0777) != (sb.st_mode & 0777)) {
				T_FAIL("%s: differs (%zu bytes, mode %o; %zu, %o)", pa, x, (unsigned)(sa.st_mode & 0777), y, (unsigned)(sb.st_mode & 0777));
				ok = false;
			}
			free(fa), free(fb);
		}
	}
	closedir(da);
	DIR *db = opendir(b);
	while (db && (e = readdir(db)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) nb++;
	if (db) closedir(db);
	if (na != nb) {
		T_FAIL("%s has %d entries, %s %d", a, na, b, nb);
		ok = false;
	}
	return ok;
}
#endif

TEST(extract) {
	char *dir = ak_fs_temp_dir();
	char *out = keep(path_in(dir, "new"));
	REQUIRE(extract(D "new.tar.gz", out));
	size_t n, m;
	uint8_t *exe = slurp(path_in(out, "Notes.exe"), &n), *want = slurp(D "new.bin", &m);
	CHECK(exe && n == m && memcmp(exe, want, n) == 0);
	free(exe), free(want);
	uint8_t *longf = slurp(path_in(out, "assets/a-rather-long-name-a-rather-long-name-a-rather-long-name-a-rather-long-name-a-rather-long-name-a-rather-long-name-a-rather-long-name-a-rather-long-name-.txt"), &n);
	CHECK(longf && n == 6 && memcmp(longf, "longer", 6) == 0);
	free(longf);
	uint8_t *noise = slurp(path_in(out, "assets/noise.bin"), &n);
	CHECK(noise && n == 80 << 10);
	free(noise);
#if !defined(_WIN32)
	char link[64] = "";
	CHECK(readlink(path_in(out, "assets/current"), link, sizeof link - 1) > 0);
	CHECK_EQ_STR(link, "new.txt");
	struct stat st;
	CHECK(stat(path_in(out, "bin/tool"), &st) == 0 && (st.st_mode & 0777) == 0755);
	CHECK(stat(path_in(out, "assets/same.txt"), &st) == 0 && (st.st_mode & 0777) == 0644);
	CHECK(stat(path_in(out, "empty"), &st) == 0 && S_ISDIR(st.st_mode));
#endif
	ak_fs_remove_tree(dir);
	ak_free(dir);
}

TEST(extract_refuses) {
	char *dir = ak_fs_temp_dir();
	struct { const char *archive, *why; } cases[] = {
		{D "evil-path.tar.gz", "the update's archive has a path outside the app: ../evil"},
		{D "evil-link.tar.gz", "the update's archive has a link pointing outside the app: a/b"},
		{D "evil-through.tar.gz", "a link, which the update doesn't write through"},
		{D "evil-fifo.tar.gz", "the update's archive holds what isn't a file, directory or link: fifo"},
		{D "nothing.tar.gz", "can't be opened"},
	};
	for (size_t i = 0; i < AK_LEN(cases); i++) {
		char sub[16];
		snprintf(sub, sizeof sub, "t%zu", i);
		char *out = path_in(dir, sub);
		if (extract(cases[i].archive, out)) T_FAIL("%s unpacked", cases[i].archive);
		else if (!strstr(failure(), cases[i].why)) T_FAIL("%s: %s, not %s", cases[i].archive, failure(), cases[i].why);
	}
	// Nothing was written beside the directories.
	CHECK(fopen(path_in(dir, "evil"), "r") == NULL);
	CHECK(fopen(path_in(dir, "x"), "r") == NULL);
	// A damaged archive: cut short at many places, or a byte changed.
	size_t n;
	uint8_t *a = slurp(D "old.tar.gz", &n);
	for (size_t cut = 0; cut < n; cut += n / 23 + 1) {
		spit(path_in(dir, "cut.tar.gz"), a, cut);
		char sub[32];
		snprintf(sub, sizeof sub, "cut%zu", cut);
		if (extract(path_in(dir, "cut.tar.gz"), path_in(dir, sub))) T_FAIL("cut at %zu: unpacked", cut);
	}
	for (size_t at = 20; at < n; at += n / 17) {
		a[at] ^= 0x21;
		spit(path_in(dir, "flip.tar.gz"), a, n);
		a[at] ^= 0x21;
		char sub[32];
		snprintf(sub, sizeof sub, "flip%zu", at);
		if (extract(path_in(dir, "flip.tar.gz"), path_in(dir, sub))) T_FAIL("byte %zu changed: unpacked", at);
	}
	free(a);
	ak_fs_remove_tree(dir);
	ak_free(dir);
}

// gzip writes data as a gzip file of stored blocks, its CRC-32 and size
// right: what the tar inside says is what's read.
static void gzip(const char *path, const uint8_t *data, size_t n) {
	FILE *f = fopen(path, "wb");
	if (!f) {
		T_FAIL("can't write %s", path);
		return;
	}
	static const uint8_t head[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
	fwrite(head, 1, sizeof head, f);
	size_t at = 0;
	do {
		size_t k = n - at < 65535 ? n - at : 65535;
		uint8_t b[5] = {at + k == n, (uint8_t)k, (uint8_t)(k >> 8), (uint8_t)~k, (uint8_t)(~k >> 8)};
		fwrite(b, 1, 5, f);
		if (k) fwrite(data + at, 1, k, f);
		at += k;
	} while (at < n);
	uint32_t crc = 0xffffffff;
	for (size_t i = 0; i < n; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) crc = crc >> 1 ^ (0xedb88320 & -(crc & 1));
	}
	crc = ~crc;
	uint8_t tail[8] = {(uint8_t)crc, (uint8_t)(crc >> 8), (uint8_t)(crc >> 16), (uint8_t)(crc >> 24), (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24)};
	fwrite(tail, 1, 8, f);
	fclose(f);
}

// tar_entry fills a ustar header: name, type, its size field as given
// (12 bytes: octal, or GNU's base 256), its checksum.
static void tar_entry(uint8_t h[512], const char *name, char type, const uint8_t size[12]) {
	memset(h, 0, 512);
	snprintf((char *)h, 100, "%s", name);
	memcpy(h + 100, "0000644", 8);
	memcpy(h + 124, size, 12);
	h[156] = (uint8_t)type;
	memcpy(h + 257, "ustar\0" "00", 8);
	unsigned sum = 0;
	for (int i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? ' ' : h[i];
	snprintf((char *)h + 148, 8, "%06o", sum);
}

TEST(extract_huge_sizes) {
	// An entry's size at int64's end (GNU's base 256, or a PAX header's):
	// damaged, not padded past it.
	char *dir = ak_fs_temp_dir();
	char *archive = path_in(dir, "huge.tar.gz");
	static const uint8_t max256[12] = {0x80, 0, 0, 0, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	static const uint8_t zero[12] = "00000000000";
	uint8_t tar[512 * 6] = {0};
	const char types[] = {'5', '0', '2'};
	for (size_t t = 0; t < AK_LEN(types); t++) {
		tar_entry(tar, "d", types[t], max256);
		gzip(archive, tar, 512 * 3);
		char sub[16];
		snprintf(sub, sizeof sub, "b%zu", t);
		if (extract(archive, path_in(dir, sub))) T_FAIL("type %c unpacked", types[t]);
		else if (!strstr(failure(), "damaged")) T_FAIL("type %c: %s", types[t], failure());
	}
	// A PAX size at int64's end, and one beyond it.
	static const char *records[] = {"28 size=9223372036854775807\n", "38 size=99999999999999999999999999999\n"};
	for (size_t r = 0; r < AK_LEN(records); r++) {
		uint8_t size[12];
		snprintf((char *)size, sizeof size, "%011o", (unsigned)strlen(records[r]));
		memset(tar, 0, sizeof tar);
		tar_entry(tar, "pax", 'x', size);
		memcpy(tar + 512, records[r], strlen(records[r]));
		tar_entry(tar + 1024, "f", '0', zero);
		gzip(archive, tar, sizeof tar);
		char sub[16];
		snprintf(sub, sizeof sub, "pax%zu", r);
		if (extract(archive, path_in(dir, sub))) T_FAIL("%s unpacked", records[r]);
		else if (!strstr(failure(), "damaged")) T_FAIL("%s: %s", records[r], failure());
	}
	ak_fs_remove_tree(dir);
	ak_free(dir);
}

// A field of an entry of the delta's index (JSON as Go writes it: no
// spaces, no escapes in these names): its string, or its number.
static bool field_str(const char *obj, const char *end, const char *key, char *out, size_t cap) {
	char k[32];
	snprintf(k, sizeof k, "\"%s\":\"", key);
	const char *p = strstr(obj, k);
	if (!p || p > end) return false;
	p += strlen(k);
	const char *q = strchr(p, '"');
	size_t n = (size_t)(q - p) < cap - 1 ? (size_t)(q - p) : cap - 1;
	memcpy(out, p, n);
	out[n] = 0;
	return true;
}

static int64_t field_int(const char *obj, const char *end, const char *key) {
	char k[32];
	snprintf(k, sizeof k, "\"%s\":", key);
	const char *p = strstr(obj, k);
	if (!p || p > end) return 0;
	return strtoll(p + strlen(k), NULL, 10);
}

// apply applies the delta to old into out, as the package does (its
// checks of the index aside): what deltaFile, makeDir and makeLink do.
static bool apply(const char *delta, const char *old, const char *out) {
	ak_str *index = call("update.deltaIndex", (ak_val[]){S(delta)}, 1, 1).p;
	if (!index) return false;
	int64_t offset = call("update.deltaStart", (ak_val[]){S(delta)}, 1, 1).i;
	bool ok = offset > 0 && strstr(index->data, "\"from\":\"1.0.0\"") && strstr(index->data, "\"version\":\"1.1.0\"");
	const char *p = strstr(index->data, "\"entries\":[");
	ok = ok && p;
	for (p = p ? strchr(p, '{') : NULL; ok && p; p = strchr(p + 1, '{')) {
		const char *end = strchr(p, '}');
		char path[512] = "", link[512] = "", from[512] = "", sha[80] = "";
		field_str(p, end, "path", path, sizeof path);
		if (strstr(p, "\"dir\":true") && strstr(p, "\"dir\":true") < end) {
			ok = call("update.makeDir", (ak_val[]){S(out), S(path)}, 2, 3).i;
		} else if (field_str(p, end, "link", link, sizeof link)) {
			ok = call("update.makeLink", (ak_val[]){S(out), S(path), S(link)}, 3, 7).i;
		} else {
			field_str(p, end, "from", from, sizeof from);
			field_str(p, end, "sha256", sha, sizeof sha);
			int64_t data = field_int(p, end, "data"), size = field_int(p, end, "size"), mode = field_int(p, end, "mode");
			ok = call("update.deltaFile", (ak_val[]){S(delta), I(offset), I(data), S(old), S(from), S(out), S(path), I(size), S(sha), I(mode)}, 10, 1 | 8 | 16 | 32 | 64 | 256).i;
			offset += data;
		}
	}
	ak_release(index);
	return ok;
}

TEST(delta) {
	char *dir = ak_fs_temp_dir();
	char *old = keep(path_in(dir, "old")), *made = keep(path_in(dir, "made")), *want = keep(path_in(dir, "want"));
	REQUIRE(extract(D "old.tar.gz", old) && extract(D "new.tar.gz", want));
	REQUIRE(call("update.makeDir", (ak_val[]){S(dir), S("made")}, 2, 3).i);
	if (!apply(D "old-to-new.delta", old, made)) T_FAIL("the delta failed: %s", failure());
#if !defined(_WIN32)
	CHECK(same_tree(made, want));
#endif
	// The same delta, on an app that changed since it was installed: the
	// file it patches comes out wrong, and the updater takes the archive.
	size_t n;
	uint8_t *exe = slurp(path_in(old, "Notes.exe"), &n);
	exe[100] ^= 1;
	spit(path_in(old, "Notes.exe"), exe, n);
	free(exe);
	CHECK(call("update.makeDir", (ak_val[]){S(dir), S("made2")}, 2, 3).i);
	CHECK(!apply(D "old-to-new.delta", old, path_in(dir, "made2")));
	CHECK_EQ_STR(failure(), "the delta update made Notes.exe wrong");
	// Not a delta.
	CHECK(call("update.deltaIndex", (ak_val[]){S(D "old.tar.gz")}, 1, 1).p == NULL);
	CHECK_EQ_STR(failure(), "not a delta update");
	CHECK_EQ_INT(call("update.deltaStart", (ak_val[]){S(D "new.bin")}, 1, 1).i, -1);
	ak_fs_remove_tree(dir);
	ak_free(dir);
}

static bool patch_file(const char *patch, int64_t len, const char *out_dir, int64_t size, const char *sha) {
	return call("update.deltaFile", (ak_val[]){S(patch), I(0), I(len), S("testdata/update"), S("old.bin"), S(out_dir), S("new.bin"), I(size), S(sha), I(0644)}, 10, 1 | 8 | 16 | 32 | 64 | 256).i;
}

TEST(bsdiff) {
	char *dir = ak_fs_temp_dir();
	size_t pn, nn;
	uint8_t *p = slurp(D "new.patch", &pn), *want = slurp(D "new.bin", &nn);
	char sha[65];
	sha_hex(want, nn, sha);
	REQUIRE(patch_file(D "new.patch", (int64_t)pn, dir, (int64_t)nn, sha));
	size_t got;
	uint8_t *made = slurp(path_in(dir, "new.bin"), &got);
	CHECK(made && got == nn && memcmp(made, want, nn) == 0);
	free(made);
	// A file of another size, or another hash: wrong, and said so.
	remove(path_in(dir, "new.bin"));
	CHECK(!patch_file(D "new.patch", (int64_t)pn, dir, (int64_t)nn - 1, sha));
	CHECK_EQ_STR(failure(), "the delta update made new.bin wrong");
	remove(path_in(dir, "new.bin"));
	CHECK(!patch_file(D "new.patch", (int64_t)pn, dir, (int64_t)nn + 1, sha));
	remove(path_in(dir, "new.bin"));
	char other[65];
	memcpy(other, sha, 65);
	other[0] = other[0] == '0' ? '1' : '0';
	CHECK(!patch_file(D "new.patch", (int64_t)pn, dir, (int64_t)nn, other));
	// Damaged patches fail cleanly: cut short anywhere, or a byte changed.
	for (size_t cut = 0; cut < pn; cut++) {
		spit(path_in(dir, "cut.patch"), p, cut);
		remove(path_in(dir, "new.bin"));
		// What follows a stream's last block may go; anything else fails.
		if (patch_file(path_in(dir, "cut.patch"), (int64_t)cut, dir, (int64_t)nn, sha)) {
			uint8_t *again = slurp(path_in(dir, "new.bin"), &got);
			if (cut < pn - 8 || !again || got != nn || memcmp(again, want, nn)) T_FAIL("cut at %zu: patched", cut);
			free(again);
		}
	}
	for (size_t at = 0; at < pn; at++) {
		for (int bit = 0; bit < 8; bit += 3) {
			p[at] ^= (uint8_t)(1 << bit);
			spit(path_in(dir, "flip.patch"), p, pn);
			p[at] ^= (uint8_t)(1 << bit);
			remove(path_in(dir, "new.bin"));
			// A bit no code reads (a stream's padding after its last
			// block) changes nothing; any other fails.
			if (patch_file(path_in(dir, "flip.patch"), (int64_t)pn, dir, (int64_t)nn, sha)) {
				uint8_t *again = slurp(path_in(dir, "new.bin"), &got);
				if (!again || got != nn || memcmp(again, want, nn)) T_FAIL("byte %zu bit %d changed: patched wrong", at, bit);
				free(again);
			}
		}
	}
	free(p), free(want);
	ak_fs_remove_tree(dir);
	ak_free(dir);
}

TEST(paths) {
	const char *valid[] = {"a", "a/b", "Notes.app/Contents/MacOS/Notes", "a..b", ".hidden/x"};
	const char *invalid[] = {"", ".", "..", "/a", "a/", "a//b", "a/./b", "a/../b", "../a", "a\\b"};
	for (size_t i = 0; i < AK_LEN(valid); i++)
		if (!call("update.validPath", (ak_val[]){S(valid[i])}, 1, 1).i) T_FAIL("%s isn't valid", valid[i]);
	for (size_t i = 0; i < AK_LEN(invalid); i++)
		if (call("update.validPath", (ak_val[]){S(invalid[i])}, 1, 1).i) T_FAIL("%s is valid", invalid[i]);
	char *dir = ak_fs_temp_dir();
	CHECK(call("update.makeLink", (ak_val[]){S(dir), S("a/l"), S("../b")}, 3, 7).i);
	CHECK(!call("update.makeLink", (ak_val[]){S(dir), S("a/m"), S("../../b")}, 3, 7).i);
	CHECK_EQ_STR(failure(), "the update's link a/m points outside the app");
	CHECK(!call("update.makeLink", (ak_val[]){S(dir), S("n"), S("/etc")}, 3, 7).i);
	// Nothing is made through a link.
	CHECK(call("update.makeLink", (ak_val[]){S(dir), S("up"), S(".")}, 3, 7).i);
	CHECK(!call("update.makeDir", (ak_val[]){S(dir), S("up/x")}, 2, 3).i);
	CHECK(strstr(failure(), "a link, which the update doesn't write through") != NULL);
	ak_fs_remove_tree(dir);
	ak_free(dir);
}

T_MAIN()
