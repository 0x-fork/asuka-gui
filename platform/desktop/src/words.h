// words.h: the generated tables of Asuka's own words (words_gen.c).
#ifndef DSK_WORDS_H
#define DSK_WORDS_H

#include "words_gen.h"

typedef struct {
	const char *tag;  // the catalog's language: en, pt-PT, zh-Hans
	const char *words[DSK_WORD_KEYS];  // by key, NULL where it has none
} dsk_word_lang;

extern const char *const dsk_word_keys[DSK_WORD_KEYS];
extern const dsk_word_lang dsk_word_langs[DSK_WORD_LANGS];

#endif
