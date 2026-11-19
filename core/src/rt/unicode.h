// unicode.h: what the runtime knows of Unicode, from the tables
// internal/ucdgen generates from a pinned release of the character
// database (unicode_gen.c): UTF-8, grapheme clusters (UAX #29's extended
// ones), the columns a terminal gives text, and case mapping and
// folding without a language's rules (no Turkish dotless i).
//
// Every function here is pure: any thread may call it.
#ifndef AK_UNICODE_H
#define AK_UNICODE_H

#include "base/base.h"

// ak_utf8_decode reads the code point at s[*i] (n bytes in all) and
// moves *i past it; what isn't UTF-8 reads as U+FFFD, one byte.
uint32_t ak_utf8_decode(const char *s, size_t n, size_t *i);
// ak_utf8_encode writes c's UTF-8 into out (4 bytes room); its length.
size_t ak_utf8_encode(uint32_t c, char *out);

// A code point's properties, packed (internal/ucdgen's bits, which must
// agree).
enum {
	AK_GCB_OTHER,
	AK_GCB_CR,
	AK_GCB_LF,
	AK_GCB_CONTROL,
	AK_GCB_EXTEND,
	AK_GCB_ZWJ,
	AK_GCB_RI,
	AK_GCB_PREPEND,
	AK_GCB_SPACING_MARK,
	AK_GCB_L,
	AK_GCB_V,
	AK_GCB_T,
	AK_GCB_LV,
	AK_GCB_LVT,
};
enum { AK_INCB_NONE, AK_INCB_LINKER, AK_INCB_CONSONANT, AK_INCB_EXTEND };
#define AK_UP_GCB(p) ((p) & 0xf)
#define AK_UP_EXTPICT (1u << 4)          // Extended_Pictographic
#define AK_UP_INCB(p) (((p) >> 5) & 3)   // Indic_Conjunct_Break
#define AK_UP_WIDTH(p) (((p) >> 7) & 3)  // its columns alone: 0, 1 or 2
#define AK_UP_EVS (1u << 9)              // has emoji and text variation sequences
#define AK_UP_CASED (1u << 10)
#define AK_UP_CASE_IGNORABLE (1u << 11)
#define AK_UP_LOWER (1u << 12)  // lower, upper and fold change it
#define AK_UP_UPPER (1u << 13)
#define AK_UP_FOLD (1u << 14)

uint16_t ak_uprops(uint32_t cp);

// The Unicode version the tables are of ("18.0.0").
extern const char ak_unicode_version[];

// ak_grapheme_end is where the grapheme cluster that starts at s[i]
// ends (a byte offset; n at the end).
size_t ak_grapheme_end(const char *s, size_t n, size_t i);

// ak_escape_len is the length of the terminal escape sequence at s[i]
// (ESC [ … a CSI's final byte, an OSC to its BEL or ST, ESC and one
// more), 0 when there's none there.
size_t ak_escape_len(const char *s, size_t n, size_t i);

// ak_cluster_width is the columns a terminal gives the grapheme cluster
// s[0:n]: an emoji presentation sequence (VS16, a ZWJ sequence, a
// modifier) two, a text presentation (VS15) one, otherwise its code
// points' widths summed (Hangul's jamo join; marks take none).
int ak_cluster_width(const char *s, size_t n);

// ak_text_width is the columns text takes, its escape sequences none.
int64_t ak_text_width(const char *s, size_t n);

// Case mapping, code point by code point: out gets what cp maps to (1
// to 3 code points), and the count is returned.
typedef enum { AK_CASE_LOWER, AK_CASE_UPPER, AK_CASE_FOLD } ak_case_kind;
int ak_case_map(uint32_t cp, ak_case_kind k, uint32_t out[3]);

// A code point's general category (Unicode's two letters: Lu, Nd…),
// and whether it's White_Space: ak_ucat's.
enum {
	AK_GC_CN, AK_GC_LU, AK_GC_LL, AK_GC_LT, AK_GC_LM, AK_GC_LO, AK_GC_MN, AK_GC_MC, AK_GC_ME, AK_GC_ND,
	AK_GC_NL, AK_GC_NO, AK_GC_PC, AK_GC_PD, AK_GC_PS, AK_GC_PE, AK_GC_PI, AK_GC_PF, AK_GC_PO, AK_GC_SM,
	AK_GC_SC, AK_GC_SK, AK_GC_SO, AK_GC_ZS, AK_GC_ZL, AK_GC_ZP, AK_GC_CC, AK_GC_CF, AK_GC_CS, AK_GC_CO,
};
#define AK_UCAT_GC(c) ((c) & 0x1f)
#define AK_UCAT_SPACE 0x80  // White_Space
uint8_t ak_ucat(uint32_t cp);

// The tables (unicode_gen.c).
typedef struct {
	uint32_t start;
	uint16_t count;
	uint8_t stride;
	int32_t delta;
} ak_case_run;
typedef struct {
	uint32_t cp;
	uint32_t to[3];
} ak_case_full;

extern const uint32_t ak_ucat_count;
extern const uint32_t ak_ucat_starts[];
extern const uint8_t ak_ucat_values[];
extern const uint32_t ak_uprop_count;
extern const uint32_t ak_uprop_starts[];
extern const uint16_t ak_uprop_values[];
extern const uint32_t ak_case_lower_runs_count, ak_case_upper_runs_count, ak_case_fold_runs_count;
extern const ak_case_run ak_case_lower_runs[], ak_case_upper_runs[], ak_case_fold_runs[];
extern const uint32_t ak_case_lower_full_count, ak_case_upper_full_count, ak_case_fold_full_count;
extern const ak_case_full ak_case_lower_full[], ak_case_upper_full[], ak_case_fold_full[];

#endif
