// periodic.c: platform/periodic on Windows: timers while the app runs (out of
// sight too: a Windows app keeps running), as a Mac's
// NSBackgroundActivityScheduler runs them; nothing launches the app.
// "schedule" (string name, u32 seconds, u8 flags: 1 network, 2 charging)
// and "cancel" (the name) keep the app's tasks in a file of the app's data,
// with when each last ran; when one is due, "next" hands the package the
// task (string name, u32 seconds it has), which it reports with "done"
// (string name, u8 ok). A task that wants power waits while the PC runs on
// its battery, one that wants the network while it has none: it's tried
// again a few minutes later.
#include "modules.h"

#include <time.h>

#define RETRY_MS (5 * 60 * 1000)

typedef struct {
	char *name;
	UINT_PTR timer;
	bool running, due;
} task_state;

static struct {
	bool loaded;
	task_state *v;
	uint32_t n;
	char **due;  // in order
	uint32_t ndue;
	wh_replies waiting;
} per;

static char *tasks_path(void) { return dsk_join(H.dirs.data, "asuka-periodic.json"); }

static dsk_json *read_tasks(void) {
	char *p = tasks_path();
	size_t n = 0;
	char *s = dsk_read_file(p, &n);
	free(p);
	dsk_json *j = s ? dsk_json_parse(s, n) : NULL;
	free(s);
	if (j && j->type != DSK_JOBJ) {
		dsk_json_free(j);
		j = NULL;
	}
	return j;
}

typedef struct {
	double every;
	bool network, charging;
	int64_t last;
} task;

static bool get_task(const dsk_json *all, const char *name, task *t) {
	const dsk_json *v = all ? dsk_json_get(all, name) : NULL;
	if (!v || v->type != DSK_JOBJ) return false;
	t->every = dsk_json_num(v, "every", 0);
	t->network = dsk_json_bool(v, "network", false);
	t->charging = dsk_json_bool(v, "charging", false);
	t->last = (int64_t)dsk_json_num(v, "last", 0);
	return true;
}

// write_tasks writes the tasks, with name set to t (NULL: removed).
static void write_tasks(const char *name, const task *t) {
	dsk_json *all = read_tasks();
	dsk_jw w = {0};
	dsk_jw_obj(&w);
	bool put = false;
	for (size_t i = 0; all && i < all->n; i++) {
		if (!strcmp(all->keys[i], name)) {
			if (!t) continue;
			put = true;
			dsk_jw_key(&w, name);
			dsk_jw_obj(&w);
			dsk_jw_key(&w, "every"), dsk_jw_num(&w, t->every);
			dsk_jw_key(&w, "network"), dsk_jw_bool(&w, t->network);
			dsk_jw_key(&w, "charging"), dsk_jw_bool(&w, t->charging);
			dsk_jw_key(&w, "last"), dsk_jw_int(&w, t->last);
			dsk_jw_end(&w);
			continue;
		}
		dsk_jw_key(&w, all->keys[i]);
		dsk_jw_value(&w, &all->kids[i]);
	}
	if (t && !put) {
		dsk_jw_key(&w, name);
		dsk_jw_obj(&w);
		dsk_jw_key(&w, "every"), dsk_jw_num(&w, t->every);
		dsk_jw_key(&w, "network"), dsk_jw_bool(&w, t->network);
		dsk_jw_key(&w, "charging"), dsk_jw_bool(&w, t->charging);
		dsk_jw_key(&w, "last"), dsk_jw_int(&w, t->last);
		dsk_jw_end(&w);
	}
	dsk_jw_end(&w);
	size_t n = 0;
	char *s = dsk_jw_take(&w, &n);
	char *p = tasks_path();
	dsk_write_file(p, s, n);
	free(p);
	free(s);
	dsk_json_free(all);
}

static task_state *state_of(const char *name, bool make) {
	for (uint32_t i = 0; i < per.n; i++)
		if (!strcmp(per.v[i].name, name)) return &per.v[i];
	if (!make) return NULL;
	per.v = wh_realloc(per.v, (per.n + 1) * sizeof *per.v);
	per.v[per.n] = (task_state){.name = wh_strdup(name)};
	return &per.v[per.n++];
}

static void fire(void *ctx);

static void after(const char *name, int64_t ms) {
	task_state *s = state_of(name, true);
	wh_kill_timer(s->timer);
	if (ms < 1000) ms = 1000;
	if (ms > 0x7FFFFFFF) ms = 0x7FFFFFFF;
	s->timer = wh_set_timer((UINT)ms, false, fire, s->name);
}

// schedule sets the task's timer for when it's next due: its interval
// after it last ran (at once, if that's past).
static void schedule(const char *name, const task *t) {
	double every = t->every > 60 ? t->every : 60;
	int64_t due = t->last * 1000 + (int64_t)(every * 1000);
	after(name, due - (int64_t)time(NULL) * 1000);
}

static void load(void) {
	if (per.loaded) return;
	per.loaded = true;
	dsk_json *all = read_tasks();
	for (size_t i = 0; all && i < all->n; i++) {
		task t;
		if (get_task(all, all->keys[i], &t)) schedule(all->keys[i], &t);
	}
	dsk_json_free(all);
}

