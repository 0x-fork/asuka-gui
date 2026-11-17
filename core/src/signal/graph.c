// graph.c: links between nodes, and alien-signals' push-dirty,
// pull-value propagation.
#include "signal/signal_impl.h"

static ak_graph graph;
ak_graph *ak_sig = &graph;
#define g ak_sig

static ak_link *new_link(void) {
	ak_link *l = g->free_links;
	if (l) {
		g->free_links = l->next_dep;
		l->next_dep = NULL;
		return l;
	}
	return ak_alloc(sizeof *l);
}

static void free_link(ak_link *l) {
	*l = (ak_link){.next_dep = g->free_links};
	g->free_links = l;
}

// ak_sig_track records that sub read dep.
void ak_sig_track(ak_node *dep, ak_comp *sub) {
	if ((dep->flags | sub->node.flags) & AK_F_DISPOSED) return;
	ak_link *prev_dep = sub->deps_tail;
	if (prev_dep && prev_dep->dep == dep) return;
	ak_link *next_dep = prev_dep ? prev_dep->next_dep : sub->deps;
	if (next_dep && next_dep->dep == dep) {
		// Same dep as last run, in the same place: reuse the link.
		next_dep->version = sub->run;
		sub->deps_tail = next_dep;
		return;
	}
	ak_link *prev_sub = dep->subs_tail;
	if (prev_sub && prev_sub->sub == sub && prev_sub->version == sub->run) return; // already read this run
	ak_link *l = new_link();
	l->dep = dep;
	l->sub = sub;
	l->version = sub->run;
	l->prev_dep = prev_dep;
	l->next_dep = next_dep;
	l->prev_sub = prev_sub;
	l->next_sub = NULL;
	if (next_dep) next_dep->prev_dep = l;
	if (prev_dep) prev_dep->next_dep = l;
	else sub->deps = l;
	sub->deps_tail = l;
	if (prev_sub) prev_sub->next_sub = l;
	else dep->subs = l;
	dep->subs_tail = l;
}

// unlink removes l from both lists and returns the next dep of its sub.
static ak_link *unlink(ak_link *l) {
	ak_node *dep = l->dep;
	ak_comp *sub = l->sub;
	ak_link *next = l->next_dep;
	if (next) next->prev_dep = l->prev_dep;
	else sub->deps_tail = l->prev_dep;
	if (l->prev_dep) l->prev_dep->next_dep = next;
	else sub->deps = next;
	if (l->next_sub) l->next_sub->prev_sub = l->prev_sub;
	else dep->subs_tail = l->prev_sub;
	if (l->prev_sub) l->prev_sub->next_sub = l->next_sub;
	else dep->subs = l->next_sub;
	free_link(l);
	// A follow keeps its sources: a value set stands until they change.
	if (!dep->subs && (dep->flags & (AK_F_MEMO | AK_F_FOLLOW)) == AK_F_MEMO) ak_sig_memo_unwatched((ak_comp *)dep);
	return next;
}

void ak_sig_start_tracking(ak_comp *c) {
	c->run++;
	c->deps_tail = NULL;
	c->node.flags &= ~(uint32_t)(AK_F_DIRTY | AK_F_PENDING);
}

// ak_sig_end_tracking drops the deps the run didn't read.
void ak_sig_end_tracking(ak_comp *c) {
	ak_link *l = c->deps_tail ? c->deps_tail->next_dep : c->deps;
	while (l) l = unlink(l);
}

void ak_sig_unlink_all_deps(ak_comp *c) {
	for (ak_link *l = c->deps; l;) l = unlink(l);
}

void ak_sig_unlink_all_subs(ak_node *n) {
	for (ak_link *l = n->subs; l;) {
		ak_link *next = l->next_sub;
		unlink(l);
		l = next;
	}
}

// ak_sig_propagate marks the subscribers in l with flag (dirty for the
// direct subscribers of a written signal, pending further down) and
// queues effects.
void ak_sig_propagate(ak_link *l, uint32_t flag) {
	for (; l; l = l->next_sub) {
		ak_comp *sub = l->sub;
		uint32_t f = sub->node.flags;
		if ((f & (AK_F_DISPOSED | AK_F_DIRTY)) || (flag == AK_F_PENDING && (f & AK_F_PENDING))) continue;
		sub->node.flags = f | flag;
		if (f & AK_F_EFFECT) ak_sig_enqueue(sub);
		else if (!(f & (AK_F_DIRTY | AK_F_PENDING)) && sub->node.subs) ak_sig_propagate(sub->node.subs, AK_F_PENDING);
	}
}

// ak_sig_shallow_propagate turns the pending subscribers of a memo that
// changed into dirty ones.
void ak_sig_shallow_propagate(ak_link *l) {
	for (; l; l = l->next_sub) {
		ak_comp *sub = l->sub;
		if ((sub->node.flags & (AK_F_PENDING | AK_F_DIRTY)) == AK_F_PENDING) sub->node.flags |= AK_F_DIRTY;
	}
}

// ak_sig_check_dirty reports whether a pending computation really must
// recompute: whether a memo it depends on changed value. It brings those
// memos up to date on the way. c is kept alive by the caller.
//
// A memo's update is code that may dispose things: the memo itself, c,
// or what holds c (another of c's memos letting go of its sources). Each
// memo is held across its update, and the walk goes on from l only if l
// still links c to it; else it starts again from c's deps (those it
// brought up to date are no longer dirty), or stops if c was disposed.
bool ak_sig_check_dirty(ak_comp *c) {
	for (ak_link *l = c->deps; l;) {
		ak_node *dep = l->dep;
		uint32_t f = dep->flags;
		if (!(f & AK_F_MEMO) || (f & AK_F_DISPOSED) || !(f & (AK_F_DIRTY | AK_F_PENDING))) {
			l = l->next_dep;
			continue;
		}
		ak_comp *m = (ak_comp *)dep;
		ak_owner_retain(&m->owner);
		if ((f & AK_F_DIRTY) || ak_sig_check_dirty(m)) {
			if (ak_sig_memo_update(m) && m->node.subs) ak_sig_shallow_propagate(m->node.subs);
		} else {
			m->node.flags &= ~(uint32_t)AK_F_PENDING;
		}
		// Links are pooled, never freed: one unlinked reads as another's
		// (or none's), never as c's to m but in c's own run.
		bool kept = l->dep == dep && l->sub == c;
		ak_owner_release(&m->owner);
		if (c->node.flags & AK_F_DISPOSED) return false;
		if (c->node.flags & AK_F_DIRTY) return true;
		l = kept ? l->next_dep : c->deps;
	}
	return (c->node.flags & AK_F_DIRTY) != 0;
}
