// encoding.c: std/encoding's builtins: Base64 (RFC 4648, the standard
// alphabet padded; the URL alphabet unpadded), Base32 (RFC 4648, both
// alphabets), hexadecimal, fixed-width numbers in either byte order,
// LEB128 varints, IEEE 754 bits and UTF-16. std/encoding's Asuka writes
// the typed API (readU16, Reader, Writer) on the untyped ones here.
#include "std/std.h"

#include <inttypes.h>
#include <string.h>

#define BYTES(i) ((ak_bytes *)args[i].p)
#define STR(i) ((ak_str *)args[i].p)

static const char std_alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char url_alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static ak_str *b64(const ak_bytes *b, const char *alpha, bool pad) {
	size_t n = b ? b->len : 0;
	size_t out = pad ? (n + 2) / 3 * 4 : n / 3 * 4 + (n % 3 ? n % 3 + 1 : 0);
	ak_str *s = ak_str_new(NULL, out);
	char *o = s->data;
	size_t i = 0;
	for (; i + 3 <= n; i += 3) {
		uint32_t v = (uint32_t)b->data[i] << 16 | (uint32_t)b->data[i + 1] << 8 | b->data[i + 2];
		*o++ = alpha[v >> 18 & 63];
		*o++ = alpha[v >> 12 & 63];
		*o++ = alpha[v >> 6 & 63];
		*o++ = alpha[v & 63];
	}
	if (n - i == 1) {
		uint32_t v = (uint32_t)b->data[i] << 16;
		*o++ = alpha[v >> 18 & 63];
		*o++ = alpha[v >> 12 & 63];
		if (pad) *o++ = '=', *o++ = '=';
	} else if (n - i == 2) {
		uint32_t v = (uint32_t)b->data[i] << 16 | (uint32_t)b->data[i + 1] << 8;
		*o++ = alpha[v >> 18 & 63];
		*o++ = alpha[v >> 12 & 63];
		*o++ = alpha[v >> 6 & 63];
		if (pad) *o++ = '=';
	}
	return s;
}

static ak_val b64_encode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.p = b64(BYTES(0), std_alpha, true)};
}

static ak_val b64url_encode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return (ak_val){.p = b64(BYTES(0), url_alpha, false)};
}

static int b64_val(uint8_t c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+' || c == '-') return 62;
	if (c == '/' || c == '_') return 63;
	return -1;
}

// b64_decode reads either alphabet, padded or not; line breaks and
// spaces are skipped (MIME's). Anything else, or a lone last digit,
// throws.
static ak_val b64_decode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *s = STR(0);
	ak_bytes *out = ak_bytes_new(NULL, (size_t)s->len / 4 * 3 + 3);
	size_t o = 0;
	uint32_t acc = 0;
	int bits = 0, pads = 0;
	for (uint32_t i = 0; i < s->len; i++) {
		uint8_t c = (uint8_t)s->data[i];
		if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
		if (c == '=') {
			pads++;
			continue;
		}
		int v = b64_val(c);
		if (v < 0 || pads) {
			ak_release(out);
			ak_throw_msg(pads ? "base64: data after the padding at %u" : "base64: '%c' (at %u) isn't base64", pads ? i : (unsigned)c, i);
			return (ak_val){0};
		}
		acc = acc << 6 | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out->data[o++] = (uint8_t)(acc >> bits);
		}
	}
	if (bits >= 6 || (pads && (bits / 2 != pads || pads > 2))) {
		ak_release(out);
		ak_throw_msg("base64: the data ends in the middle of a byte");
		return (ak_val){0};
	}
	out->len = (uint32_t)o;
	return (ak_val){.p = out};
}

static ak_val hex_encode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	static const char digits[] = "0123456789abcdef";
	const ak_bytes *b = BYTES(0);
	size_t len = b ? b->len : 0;
	ak_str *s = ak_str_new(NULL, len * 2);
	for (size_t i = 0; i < len; i++) {
		s->data[2 * i] = digits[b->data[i] >> 4];
		s->data[2 * i + 1] = digits[b->data[i] & 15];
	}
	return (ak_val){.p = s};
}

