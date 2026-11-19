// collections.c: std/collections' native fns: its B-trees made (rt.h's
// ak_tree; their methods are the runtime's, rt/tree.c's tree.*). A
// generic one gets its type arguments' descriptors (json.h's) after its
// parameters: what a tree orders its keys by, and their and its values'
// kinds.
#include "std/std.h"
#include "rt/json.h"

#define DESC(i) (((const ak_str *)args[i].p)->data)

void ak_vm_json_types(ak_json_types *out);  // vm/json_lib.c: the running module's shapes

// with_order is t, whose keys are of descriptor d, ordered by their
// type's compare when it's an enum without payloads that has one (its
// shape's #cmp, found by its name in the running module: the keys are
// ints, which carry no shape); a struct's or an enum's keys are by their
// own shape's.
static ak_tree *with_order(ak_tree *t, const char *d) {
	if (d[0] != 'e') return t;
	const char *end = strchr(d + 1, ';');
	ak_json_types j;
	ak_vm_json_types(&j);
	const ak_shape *s = j.shape && end ? j.shape(j.ctx, d + 1, (size_t)(end - d - 1)) : NULL;
	if (s && ak_shape_has(s, AK_HOOK_CMP)) {
		t->mode = AK_TM_CMP;
		t->kshape = s;
	}
	return t;
}

// collections.newTree(K, V) -> BTree<K, V>: an empty map's.
static ak_val new_tree(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const char *k = DESC(0);
	return (ak_val){.p = with_order(ak_tree_new(ak_tree_desc_mode(k), ak_tree_desc_kind(k), ak_tree_desc_kind(DESC(1))), k)};
}

// collections.newSet(T) -> BTree<T, bool>: an empty set's, of keys only.
static ak_val new_set(ak_val *args, uint32_t n) {
	AK_UNUSED(n);
	const char *k = DESC(0);
	return (ak_val){.p = with_order(ak_tree_new(ak_tree_desc_mode(k), ak_tree_desc_kind(k), AK_K_VOID), k)};
}

const ak_lib_entry ak_std_collections[] = {
	{"collections.newTree", new_tree},
	{"collections.newSet", new_set},
	{NULL, NULL},
};
