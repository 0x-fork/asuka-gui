// symbols.h: the Material Symbols the Linux host draws an SF Symbol name
// with where the icon theme has none (symbols_gen.c, which
// platform/linux/gen_symbols.go writes), and what screen readers say for
// them.
#ifndef AK_LINUX_SYMBOLS_H
#define AK_LINUX_SYMBOLS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	const char *name;
	uint16_t path;  // its lx_material_paths index
} lx_symbol_path;

typedef struct {
	const char *name, *label;
} lx_symlabel;

extern const char *const lx_material_paths[];
extern const lx_symbol_path lx_material_symbols[];
extern const size_t lx_material_symbol_count;
extern const lx_symlabel lx_symbol_labels[];
extern const size_t lx_symbol_label_count;

#endif