static int hex_val(uint8_t c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static ak_val hex_decode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *s = STR(0);
	if (s->len % 2) {
		ak_throw_msg("hex: an odd number of digits (%u)", s->len);
		return (ak_val){0};
	}
	ak_bytes *out = ak_bytes_new(NULL, s->len / 2);
	for (uint32_t i = 0; i < s->len; i += 2) {
		int hi = hex_val((uint8_t)s->data[i]), lo = hex_val((uint8_t)s->data[i + 1]);
		if (hi < 0 || lo < 0) {
			ak_release(out);
			ak_throw_msg("hex: '%c' (at %u) isn't a hex digit", hi < 0 ? s->data[i] : s->data[i + 1], hi < 0 ? i : i + 1);
			return (ak_val){0};
		}
		out->data[i / 2] = (uint8_t)(hi << 4 | lo);
	}
	return (ak_val){.p = out};
}

// ---- Base32 ----

static const char b32_std[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static const char b32_hex[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

// base32Encode(data, alphabet, padded): alphabet 0 the standard one, 1
// the extended hex one (std/encoding's Base32's cases, in order).
static ak_val b32_encode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *b = BYTES(0);
	const char *alpha = args[1].i ? b32_hex : b32_std;
	bool pad = args[2].i != 0;
	static const uint8_t tail[5] = {0, 2, 4, 5, 7};  // a last group's digits, by its bytes
	size_t len = b ? b->len : 0;
	size_t out = pad ? (len + 4) / 5 * 8 : len / 5 * 8 + tail[len % 5];
	ak_str *s = ak_str_new(NULL, out);
	char *o = s->data;
	for (size_t i = 0; i < len; i += 5) {
		size_t k = len - i < 5 ? len - i : 5;
		uint64_t v = 0;
		for (size_t j = 0; j < 5; j++) v = v << 8 | (j < k ? b->data[i + j] : 0);
		size_t digits = k == 5 ? 8 : tail[k];
		for (size_t j = 0; j < digits; j++) *o++ = alpha[v >> (35 - 5 * j) & 31];
		if (pad)
			for (size_t j = digits; j < 8; j++) *o++ = '=';
	}
	return (ak_val){.p = s};
}

static int b32_val(uint8_t c, bool hex) {
	if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
	if (hex) {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'A' && c <= 'V') return c - 'A' + 10;
		return -1;
	}
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= '2' && c <= '7') return c - '2' + 26;
	return -1;
}

// base32Decode(text, alphabet) reads either case, padded or not, and
// skips line breaks and spaces, as b64_decode does. Padding, when there's
// any, must fill the last group to 8 digits, with nothing after it; a
// last group of 1, 3 or 6 digits ends in the middle of a byte. The bits
// a last group has past its last byte are ignored (RFC 4648 §3.5 lets a
// decoder take or refuse them; base64Decode takes them too).
static ak_val b32_decode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *s = STR(0);
	bool hex = args[1].i != 0;
	ak_bytes *out = ak_bytes_new(NULL, (size_t)s->len * 5 / 8 + 1);
	size_t o = 0, digits = 0;
	uint32_t acc = 0, pads = 0, pad_at = 0;
	int bits = 0;
	for (uint32_t i = 0; i < s->len; i++) {
		uint8_t c = (uint8_t)s->data[i];
		if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
		if (c == '=') {
			if (!pads) pad_at = i;
			pads++;
			continue;
		}
		int v = b32_val(c, hex);
		if (v < 0 || pads) {
			ak_release(out);
			if (pads) ak_throw_msg("base32: data after the padding at %u", i);
			else ak_throw_msg("base32: '%c' (at %u) isn't base32%s", c, i, hex ? " (the hex alphabet)" : "");
			return (ak_val){0};
		}
		acc = acc << 5 | (uint32_t)v;
		bits += 5;
		digits++;
		if (bits >= 8) {
			bits -= 8;
			out->data[o++] = (uint8_t)(acc >> bits);
		}
	}
	size_t last = digits % 8;
	if (last == 1 || last == 3 || last == 6) {
		ak_release(out);
		ak_throw_msg("base32: the data ends in the middle of a byte");
		return (ak_val){0};
	}
	if (pads && (last == 0 || last + pads != 8)) {
		ak_release(out);
		ak_throw_msg("base32: the padding at %u doesn't fill the last group to 8 digits", pad_at);
		return (ak_val){0};
	}
	out->len = (uint32_t)o;
	return (ak_val){.p = out};
}

