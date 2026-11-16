// encoding.c: asuka/encoding's builtins: Base64 (RFC 4648, the standard
// alphabet padded; the URL alphabet unpadded) and hexadecimal.
#include "std/std.h"

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

const ak_lib_entry ak_std_encoding[] = {
	{"encoding.base64Encode", b64_encode},
	{"encoding.base64URLEncode", b64url_encode},
	{"encoding.base64Decode", b64_decode},
	{"encoding.hexEncode", hex_encode},
	{"encoding.hexDecode", hex_decode},
	{NULL, NULL},
};
