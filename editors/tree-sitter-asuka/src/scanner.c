// The tokens of Asuka's that a regular expression can't make, for the
// tree-sitter grammar:
//
//   - a newline that ends an item, a member or a statement, as the
//     language's scanner says (lang/syntax/scanner.go, GRAMMAR.md's
//     "Newlines"): only where the grammar may end one, and not before a
//     line that continues this one (one that starts with a binary
//     operator, with .name (decision 26), or with else or catch after a
//     } (decision 6));
//   - a """ string's text, which holds " and "" as they are;
//   - a raw string, r"…", r#"…"#, r"""…""": as many #s after it as
//     before;
//   - the first > of >> and >>= where a shift may go: each > is a token
//     (decision 24), so a type's Box<Box<int>> closes twice.

#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>

enum TokenType {
  NEWLINE,
  MULTILINE_STRING_CONTENT,
  RAW_STRING_LITERAL,
  SHIFT_GT,
  ERROR_SENTINEL,
};

void *tree_sitter_asuka_external_scanner_create(void) { return NULL; }

void tree_sitter_asuka_external_scanner_destroy(void *payload) { (void)payload; }

unsigned tree_sitter_asuka_external_scanner_serialize(void *payload, char *buffer) {
  (void)payload;
  (void)buffer;
  return 0;
}

void tree_sitter_asuka_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  (void)payload;
  (void)buffer;
  (void)length;
}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline bool is_ident_start(int32_t c) {
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c > 0x7f;
}

static inline bool is_ident_continue(int32_t c) {
  return is_ident_start(c) || (c >= '0' && c <= '9');
}

// What skip_blank stopped at.
enum Blank {
  BLANK_END,         // the next line's first character
  BLANK_SLASH,       // past a / that starts no comment
  BLANK_UNTERMINATED // a block comment that doesn't end
};

// skip_blank moves past spaces, newlines and comments, up to what the
// next line starts with.
static enum Blank skip_blank(TSLexer *lexer) {
  for (;;) {
    int32_t c = lexer->lookahead;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0xfeff) {
      advance(lexer);
      continue;
    }
    if (c != '/') {
      return BLANK_END;
    }
    advance(lexer);
    if (lexer->lookahead == '/') {
      while (lexer->lookahead != '\n' && !lexer->eof(lexer)) {
        advance(lexer);
      }
      continue;
    }
    if (lexer->lookahead != '*') {
      return BLANK_SLASH;
    }
    advance(lexer);
    for (;;) {
      if (lexer->eof(lexer)) {
        return BLANK_UNTERMINATED;
      }
      if (lexer->lookahead == '*') {
        advance(lexer);
        if (lexer->lookahead == '/') {
          advance(lexer);
          break;
        }
        continue;
      }
      advance(lexer);
    }
  }
}

// word_is reports whether the word at the lexer is w, and moves past it.
static bool word_is(TSLexer *lexer, const char *w) {
  for (; *w; w++) {
    if (lexer->lookahead != *w) {
      return false;
    }
    advance(lexer);
  }
  return !is_ident_continue(lexer->lookahead);
}

// dot_line_is_own reports, at the name after a line's leading ., whether
// the line starts something of its own rather than continuing the line
// before as a member (decision 26): a match arm (a => on it, outside
// brackets), or a block's value alone on its last line (if c { log() ⏎
// .red }).
static bool dot_line_is_own(TSLexer *lexer) {
  while (is_ident_continue(lexer->lookahead)) {
    advance(lexer);
  }
  bool alone = true;
  int depth = 0;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == '\n' && depth <= 0) {
      break;
    }
    if (c == '/') {
      advance(lexer);
      if (lexer->lookahead == '/') {
        while (lexer->lookahead != '\n' && !lexer->eof(lexer)) {
          advance(lexer);
        }
        break;
      }
      alone = false;
      continue;
    }
    if (c == '"' || c == '\'') {
      // A string's or a char's text: its brackets and arrows aren't the
      // line's.
      int32_t quote = c;
      advance(lexer);
      while (lexer->lookahead != quote && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
        if (lexer->lookahead == '\\') {
          advance(lexer);
        }
        advance(lexer);
      }
      advance(lexer);
      alone = false;
      continue;
    }
    if (c == '}' && depth == 0) {
      // The } of the block the line is in.
      return alone;
    }
    if (c != ' ' && c != '\t' && c != '\r') {
      alone = false;
    }
    switch (c) {
      case '(':
      case '[':
      case '{':
        depth++;
        break;
      case ')':
      case ']':
      case '}':
        depth--;
        break;
      case '=':
        advance(lexer);
        if (lexer->lookahead == '>' && depth == 0) {
          return true;
        }
        continue;
    }
    advance(lexer);
  }
  if (!alone) {
    return false;
  }
  // Alone on its line: its own when the block's } follows.
  return skip_blank(lexer) == BLANK_END && lexer->lookahead == '}';
}