// ---- Fixed-width numbers ----

// span checks that data has count bytes at at, throwing what's wrong
// in what's name (a function's, or Reader's method's) when it doesn't.
static bool span(const ak_bytes *b, int64_t at, int64_t count, const ak_str *what) {
	uint32_t len = b ? b->len : 0;
	if (at < 0) {
		ak_throw_msg("encoding: %s at %" PRId64 ": an offset can't be negative", what->data, at);
		return false;
	}
	if (count < 0) {
		ak_throw_msg("encoding: %s of %" PRId64 " bytes: a count can't be negative", what->data, count);
		return false;
	}
	if (at > (int64_t)len || count > (int64_t)len - at) {
		ak_throw_msg("encoding: %s needs %" PRId64 " byte%s at %" PRId64 ", and the data has %u", what->data, count,
		             count == 1 ? "" : "s", at, len);
		return false;
	}
	return true;
}

// readUint(data, at, size, endian, what): the size (1 to 8) bytes at at
// as an unsigned number, zero-extended; endian 1 is big-endian (Endian's
// cases, in order). The Asuka side truncates it to its type.
static ak_val read_uint(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *b = BYTES(0);
	int64_t at = args[1].i, size = args[2].i;
	if (!span(b, at, size, STR(4))) return (ak_val){0};
	const uint8_t *p = b->data + at;
	uint64_t v = 0;
	if (args[3].i)
		for (int64_t i = 0; i < size; i++) v = v << 8 | p[i];
	else
		for (int64_t i = size; i-- > 0;) v = v << 8 | p[i];
	return (ak_val){.i = (int64_t)v};
}

// writeUint(value, size, endian): value's low size bytes.
static ak_val write_uint(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	uint64_t v = (uint64_t)args[0].i;
	int64_t size = args[1].i;
	ak_bytes *b = ak_bytes_new(NULL, (size_t)size);
	for (int64_t i = 0; i < size; i++) b->data[args[2].i ? size - 1 - i : i] = (uint8_t)(v >> (8 * i));
	return (ak_val){.p = b};
}

// take(data, at, count, what): the count bytes at at (Reader.bytes).
static ak_val take(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *b = BYTES(0);
	if (!span(b, args[1].i, args[2].i, STR(3))) return (ak_val){0};
	return (ak_val){.p = ak_bytes_new(args[2].i ? b->data + args[1].i : NULL, (size_t)args[2].i)};
}

// need(data, at, count, what) throws when data hasn't count bytes at at
// (Reader.skip).
static ak_val need(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	span(BYTES(0), args[1].i, args[2].i, STR(3));
	return (ak_val){0};
}

// join(parts): the parts one after another, in one allocation (Writer.data).
static ak_val join(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_arr *a = args[0].p;
	uint64_t total = 0;
	for (uint32_t i = 0; a && i < a->len; i++) {
		const ak_bytes *p = a->data[i].p;
		total += p ? p->len : 0;
	}
	if (total > UINT32_MAX) {
		ak_throw_msg("encoding: Writer.data: %" PRIu64 " bytes, more than bytes can hold (4 GiB)", total);
		return (ak_val){0};
	}
	ak_bytes *out = ak_bytes_new(NULL, (size_t)total);
	size_t o = 0;
	for (uint32_t i = 0; a && i < a->len; i++) {
		const ak_bytes *p = a->data[i].p;
		if (p && p->len) memcpy(out->data + o, p->data, p->len);
		o += p ? p->len : 0;
	}
	return (ak_val){.p = out};
}

// ---- IEEE 754 bits ----

static ak_val f32_bits(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	float f = (float)args[0].f;  // rounded to nearest, ties to even
	uint32_t u;
	memcpy(&u, &f, sizeof u);
	return (ak_val){.i = u};
}

static ak_val f32_from_bits(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	uint32_t u = (uint32_t)args[0].i;
	float f;
	memcpy(&f, &u, sizeof f);
	return (ak_val){.f = (double)f};
}

