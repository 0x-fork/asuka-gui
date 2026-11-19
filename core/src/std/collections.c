// collections.c: std/collections' native fns: its B-trees made (rt.h's
// ak_tree; their methods are the runtime's, rt/tree.c's tree.*). A
// generic one gets its type arguments' descriptors (json.h's) after its
// parameters: what a tree orders its keys by, and their and its values'
// kinds.
#include "std/std.h"

#define DESC(i) (((const ak_str *)args[i].p)->data)

// collections.newTree(K, V) -> BTree<K, V>: an empty map's.
static ak_val new_tree(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const char *k = DESC(0);
	return (ak_val){.p = ak_tree_new(ak_tree_desc_mode(k), ak_tree_desc_kind(k), ak_tree_desc_kind(DESC(1)))};
}

// collections.newSet(T) -> BTree<T, bool>: an empty set's, of keys only.
static ak_val new_set(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const char *k = DESC(0);
	return (ak_val){.p = ak_tree_new(ak_tree_desc_mode(k), ak_tree_desc_kind(k), AK_K_VOID)};
}

const ak_lib_entry ak_std_collections[] = {
	{"collections.newTree", new_tree},
	{"collections.newSet", new_set},
	{NULL, NULL},
};