// continues reports whether the line the lexer is at continues the line
// before: it starts with a binary operator that can't start an
// expression, with .name, or with else or catch.
static bool continues(TSLexer *lexer, enum Blank blank) {
  if (blank == BLANK_SLASH) {
    return lexer->lookahead != '='; // a division, not /=
  }
  int32_t c = lexer->lookahead;
  switch (c) {
    case '.':
      advance(lexer);
      if (lexer->lookahead == '.') {
        return true; // .. and ..<
      }
      if (is_ident_start(lexer->lookahead)) {
        return !dot_line_is_own(lexer);
      }
      return false;
    case '?':
      advance(lexer);
      if (lexer->lookahead == '.') {
        return true;
      }
      if (lexer->lookahead == '?') {
        advance(lexer);
        return lexer->lookahead != '=';
      }
      return false;
    case '*':
      advance(lexer);
      if (lexer->lookahead == '*') {
        advance(lexer);
      }
      return lexer->lookahead != '=';
    case '+':
    case '%':
    case '^':
      advance(lexer);
      return lexer->lookahead != '=';
    case '&':
    case '|':
      advance(lexer);
      if (lexer->lookahead == c) {
        return true; // && ||
      }
      if (c == '&' && (lexer->lookahead == '+' || lexer->lookahead == '-' || lexer->lookahead == '*')) {
        advance(lexer);
      }
      return lexer->lookahead != '=';
    case '=':
    case '!':
      advance(lexer);
      return lexer->lookahead == '=';
    case '<':
    case '>':
      advance(lexer);
      if (lexer->lookahead == c) {
        advance(lexer);
        return lexer->lookahead != '=';
      }
      return true;
    case 'e':
      return word_is(lexer, "else");
    case 'c':
      return word_is(lexer, "catch");
  }
  return false;
}

static bool scan_newline(TSLexer *lexer) {
  advance(lexer); // the \n
  lexer->mark_end(lexer);
  // Runs of newlines are one.
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r' ||
         lexer->lookahead == '\n') {
    bool nl = lexer->lookahead == '\n';
    advance(lexer);
    if (nl) {
      lexer->mark_end(lexer);
    }
  }
  enum Blank blank = skip_blank(lexer);
  if (blank == BLANK_UNTERMINATED) {
    return false;
  }
  if (blank == BLANK_END && lexer->eof(lexer)) {
    lexer->result_symbol = NEWLINE;
    return true;
  }
  if (continues(lexer, blank)) {
    return false;
  }
  lexer->result_symbol = NEWLINE;
  return true;
}

// scan_multiline_content scans a """ string's text up to its closing
// quotes, an escape or an interpolation.
static bool scan_multiline_content(TSLexer *lexer) {
  bool has_content = false;
  for (;;) {
    lexer->mark_end(lexer);
    int32_t c = lexer->lookahead;
    if (lexer->eof(lexer) || c == '\\' || c == '{') {
      break;
    }
    if (c == '"') {
      advance(lexer);
      if (lexer->lookahead == '"') {
        advance(lexer);
        if (lexer->lookahead == '"') {
          break; // the closing """, the token's end marked before it
        }
      }
      has_content = true;
      continue;
    }
    advance(lexer);
    has_content = true;
  }
  if (!has_content) {
    return false;
  }
  lexer->result_symbol = MULTILINE_STRING_CONTENT;
  return true;
}

// scan_raw_string scans r"…", r#"…"#, r"""…""" at its r.
static bool scan_raw_string(TSLexer *lexer) {
  advance(lexer); // r
  unsigned hashes = 0;
  while (lexer->lookahead == '#') {
    hashes++;
    advance(lexer);
  }
  if (lexer->lookahead != '"') {
    return false;
  }
  advance(lexer);
  unsigned quotes = 1;
  if (lexer->lookahead == '"') {
    advance(lexer);
    if (lexer->lookahead == '"') {
      advance(lexer);
      quotes = 3;
    } else {
      // r"": empty, unless hashes are to close it.
      if (hashes == 0) {
        lexer->result_symbol = RAW_STRING_LITERAL;
        return true;
      }
      unsigned n = 0;
      while (n < hashes && lexer->lookahead == '#') {
        n++;
        advance(lexer);
      }
      if (n == hashes) {
        lexer->result_symbol = RAW_STRING_LITERAL;
        return true;
      }
    }
  }
  for (;;) {
    if (lexer->eof(lexer)) {
      lexer->result_symbol = RAW_STRING_LITERAL;
      return true;
    }
    if (lexer->lookahead == '\n' && quotes == 1) {
      // Unterminated: the line ends it.
      lexer->result_symbol = RAW_STRING_LITERAL;
      return true;
    }
    if (lexer->lookahead != '"') {
      advance(lexer);
      continue;
    }
    unsigned q = 0;
    while (q < quotes && lexer->lookahead == '"') {
      q++;
      advance(lexer);
    }
    if (q < quotes) {
      continue;
    }
    unsigned n = 0;
    while (n < hashes && lexer->lookahead == '#') {
      n++;
      advance(lexer);
    }
    if (n == hashes) {
      lexer->result_symbol = RAW_STRING_LITERAL;
      return true;
    }
  }
}

bool tree_sitter_asuka_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  (void)payload;
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }
  if (valid_symbols[MULTILINE_STRING_CONTENT]) {
    return scan_multiline_content(lexer);
  }
  if (valid_symbols[NEWLINE]) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r') {
      skip(lexer);
    }
    if (lexer->lookahead == '\n') {
      return scan_newline(lexer);
    }
  }
  if (valid_symbols[SHIFT_GT]) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r' ||
           lexer->lookahead == '\n') {
      skip(lexer);
    }
    if (lexer->lookahead == '>') {
      advance(lexer);
      lexer->mark_end(lexer);
      if (lexer->lookahead == '>') {
        lexer->result_symbol = SHIFT_GT;
        return true;
      }
      return false;
    }
  }
  if (valid_symbols[RAW_STRING_LITERAL]) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r' ||
           lexer->lookahead == '\n') {
      skip(lexer);
    }
    if (lexer->lookahead == 'r') {
      return scan_raw_string(lexer);
    }
  }
  return false;
}