static ak_val f64_bits(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	int64_t i;
	memcpy(&i, &args[0].f, sizeof i);
	return (ak_val){.i = i};
}

static ak_val f64_from_bits(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	double f;
	memcpy(&f, &args[0].i, sizeof f);
	return (ak_val){.f = f};
}

// ---- Varints (LEB128, as Go's encoding/binary) ----

// varintWrite(value, signed): value as a varint, 1 to 10 bytes; signed,
// value is an int, zigzag-encoded first (0, -1, 1, -2 … as 0, 1, 2, 3 …).
static ak_val varint_write(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	uint64_t v = (uint64_t)args[0].i;
	if (args[1].i) v = args[0].i < 0 ? ~(v << 1) : v << 1;
	uint8_t buf[10];
	size_t k = 0;
	while (v >= 0x80) {
		buf[k++] = (uint8_t)(v | 0x80);
		v >>= 7;
	}
	buf[k++] = (uint8_t)v;
	return (ak_val){.p = ak_bytes_new(buf, k)};
}

// varintRead(data, at, signed, what): [the value, how many bytes it
// took], an array of two u64s (the Asuka side makes the tuple). A varint
// that runs past the data's end throws, as one past 64 bits does (more
// than 10 bytes, or a 10th over 1), as Go's Uvarint refuses them.
static ak_val varint_read(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_bytes *b = BYTES(0);
	int64_t at = args[1].i;
	const ak_str *what = STR(3);
	if (!span(b, at, 1, what)) return (ak_val){0};
	uint32_t len = b->len;
	uint64_t v = 0;
	int64_t i = 0;
	for (;; i++) {
		if (at > (int64_t)len - 1 - i) {
			ak_throw_msg("encoding: %s at %" PRId64 ": the data (%u byte%s) ends inside the varint", what->data, at, len,
			             len == 1 ? "" : "s");
			return (ak_val){0};
		}
		uint8_t c = b->data[at + i];
		if (i == 9 && c > 1) {
			ak_throw_msg("encoding: %s at %" PRId64 ": the varint is past 64 bits", what->data, at);
			return (ak_val){0};
		}
		v |= (uint64_t)(c & 0x7f) << (7 * i);
		if (c < 0x80) break;
	}
	if (args[2].i) v = v & 1 ? ~(v >> 1) : v >> 1;
	ak_arr *r = ak_arr_new(AK_K_INT, NULL, 2);
	r->data[0].i = (int64_t)v;
	r->data[1].i = i + 1;
	r->len = 2;
	return (ak_val){.p = r};
}

// ---- UTF-16 ----

static void put16(uint8_t *p, uint32_t u, bool big) {
	p[big ? 0 : 1] = (uint8_t)(u >> 8);
	p[big ? 1 : 0] = (uint8_t)u;
}

// utf16Encode(text, endian, bom): text's UTF-16, a surrogate pair for a
// code point past U+FFFF; U+FEFF first when bom.
static ak_val utf16_encode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const ak_str *s = STR(0);
	bool big = args[1].i != 0, bom = args[2].i != 0;
	size_t units = bom;
	for (size_t i = 0; i < s->len;) units += ak_utf8_decode(s->data, s->len, &i) > 0xffff ? 2 : 1;
	ak_bytes *b = ak_bytes_new(NULL, units * 2);
	uint8_t *o = b->data;
	if (bom) put16(o, 0xfeff, big), o += 2;
	for (size_t i = 0; i < s->len;) {
		uint32_t c = ak_utf8_decode(s->data, s->len, &i);
		if (c > 0xffff) {
			c -= 0x10000;
			put16(o, 0xd800 | c >> 10, big), o += 2;
			put16(o, 0xdc00 | (c & 0x3ff), big), o += 2;
		} else {
			put16(o, c, big), o += 2;
		}
	}
	return (ak_val){.p = b};
}

