// encoding.c: std/encoding's builtins: Base64 (RFC 4648, the standard
// alphabet padded; the URL alphabet unpadded) and hexadecimal.
#include "std/std.h"

#define BYTES(i) ((ak_bytes *)args[i].p)
#define STR(i) ((ak_str *)args[i].p)

static const char std_alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char url_alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// Tables made once (any thread: racing threads make the same ones):
// two Base64 digits for each 12 bits (each alphabet's), two hex digits
// for each byte, and each character's Base64 value at each of a group's
// four places (bit 31 set: not a digit).
static uint16_t enc_std[4096], enc_url[4096], enc_hex[256];
static uint32_t dec_b64[4][256];
static uint8_t dec_hex[256];
static int tables_made;

static void make_tables(void) {
	if (__atomic_load_n(&tables_made, __ATOMIC_ACQUIRE)) return;
	for (int i = 0; i < 4096; i++) {
		char a[2] = {std_alpha[i >> 6], std_alpha[i & 63]}, b[2] = {url_alpha[i >> 6], url_alpha[i & 63]};
		memcpy(&enc_std[i], a, 2);
		memcpy(&enc_url[i], b, 2);
	}
	static const char digits[] = "0123456789abcdef";
	for (int i = 0; i < 256; i++) {
		char h[2] = {digits[i >> 4], digits[i & 15]};
		memcpy(&enc_hex[i], h, 2);
		int v = i >= 'A' && i <= 'Z' ? i - 'A' : i >= 'a' && i <= 'z' ? i - 'a' + 26 : i >= '0' && i <= '9' ? i - '0' + 52 :
		        i == '+' || i == '-' ? 62 : i == '/' || i == '_' ? 63 : -1;
		for (int k = 0; k < 4; k++) dec_b64[k][i] = v < 0 ? 0x80000000u : (uint32_t)v << (18 - 6 * k);
		dec_hex[i] = i >= '0' && i <= '9' ? (uint8_t)(i - '0') : i >= 'a' && i <= 'f' ? (uint8_t)(i - 'a' + 10) : i >= 'A' && i <= 'F' ? (uint8_t)(i - 'A' + 10) : 0xff;
	}
	__atomic_store_n(&tables_made, 1, __ATOMIC_RELEASE);
}

static ak_str *b64(const ak_bytes *b, const char *alpha, bool pad) {
	make_tables();
	const uint16_t *t = alpha == std_alpha ? enc_std : enc_url;
	size_t n = b ? b->len : 0;
	size_t out = pad ? (n + 2) / 3 * 4 : n / 3 * 4 + (n % 3 ? n % 3 + 1 : 0);
	ak_str *s = ak_str_new(NULL, out);
	char *o = s->data;
	const uint8_t *p = n ? b->data : NULL;
	size_t i = 0;
	for (; i + 3 <= n; i += 3, o += 4) {
		uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
		memcpy(o, &t[v >> 12], 2);
		memcpy(o + 2, &t[v & 4095], 2);
	}
	if (n - i == 1) {
		uint32_t v = (uint32_t)p[i] << 16;
		*o++ = alpha[v >> 18 & 63];
		*o++ = alpha[v >> 12 & 63];
		if (pad) *o++ = '=', *o++ = '=';
	} else if (n - i == 2) {
		uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8;
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
	make_tables();
	const ak_str *s = STR(0);
	ak_bytes *out = ak_bytes_new(NULL, (size_t)s->len / 4 * 3 + 3);
	size_t o = 0;
	uint32_t acc = 0, i = 0;
	int bits = 0, pads = 0;
	// Four digits at a time while they're all digits; the rest (spaces,
	// padding, what isn't Base64) a character at a time.
	const uint8_t *in = (const uint8_t *)s->data;
	uint8_t *dst = out->data;
	for (; i + 4 <= s->len; i += 4, o += 3) {
		uint32_t v = dec_b64[0][in[i]] | dec_b64[1][in[i + 1]] | dec_b64[2][in[i + 2]] | dec_b64[3][in[i + 3]];
		if (v >> 31) break;
		dst[o] = (uint8_t)(v >> 16), dst[o + 1] = (uint8_t)(v >> 8), dst[o + 2] = (uint8_t)v;
	}
	for (; i < s->len; i++) {
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
	AK_UNUSED(digits);
	make_tables();
	ak_str *s = ak_str_new(NULL, len * 2);
	const uint8_t *p = len ? b->data : NULL;
	char *o = s->data;
	for (size_t i = 0; i < len; i++) memcpy(o + 2 * i, &enc_hex[p[i]], 2);
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
	make_tables();
	ak_bytes *out = ak_bytes_new(NULL, s->len / 2);
	const uint8_t *in = (const uint8_t *)s->data;
	uint8_t *dst = out->data;
	uint32_t i = 0;
	for (; i < s->len; i += 2) {
		uint8_t h = dec_hex[in[i]], l = dec_hex[in[i + 1]];
		if ((h | l) & 0x80) break;
		dst[i / 2] = (uint8_t)(h << 4 | l);
	}
	for (; i < s->len; i += 2) {
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

const ak_lib_entry ak_std_encoding[] = {
	{"encoding.base64Encode", b64_encode},
	{"encoding.base64URLEncode", b64url_encode},
	{"encoding.base64Decode", b64_decode},
	{"encoding.hexEncode", hex_encode},
	{"encoding.hexDecode", hex_decode},
	{NULL, NULL},
};