// run is "next"'s answer: the name and the seconds it has.
static void answer_run(wh_reply *r, const char *name) {
	dsk_json *all = read_tasks();
	task t = {0};
	get_task(all, name, &t);
	dsk_json_free(all);
	ak_buf b = {0};
	ak_buf_u32(&b, (uint32_t)strlen(name));
	ak_buf_put(&b, name, strlen(name));
	ak_buf_u32(&b, t.charging ? 240 : 25);
	wh_reply_ok(r, b.data, b.len);
	ak_buf_free(&b);
}

static void unqueue(const char *name) {
	for (uint32_t i = 0; i < per.ndue; i++)
		if (!strcmp(per.due[i], name)) {
			free(per.due[i]);
			memmove(per.due + i, per.due + i + 1, (per.ndue - i - 1) * sizeof *per.due);
			per.ndue--;
			return;
		}
}

static bool on_ac_power(void) {
	SYSTEM_POWER_STATUS s;
	if (!GetSystemPowerStatus(&s)) return true;
	return s.ACLineStatus != 0;  // 1 online, 255 unknown
}

// fire is a task's turn: it goes to the package, unless what it wants
// isn't there now.
static void fire(void *ctx) {
	const char *name = ctx;
	task_state *s = state_of(name, false);
	if (!s) return;
	s->timer = 0;
	dsk_json *all = read_tasks();
	task t;
	bool kept = get_task(all, name, &t);
	dsk_json_free(all);
	if (!kept) return;
	if ((t.charging && !on_ac_power()) || (t.network && !wh_online()) || s->running) {
		after(name, RETRY_MS);
		return;
	}
	s->running = true;
	wh_reply *r;
	while ((r = wh_replies_take(&per.waiting))) {
		if (wh_reply_gone(r)) {
			wh_reply_nil(r);
			continue;
		}
		answer_run(r, name);
		return;
	}
	per.due = wh_realloc(per.due, (per.ndue + 1) * sizeof *per.due);
	per.due[per.ndue++] = wh_strdup(name);
}

static char *read_name(const uint8_t *a, size_t n, size_t *used) {
	if (n < 4 || wh_u32(a) > n - 4) {
		*used = n;
		return wh_strdup("");
	}
	uint32_t l = wh_u32(a);
	*used = 4 + l;
	return wh_strndup((const char *)a + 4, l);
}

static void m_schedule(wh_reply *r, const uint8_t *a, size_t n) {
	size_t used;
	char *name = read_name(a, n, &used);
	if (!*name || n - used < 5) {
		free(name);
		wh_reply_fail(r, "bad args");
		return;
	}
	uint32_t secs = wh_u32(a + used);
	uint8_t flags = a[used + 4];
	load();
	dsk_json *all = read_tasks();
	task old = {0};
	get_task(all, name, &old);
	dsk_json_free(all);
	task t = {.every = secs, .network = flags & 1, .charging = (flags & 2) != 0, .last = old.last ? old.last : (int64_t)time(NULL)};
	write_tasks(name, &t);
	schedule(name, &t);
	free(name);
	wh_reply_nil(r);
}

static void m_cancel(wh_reply *r, const uint8_t *a, size_t n) {
	char *name = wh_strndup((const char *)a, n);
	write_tasks(name, NULL);
	task_state *s = state_of(name, false);
	if (s) {
		wh_kill_timer(s->timer);
		s->timer = 0;
		s->running = false;
	}
	unqueue(name);
	free(name);
	wh_reply_nil(r);
}

static void m_next(wh_reply *r, const uint8_t *a, size_t n) {
	(void)a, (void)n;
	load();
	if (per.ndue) {
		char *name = per.due[0];
		memmove(per.due, per.due + 1, (per.ndue - 1) * sizeof *per.due);
		per.ndue--;
		answer_run(r, name);
		free(name);
		return;
	}
	wh_replies_add(&per.waiting, r);
}

// done: a task reported: it's due again an interval after (after it ran,
// if it went well; else from now).
static void m_done(wh_reply *r, const uint8_t *a, size_t n) {
	size_t used;
	char *name = read_name(a, n, &used);
	bool ok = n > used && a[used] == 1;
	task_state *s = state_of(name, false);
	if (s && s->running) {
		s->running = false;
		dsk_json *all = read_tasks();
		task t;
		bool kept = get_task(all, name, &t);
		dsk_json_free(all);
		if (kept) {
			if (ok) {
				t.last = (int64_t)time(NULL);
				write_tasks(name, &t);
				schedule(name, &t);
			} else after(name, (int64_t)((t.every > 60 ? t.every : 60) * 1000));
		}
	}
	free(name);
	wh_reply_nil(r);
}

// stop ends the "next" waiting (platform/host's next stream, left).
static void m_stop(wh_reply *r, const uint8_t *a, size_t n) {
	(void)a, (void)n;
	wh_replies_fail(&per.waiting, "stopped");
	wh_reply_nil(r);
}

static const wh_method_def periodic_methods[] = {
	{"schedule", m_schedule}, {"cancel", m_cancel}, {"next", m_next}, {"done", m_done}, {"stop", m_stop}, {NULL, NULL},
};

const wh_module_def wh_periodic_module = {"periodic", periodic_methods};