// utf16_next reads b's UTF-16 in big's order from *pos: the next code
// point (U+FFFD for an unpaired surrogate or an odd last byte), moving
// *pos past it; *bad is set for those.
static uint32_t utf16_next(const ak_bytes *b, size_t *pos, bool big, bool *bad) {
	size_t i = *pos, len = b->len;
	*bad = false;
	if (len - i < 2) {
		*pos = len;
		*bad = true;
		return 0xfffd;
	}
	uint32_t u = big ? (uint32_t)b->data[i] << 8 | b->data[i + 1] : (uint32_t)b->data[i + 1] << 8 | b->data[i];
	*pos = i + 2;
	if (u < 0xd800 || u > 0xdfff) return u;
	if (u <= 0xdbff && len - i >= 4) {
		uint32_t l = big ? (uint32_t)b->data[i + 2] << 8 | b->data[i + 3] : (uint32_t)b->data[i + 3] << 8 | b->data[i + 2];
		if (l >= 0xdc00 && l <= 0xdfff) {
			*pos = i + 4;
			return 0x10000 + ((u - 0xd800) << 10) + (l - 0xdc00);
		}
	}
	if (u <= 0xdbff && len - i == 3) *pos = len;  // a lead surrogate, then half a unit: one error, as WHATWG's decoder has
	*bad = true;
	return 0xfffd;
}

// utf16Decode(data, endian) and utf16DecodeStrict: a BOM at the start
// says the order (and is dropped), else endian does. Lossy, what isn't
// UTF-16 becomes U+FFFD (as WHATWG's TextDecoder, Go's utf16.Decode);
// strict, it throws, saying where.
static ak_val utf16_decode_with(ak_val *args, bool strict) {
	const ak_bytes *b = BYTES(0);
	if (!b || b->len == 0) return (ak_val){.p = ak_str_empty()};
	bool big = args[1].i != 0;
	size_t start = 0;
	if (b->len >= 2 && ((b->data[0] == 0xfe && b->data[1] == 0xff) || (b->data[0] == 0xff && b->data[1] == 0xfe))) {
		big = b->data[0] == 0xfe;
		start = 2;
	}
	size_t out = 0;
	bool bad;
	char tmp[4];
	for (size_t i = start; i < b->len;) {
		size_t at = i;
		uint32_t c = utf16_next(b, &i, big, &bad);
		if (bad && strict) {
			if (b->len - at < 2)
				ak_throw_msg("encoding: utf16DecodeStrict: an odd number of bytes (%u): the last, at %zu, is half a code unit", b->len, at);
			else
				ak_throw_msg("encoding: utf16DecodeStrict: an unpaired surrogate (0x%04x) at byte %zu",
				             big ? (unsigned)b->data[at] << 8 | b->data[at + 1] : (unsigned)b->data[at + 1] << 8 | b->data[at], at);
			return (ak_val){0};
		}
		out += ak_utf8_encode(c, tmp);
	}
	ak_str *s = ak_str_new(NULL, out);
	char *o = s->data;
	for (size_t i = start; i < b->len;) o += ak_utf8_encode(utf16_next(b, &i, big, &bad), o);
	return (ak_val){.p = s};
}

static ak_val utf16_decode(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return utf16_decode_with(args, false);
}

static ak_val utf16_decode_strict(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	return utf16_decode_with(args, true);
}

const ak_lib_entry ak_std_encoding[] = {
	{"encoding.base64Encode", b64_encode},
	{"encoding.base64URLEncode", b64url_encode},
	{"encoding.base64Decode", b64_decode},
	{"encoding.hexEncode", hex_encode},
	{"encoding.hexDecode", hex_decode},
	{"encoding.base32Encode", b32_encode},
	{"encoding.base32Decode", b32_decode},
	{"encoding.readUint", read_uint},
	{"encoding.writeUint", write_uint},
	{"encoding.take", take},
	{"encoding.need", need},
	{"encoding.join", join},
	{"encoding.float32Bits", f32_bits},
	{"encoding.float32FromBits", f32_from_bits},
	{"encoding.float64Bits", f64_bits},
	{"encoding.float64FromBits", f64_from_bits},
	{"encoding.varintWrite", varint_write},
	{"encoding.varintRead", varint_read},
	{"encoding.utf16Encode", utf16_encode},
	{"encoding.utf16Decode", utf16_decode},
	{"encoding.utf16DecodeStrict", utf16_decode_strict},
	{NULL, NULL},
};
