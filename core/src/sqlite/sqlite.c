// std/sqlite's runtime (std/sqlite: its native fns are these builtins).
// SQLite is std/sqlite's C (its asuka.toml's [c]: the SQLite amalgamation
// and std/sqlite/csrc/asq.c), linked statically into the apps that import
// std/sqlite (core/link: ak_link_sqlite) and called a whole statement at
// a time through its one entry point, asq_call (std/sqlite/csrc/asq.h).
// Statements run on a thread of their own, one after another (so a write
// is seen by the query after it), and their rows come back as one buffer,
// which rows() decodes on the main thread straight into the program's
// values by the type's descriptor (rt/json.h): no JSON text, no per-cell
// crossing.
//
//   sqlite.openConn(path, busyMs) -> int               a connection
//   sqlite.closeConn(conn)
//   sqlite.execNow(conn, sql, args) -> [string]        on the main thread (open's pragmas): the tables written
//   sqlite.migrateConn(conn, steps) -> [string]        on the main thread: migrate's steps, the tables written
//   sqlite.execAsync(conn, sql, args) -> Task<[string]> off it
//   sqlite.queryAsync(conn, sql, args) -> Task<bytes>  off it: a rows answer
//   sqlite.encodeArgs(value, desc) -> bytes            a tuple, an array, a struct or a map as arguments
//   sqlite.decodeRows(answer, desc) -> [T]             the answer's rows as T
//   sqlite.readsOf(answer), sqlite.writesOf(answer) -> [string]
//   sqlite.cachedAnswer(key) -> bytes?, sqlite.keepAnswer(key, answer)
//   sqlite.available() -> bool
//
// A rows answer: u32 columns, each a string (its name); u32 rows, then
// each row's values as asq writes them (u8 type: 0 null, 1 i64, 2 f64, 3
// text and 4 blob with a u32 length); then u32 n and n strings, the tables
// written, and the same for the tables read.
//
// On the web (no threads) SQLite is the page's: its SQLite worker
// (platform/web/src/sqlite-worker.js) runs std/sqlite's C built to a
// WebAssembly module of its own (asqlite.wasm, which the CLI builds for
// the apps that import std/sqlite), and the calls here go to it
// (js_sqlite_send) and come back as the same answers (asuka_sqlite_done),
// the rows decoded here as on the other platforms. The page's main thread
// can't wait for the worker, so what runs on the main thread elsewhere
// (openConn, execNow, migrateConn) is sent without waiting: in order,
// before the statements after it. A connection that didn't open fails
// every statement with why; a call sent without waiting that failed fails
// the next statement on its connection with its error (sqlite-worker.js).
#include "vm/vm_int.h"
#include "rt/json.h"
#include "rt/sched.h"
#include "base/thread.h"
#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>

// asq.h's ops.
enum { ASQ_OPEN = 1, ASQ_CLOSE = 2, ASQ_EXEC = 3, ASQ_QUERY = 4, ASQ_NEXT = 5, ASQ_CLOSE_ROWS = 6, ASQ_TAKE = 7, ASQ_INTERRUPT = 8 };
enum { ASQ_READS = 1 };

typedef intptr_t (*asq_fn)(uintptr_t op, uintptr_t conn, uintptr_t arg, const uint8_t *in, uintptr_t inLen, uint8_t *out, uintptr_t outCap);

// ---- Answers' cells ----

typedef struct {
	uint8_t type;  // 0 null, 1 int, 2 float, 3 text, 4 blob
	int64_t i;
	double f;
	const uint8_t *p;
	uint32_t n;
} cell;

static bool read_cell(ak_reader *r, cell *c) {
	c->type = ak_read_u8(r);
	switch (c->type) {
	case 0: break;
	case 1: c->i = (int64_t)ak_read_u64(r); break;
	case 2: c->f = ak_read_f64(r); break;
	case 3:
	case 4: c->p = ak_read_bytes(r, &c->n); break;
	default: return false;
	}
	return !r->err;
}

static ak_arr *names_of(ak_reader *r) {
	uint32_t n = ak_read_u32(r);
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, n);
	a->h.flags = 1;
	for (uint32_t i = 0; i < n && !r->err; i++) {
		uint32_t k;
		const uint8_t *s = ak_read_bytes(r, &k);
		ak_arr_push(&a, (ak_val){.p = ak_str_new((const char *)s, k)});
	}
	return a;
}

// stmt_in writes EXEC's (and QUERY's) input: flags, the SQL, the
// arguments (args() made them; none: no arguments).
static void stmt_in_n(ak_buf *in, uint8_t flags, const char *sql, size_t len, const ak_bytes *args) {
	ak_buf_u8(in, flags);
	ak_buf_str(in, sql, len);
	if (args && args->len) ak_buf_put(in, args->data, args->len);
	else ak_buf_u32(in, 0);
}

static void stmt_in(ak_buf *in, uint8_t flags, const ak_str *sql, const ak_bytes *args) { stmt_in_n(in, flags, sql->data, sql->len, args); }

#if !defined(__wasm__)

// ---- SQLite ----

// lib returns asq_call: SQLite linked into the build (ak_link_sqlite), or
// NULL (with the error thrown) in a build without it.
static asq_fn lib(bool throw_) {
	asq_fn f = ak_link_sqlite;
	if (!f && throw_) ak_throw_msg("sqlite: this build has no SQLite: it's linked into the apps that import std/sqlite");
	return f;
}

// ---- Calls ----

// call runs one asq call into *out (grown as needed); its answer's length,
// or -1 when even TAKE failed.
static intptr_t call(asq_fn f, uintptr_t op, uintptr_t conn, uintptr_t arg, const ak_buf *in, ak_buf *out) {
	if (out->cap < 65536) ak_buf_reserve(out, 65536 - out->len);
	intptr_t n = f(op, conn, arg, in ? in->data : NULL, in ? in->len : 0, out->data, out->cap);
	if (n < 0) {
		size_t want = (size_t)-n;
		out->len = 0;
		ak_buf_reserve(out, want);
		n = f(ASQ_TAKE, conn, 0, NULL, 0, out->data, out->cap);
		if (n < 0) return -1;
	}
	out->len = (size_t)n;
	return n;
}

// An answer, read: whether it's ok, its body, and its tables.
typedef struct {
	bool ok;
	ak_reader body;   // from after the header to the tables
	ak_reader tables; // written, then read
	char msg[512];
} answer;

static bool parse(const ak_buf *b, answer *a) {
	memset(a, 0, sizeof *a);
	ak_reader r = {.p = b->data, .len = b->len};
	a->ok = ak_read_u8(&r) != 0;
	uint32_t off = ak_read_u32(&r);
	if (r.err || off < 5 || off > b->len) {
		snprintf(a->msg, sizeof a->msg, "sqlite: a short answer from the library");
		a->ok = false;
		return false;
	}
	a->body = (ak_reader){.p = b->data + 5, .len = off - 5};
	a->tables = (ak_reader){.p = b->data + off, .len = b->len - off};
	if (!a->ok) {
		uint32_t code = ak_read_u32(&a->body);
		ak_read_u8(&a->body);
		uint32_t n;
		const uint8_t *m = ak_read_bytes(&a->body, &n);
		snprintf(a->msg, sizeof a->msg, "sqlite: %.*s (%u)", (int)(n < 400 ? n : 400), m ? (const char *)m : "", code);
	}
	return a->ok;
}

// copy_set copies a set of names (u32 n, n strings) from r into b.
static void copy_set(ak_reader *r, ak_buf *b) {
	uint32_t n = ak_read_u32(r);
	ak_buf_u32(b, r->err ? 0 : n);
	for (uint32_t i = 0; i < n && !r->err; i++) {
		uint32_t k;
		const uint8_t *s = ak_read_bytes(r, &k);
		ak_buf_str(b, (const char *)s, k);
	}
}

// run_exec runs an EXEC; out gets the tables written (u32 n, strings), or
// msg the error.
static bool run_exec(asq_fn f, uint64_t conn, const ak_buf *in, ak_buf *tmp, ak_buf *out, char *msg, size_t msgn) {
	if (call(f, ASQ_EXEC, conn, 0, in, tmp) < 0) {
		snprintf(msg, msgn, "sqlite: out of memory");
		return false;
	}
	answer a;
	if (!parse(tmp, &a)) {
		snprintf(msg, msgn, "%s", a.msg);
		return false;
	}
	copy_set(&a.tables, out);
	return true;
}

// run_query runs a QUERY and its NEXTs into one rows answer in out.
static bool run_query(asq_fn f, uint64_t conn, const ak_buf *in, ak_buf *tmp, ak_buf *out, char *msg, size_t msgn) {
	if (call(f, ASQ_QUERY, conn, 0, in, tmp) < 0) {
		snprintf(msg, msgn, "sqlite: out of memory");
		return false;
	}
	answer a;
	if (!parse(tmp, &a)) {
		snprintf(msg, msgn, "%s", a.msg);
		return false;
	}
	uint32_t cursor = ak_read_u32(&a.body);
	uint32_t ncols = ak_read_u32(&a.body);
	ak_buf_u32(out, ncols);
	for (uint32_t i = 0; i < ncols && !a.body.err; i++) {
		uint32_t k, d;
		const uint8_t *name = ak_read_bytes(&a.body, &k);
		ak_read_bytes(&a.body, &d);  // the declared type
		ak_buf_str(out, (const char *)name, k);
	}
	size_t rows_at = out->len;
	ak_buf_u32(out, 0);
	uint32_t rows = 0;
	// The tables, gathered over the chunks.
	ak_buf written = {0}, read = {0};
	uint32_t nw = 0, nr = 0;
	bool ok = true;
	for (;;) {
		uint8_t done = ak_read_u8(&a.body);
		uint32_t n = ak_read_u32(&a.body);
		if (a.body.err) {
			snprintf(msg, msgn, "sqlite: a short answer from the library");
			ok = false;
			break;
		}
		rows += n;
		ak_buf_put(out, a.body.p + a.body.off, a.body.len - a.body.off);
		for (int set = 0; set < 2; set++) {
			uint32_t k = ak_read_u32(&a.tables);
			for (uint32_t i = 0; i < k && !a.tables.err; i++) {
				uint32_t sl;
				const uint8_t *s = ak_read_bytes(&a.tables, &sl);
				ak_buf_str(set ? &read : &written, (const char *)s, sl);
			}
			if (set) nr += k;
			else nw += k;
		}
		if (done || !cursor) break;
		ak_buf nin = {0};
		ak_buf_u8(&nin, ASQ_READS);
		intptr_t r = call(f, ASQ_NEXT, conn, cursor, &nin, tmp);
		ak_buf_free(&nin);
		if (r < 0) {
			snprintf(msg, msgn, "sqlite: out of memory");
			ok = false;
			break;
		}
		if (!parse(tmp, &a)) {
			snprintf(msg, msgn, "%s", a.msg);
			ok = false;
			break;
		}
	}
	if (ok) {
		uint8_t *p = out->data + rows_at;
		p[0] = rows, p[1] = rows >> 8, p[2] = rows >> 16, p[3] = rows >> 24;
		ak_buf_u32(out, nw);
		if (written.len) ak_buf_put(out, written.data, written.len);
		ak_buf_u32(out, nr);
		if (read.len) ak_buf_put(out, read.data, read.len);
	}
	ak_buf_free(&written);
	ak_buf_free(&read);
	return ok;
}

// ---- Connections ----
//
// The program holds a connection as a number (DB's conn), never asq's
// pointer: one it closed, closed twice or made up is no connection, and
// a statement on it throws. A connection's statements run in order:
// those sent off the main thread first, in the order sent; one run on
// the main thread (execNow, migrateConn) waits for those sent before
// it; and a close waits for them too: the connection closes on the
// statements' thread after the last.

typedef struct dbconn {
	int64_t id;
	uintptr_t asq;    // asq's connection
	uint32_t queued;  // statements sent and not yet run (q_mu)
	bool closing;     // closed with statements queued: the last closes it (q_mu)
} dbconn;

static AK_VEC(dbconn *) conns;  // the open ones; main thread only
static int64_t last_conn;

static ak_mutex q_mu = AK_MUTEX_INIT;
static ak_cond q_cond;  // a statement queued
static ak_cond q_idle;  // a connection's statements all run
static bool q_inited, q_started;

static void q_init(void) {
	if (q_inited) return;
	ak_cond_init(&q_cond);
	ak_cond_init(&q_idle);
	q_inited = true;
}

// conn_of is the open connection id names, or NULL with an error thrown.
static dbconn *conn_of(int64_t id) {
	for (uint32_t i = 0; i < conns.len; i++)
		if (conns.data[i]->id == id) return conns.data[i];
	ak_throw_msg("sqlite: the database is closed");
	return NULL;
}

// conn_wait waits for the statements sent on c before (main thread).
static void conn_wait(dbconn *c) {
	ak_mutex_lock(&q_mu);
	while (c->queued) ak_cond_wait(&q_idle, &q_mu);
	ak_mutex_unlock(&q_mu);
}

static void conn_close(asq_fn f, dbconn *c) {
	uint8_t out[64];
	if (f) f(ASQ_CLOSE, c->asq, 0, NULL, 0, out, sizeof out);
	ak_free(c);
}

// ---- The statements' thread ----

typedef struct job {
	bool query;
	dbconn *conn;  // until it has run
	ak_buf in, out;
	char msg[512];
	bool ok;
	bool running, cancelled;
	ak_future *fut;  // retained; main thread only
	struct job *next;
} job;

static job *q_head, *q_tail;

static void job_free(job *j) {
	ak_buf_free(&j->in);
	ak_buf_free(&j->out);
	ak_free(j);
}

static void deliver(void *p) {
	job *j = p;
	ak_future *f = j->fut;
	f->cancel = NULL;
	f->ctx = NULL;
	if (f->state == AK_FUT_PENDING) {
		if (!j->ok) {
			ak_throw_msg("%s", j->msg);
			ak_future_reject(f, ak_error_take());
		} else if (j->query) {
			ak_future_resolve(f, (ak_val){.p = ak_bytes_new(j->out.data, j->out.len)});
		} else {
			ak_reader r = {.p = j->out.data, .len = j->out.len};
			ak_future_resolve(f, (ak_val){.p = names_of(&r)});
		}
	}
	ak_release(f);
	job_free(j);
	ak_sched_unhold();
}

static void worker(void *arg) {
	AK_UNUSED(arg);
	asq_fn f = lib(false);
	ak_buf tmp = {0};
	for (;;) {
		ak_mutex_lock(&q_mu);
		while (!q_head) ak_cond_wait(&q_cond, &q_mu);
		job *j = q_head;
		q_head = j->next;
		if (!q_head) q_tail = NULL;
		bool skip = j->cancelled;
		j->running = !skip;
		ak_mutex_unlock(&q_mu);
		dbconn *c = j->conn;
		if (skip) {
			j->ok = false;
			snprintf(j->msg, sizeof j->msg, "sqlite: cancelled");
		} else if (j->query) {
			j->ok = run_query(f, c->asq, &j->in, &tmp, &j->out, j->msg, sizeof j->msg);
		} else {
			j->ok = run_exec(f, c->asq, &j->in, &tmp, &j->out, j->msg, sizeof j->msg);
		}
		ak_mutex_lock(&q_mu);
		j->running = false;
		j->conn = NULL;
		bool last = --c->queued == 0;
		bool close = last && c->closing;
		if (last) ak_cond_broadcast(&q_idle);
		ak_mutex_unlock(&q_mu);
		if (close) conn_close(f, c);
		if (tmp.cap > (1u << 22)) ak_buf_free(&tmp);
		ak_post(deliver, j);
	}
}

// A cancelled statement stops where it is (SQLite's interrupt), or never
// starts.
static void job_cancel(ak_future *fut) {
	job *j = fut->ctx;
	if (!j) return;
	ak_mutex_lock(&q_mu);
	j->cancelled = true;
	if (j->running) {
		asq_fn f = lib(false);
		if (f) f(ASQ_INTERRUPT, j->conn->asq, 0, NULL, 0, NULL, 0);
	}
	ak_mutex_unlock(&q_mu);
}

static ak_future *submit(bool query, dbconn *conn, ak_buf in) {
	ak_future *fut = ak_future_new(AK_K_REF);
	job *j = ak_alloc(sizeof *j);
	memset(j, 0, sizeof *j);
	j->query = query;
	j->conn = conn;
	j->in = in;
	j->fut = ak_retain(fut);
	fut->ctx = j;
	fut->cancel = job_cancel;
	ak_mutex_lock(&q_mu);
	q_init();
	if (!q_started) q_started = ak_thread_start(worker, NULL);
	if (!q_started) {
		ak_mutex_unlock(&q_mu);
		// No threads: run it here.
		asq_fn f = lib(false);
		ak_buf tmp = {0};
		j->ok = query ? run_query(f, conn->asq, &j->in, &tmp, &j->out, j->msg, sizeof j->msg) : run_exec(f, conn->asq, &j->in, &tmp, &j->out, j->msg, sizeof j->msg);
		j->conn = NULL;
		ak_buf_free(&tmp);
		ak_sched_hold();
		deliver(j);
		return fut;
	}
	ak_sched_hold();  // until deliver runs
	conn->queued++;
	if (q_tail) q_tail->next = j;
	else q_head = j;
	q_tail = j;
	ak_cond_signal(&q_cond);
	ak_mutex_unlock(&q_mu);
	return fut;
}

// ---- Builtins ----

static ak_val b_available(ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	return (ak_val){.i = lib(false) != NULL};
}

static ak_val b_open(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	asq_fn f = lib(true);
	if (!f) return (ak_val){0};
	ak_str *path = a[0].p;
	ak_buf in = {0}, out = {0};
	ak_buf_u32(&in, (uint32_t)(n > 1 ? a[1].i : 5000));
	ak_buf_str(&in, path->data, path->len);
	int64_t id = 0;
	if (call(f, ASQ_OPEN, 0, 0, &in, &out) < 0) ak_throw_msg("sqlite: out of memory");
	else {
		answer an;
		if (!parse(&out, &an)) ak_throw_msg("%s", an.msg);
		else {
			dbconn *c = ak_alloc(sizeof *c);
			c->id = id = ++last_conn;
			c->asq = (uintptr_t)ak_read_u64(&an.body);
			ak_vec_push(&conns, c);
		}
	}
	ak_buf_free(&in);
	ak_buf_free(&out);
	return (ak_val){.i = id};
}

// sqlite.closeConn: closing one that's closed (or no connection) does
// nothing.
static ak_val b_close(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	dbconn *c = NULL;
	for (uint32_t i = 0; i < conns.len && !c; i++) {
		if (conns.data[i]->id != a[0].i) continue;
		c = conns.data[i];
		conns.data[i] = conns.data[--conns.len];
	}
	if (!c) return (ak_val){0};
	ak_mutex_lock(&q_mu);
	bool now = c->queued == 0;
	c->closing = !now;
	ak_mutex_unlock(&q_mu);
	if (now) conn_close(lib(false), c);
	return (ak_val){0};
}

static ak_val b_exec(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	asq_fn f = lib(true);
	if (!f) return (ak_val){0};
	dbconn *c = conn_of(a[0].i);
	if (!c) return (ak_val){0};
	conn_wait(c);
	ak_buf in = {0}, tmp = {0}, out = {0};
	stmt_in(&in, 0, a[1].p, a[2].p);
	char msg[512];
	ak_arr *res = NULL;
	if (run_exec(f, c->asq, &in, &tmp, &out, msg, sizeof msg)) {
		ak_reader r = {.p = out.data, .len = out.len};
		res = names_of(&r);
	} else {
		ak_throw_msg("%s", msg);
	}
	ak_buf_free(&in);
	ak_buf_free(&tmp);
	ak_buf_free(&out);
	return (ak_val){.p = res};
}

// exec_sql runs one statement of C text on conn into *names (the tables
// it wrote, u32 n and strings, appended); false with msg on failure.
static bool exec_sql(asq_fn f, uint64_t conn, const char *sql, ak_buf *tmp, ak_buf *names, uint32_t *count, char *msg, size_t msgn) {
	ak_buf in = {0}, out = {0};
	stmt_in_n(&in, 0, sql, strlen(sql), NULL);
	bool ok = run_exec(f, conn, &in, tmp, &out, msg, msgn);
	if (ok) {
		ak_reader r = {.p = out.data, .len = out.len};
		uint32_t k = ak_read_u32(&r);
		*count += k;
		ak_buf_put(names, r.p + r.off, r.len - r.off);
	}
	ak_buf_free(&in);
	ak_buf_free(&out);
	return ok;
}

// sqlite.migrateConn(conn, steps): migrate's steps from the database's
// PRAGMA user_version on, each in a transaction, numbered as it commits;
// the tables they wrote.
static ak_val b_migrate(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	asq_fn f = lib(true);
	if (!f) return (ak_val){0};
	dbconn *c = conn_of(a[0].i);
	if (!c) return (ak_val){0};
	conn_wait(c);
	uint64_t conn = c->asq;
	ak_arr *steps = a[1].p;
	ak_buf in = {0}, tmp = {0}, out = {0}, names = {0};
	uint32_t count = 0;
	char msg[512], why[600];
	stmt_in_n(&in, 0, "pragma user_version", 19, NULL);
	bool ok = run_query(f, conn, &in, &tmp, &out, msg, sizeof msg);
	int64_t v = 0;
	if (ok) {
		ak_reader r = {.p = out.data, .len = out.len};
		uint32_t ncols = ak_read_u32(&r);
		for (uint32_t i = 0; i < ncols; i++) {
			uint32_t k;
			ak_read_bytes(&r, &k);
		}
		cell c;
		if (ak_read_u32(&r) > 0 && ncols > 0 && read_cell(&r, &c) && c.type == 1) v = c.i;
	} else {
		snprintf(why, sizeof why, "%s", msg);
	}
	for (; ok && v < (int64_t)steps->len; v++) {
		ak_str *step = steps->data[v].p;
		char sql[64];
		if (!exec_sql(f, conn, "begin immediate", &tmp, &names, &count, msg, sizeof msg)) {
			snprintf(why, sizeof why, "sqlite: migration %lld: %s", (long long)v + 1, msg);
			ok = false;
			break;
		}
		ak_buf sin = {0}, sout = {0};
		stmt_in(&sin, 0, step, NULL);
		ok = run_exec(f, conn, &sin, &tmp, &sout, msg, sizeof msg);
		if (ok) {
			ak_reader r = {.p = sout.data, .len = sout.len};
			count += ak_read_u32(&r);
			ak_buf_put(&names, r.p + r.off, r.len - r.off);
			snprintf(sql, sizeof sql, "pragma user_version = %lld", (long long)v + 1);
			ok = exec_sql(f, conn, sql, &tmp, &names, &count, msg, sizeof msg) && exec_sql(f, conn, "commit", &tmp, &names, &count, msg, sizeof msg);
		}
		ak_buf_free(&sin);
		ak_buf_free(&sout);
		if (!ok) {
			char ignored[64];
			uint32_t none = 0;
			ak_buf nb = {0};
			exec_sql(f, conn, "rollback", &tmp, &nb, &none, ignored, sizeof ignored);
			ak_buf_free(&nb);
			snprintf(why, sizeof why, "sqlite: migration %lld: %s", (long long)v + 1, msg);
		}
	}
	ak_arr *res = NULL;
	if (ok) {
		ak_buf all = {0};
		ak_buf_u32(&all, count);
		if (names.len) ak_buf_put(&all, names.data, names.len);
		ak_reader r = {.p = all.data, .len = all.len};
		res = names_of(&r);
		ak_buf_free(&all);
	} else {
		ak_throw_msg("%s", why);
	}
	ak_buf_free(&in);
	ak_buf_free(&tmp);
	ak_buf_free(&out);
	ak_buf_free(&names);
	return (ak_val){.p = res};
}

static ak_val b_run(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	if (!lib(true)) return (ak_val){0};
	dbconn *c = conn_of(a[0].i);
	if (!c) return (ak_val){0};
	ak_buf in = {0};
	stmt_in(&in, 0, a[1].p, a[2].p);
	return (ak_val){.p = submit(false, c, in)};
}

static ak_val b_query(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	if (!lib(true)) return (ak_val){0};
	dbconn *c = conn_of(a[0].i);
	if (!c) return (ak_val){0};
	ak_buf in = {0};
	stmt_in(&in, ASQ_READS, a[1].p, a[2].p);
	return (ak_val){.p = submit(true, c, in)};
}

#endif  // !__wasm__

#if defined(__wasm__)

// ---- The web: the page's SQLite worker ----
//
// js_sqlite_send(id, op, conn, in): a call for the page's worker (op one
// of WEB_*; in as asq's input, MIGRATE's the steps: u32 n, strings); id 0
// for one sent without waiting, else asuka_sqlite_done(id, answer)
// answers it: u8 ok, then EXEC's tables written (u32 n, strings) or
// QUERY's rows answer, or the error's message (a string).

#define IMPORT(name) __attribute__((import_module("asuka"), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name), visibility("default")))

enum { WEB_OPEN = 1, WEB_CLOSE = 2, WEB_EXEC = 3, WEB_QUERY = 4, WEB_MIGRATE = 5, WEB_CANCEL = 8 };

IMPORT("sqlite_available") int js_sqlite_available(void);
IMPORT("sqlite_send") void js_sqlite_send(double id, int op, double conn, const uint8_t *in, uint32_t len);

typedef struct web_call {
	uint64_t id;
	bool query;
	ak_future *fut;  // retained
	struct web_call *next;
} web_call;

static web_call *calls;
static uint64_t last_call;
static int64_t last_conn;

static void send_now(int op, int64_t conn, const ak_buf *in) { js_sqlite_send(0, op, (double)conn, in ? in->data : NULL, in ? (uint32_t)in->len : 0); }

static web_call *take_call(uint64_t id) {
	for (web_call **p = &calls; *p; p = &(*p)->next) {
		if ((*p)->id == id) {
			web_call *c = *p;
			*p = c->next;
			return c;
		}
	}
	return NULL;
}

// A cancelled statement: one queued never runs; the answer settles it.
static void web_cancel(ak_future *f) {
	web_call *c = f->ctx;
	if (!c) return;
	ak_buf in = {0};
	ak_buf_u64(&in, c->id);
	send_now(WEB_CANCEL, 0, &in);
	ak_buf_free(&in);
}

static ak_future *web_submit(bool query, int64_t conn, ak_buf in) {
	ak_future *fut = ak_future_new(AK_K_REF);
	web_call *c = ak_alloc(sizeof *c);
	c->id = ++last_call;
	c->query = query;
	c->fut = ak_retain(fut);
	c->next = calls;
	calls = c;
	fut->ctx = c;
	fut->cancel = web_cancel;
	ak_sched_hold();  // until it's answered
	js_sqlite_send((double)c->id, query ? WEB_QUERY : WEB_EXEC, (double)conn, in.data, (uint32_t)in.len);
	ak_buf_free(&in);
	return fut;
}

// asuka_sqlite_done answers call id, then runs what waited for it.
EXPORT("asuka_sqlite_done") void asuka_sqlite_done(double id, const uint8_t *p, size_t n) {
	web_call *c = take_call((uint64_t)id);
	if (!c) return;
	ak_future *f = c->fut;
	f->cancel = NULL;
	f->ctx = NULL;
	ak_reader r = {.p = p, .len = n};
	bool ok = ak_read_u8(&r) == 1;
	if (r.err) {
		ak_throw_msg("sqlite: a short answer from the page");
		ak_future_reject(f, ak_error_take());
	} else if (!ok) {
		uint32_t k;
		const uint8_t *m = ak_read_bytes(&r, &k);
		ak_throw_msg("%.*s", (int)(k < 500 ? k : 500), m ? (const char *)m : "sqlite: failed");
		ak_future_reject(f, ak_error_take());
	} else if (c->query) {
		ak_future_resolve(f, (ak_val){.p = ak_bytes_new(p + 1, n - 1)});
	} else {
		ak_future_resolve(f, (ak_val){.p = names_of(&r)});
	}
	ak_release(f);
	ak_free(c);
	ak_sched_unhold();
	ak_ui_run_posted();
}

static ak_arr *no_names(void) {
	ak_arr *a = ak_arr_new(AK_K_REF, NULL, 0);
	a->h.flags = 1;
	return a;
}

static ak_val b_available(ak_val *a, uint32_t n) {
	AK_UNUSED(a), AK_UNUSED(n);
	return (ak_val){.i = js_sqlite_available() != 0};
}

static ak_val b_open(ak_val *a, uint32_t n) {
	if (!js_sqlite_available()) {
		ak_throw_msg("sqlite: the page has no SQLite (its module, asqlite.wasm, isn't the page's sqlite option)");
		return (ak_val){0};
	}
	ak_str *path = a[0].p;
	ak_buf in = {0};
	ak_buf_u32(&in, (uint32_t)(n > 1 ? a[1].i : 5000));
	ak_buf_str(&in, path->data, path->len);
	int64_t conn = ++last_conn;
	send_now(WEB_OPEN, conn, &in);
	ak_buf_free(&in);
	return (ak_val){.i = conn};
}

static ak_val b_close(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	if (a[0].i) send_now(WEB_CLOSE, a[0].i, NULL);
	return (ak_val){0};
}

static ak_val b_exec(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_buf in = {0};
	stmt_in(&in, 0, a[1].p, a[2].p);
	send_now(WEB_EXEC, a[0].i, &in);
	ak_buf_free(&in);
	return (ak_val){.p = no_names()};
}

static ak_val b_migrate(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_arr *steps = a[1].p;
	ak_buf in = {0};
	ak_buf_u32(&in, steps->len);
	for (uint32_t i = 0; i < steps->len; i++) {
		ak_str *step = steps->data[i].p;
		ak_buf_str(&in, step->data, step->len);
	}
	send_now(WEB_MIGRATE, a[0].i, &in);
	ak_buf_free(&in);
	return (ak_val){.p = no_names()};
}

static ak_val b_run(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_buf in = {0};
	stmt_in(&in, 0, a[1].p, a[2].p);
	return (ak_val){.p = web_submit(false, a[0].i, in)};
}

static ak_val b_query(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_buf in = {0};
	stmt_in(&in, ASQ_READS, a[1].p, a[2].p);
	return (ak_val){.p = web_submit(true, a[0].i, in)};
}

#endif  // __wasm__

// One argument, by its kind (and a reference's tag).
static bool put_arg(ak_buf *b, ak_val v, ak_kind k) {
	switch (k) {
	case AK_K_INT:
	case AK_K_BOOL:
		ak_buf_u8(b, 1);
		ak_buf_u64(b, (uint64_t)v.i);
		return true;
	case AK_K_FLOAT:
		ak_buf_u8(b, 2);
		ak_buf_f64(b, v.f);
		return true;
	case AK_K_REF:
		break;
	default:
		return false;
	}
	ak_obj *o = v.p;
	if (!o) {
		ak_buf_u8(b, 0);
		return true;
	}
	switch (o->tag) {
	case AK_TAG_STR: {
		ak_str *s = v.p;
		ak_buf_u8(b, 3);
		ak_buf_str(b, s->data, s->len);
		return true;
	}
	case AK_TAG_BYTES: {
		ak_bytes *y = v.p;
		ak_buf_u8(b, 4);
		ak_buf_str(b, (const char *)y->data, y->len);
		return true;
	}
	case AK_TAG_BOX: {
		ak_box *x = v.p;
		return put_arg(b, x->v, (ak_kind)x->h.aux);
	}
	}
	return false;
}

static bool tuple_shape(const ak_shape *s) { return s->name && s->name[0] == '('; }

// sqlite.args(value, desc): the statement's arguments. A tuple, a set or an
// array gives them by position (?, ?1), a struct or a map from strings
// by name (:name, @name, $name) and position; one value alone is the one
// argument.
static ak_val b_args(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_val v = a[0];
	ak_str *desc = n > 1 ? a[1].p : NULL;
	ak_buf b = {0};
	bool ok = true;
	ak_obj *o = v.p;
	char d = desc ? desc->data[0] : '-';
	if (d == 'i' || d == 'f' || d == 'b' || d == 'e') {
		ak_buf_u32(&b, 1);
		ak_buf_str(&b, "", 0);
		ok = put_arg(&b, v, d == 'f' ? AK_K_FLOAT : AK_K_INT);
	} else if (o && o->tag == AK_TAG_ARR) {
		ak_arr *arr = v.p;
		ak_buf_u32(&b, arr->len);
		for (uint32_t i = 0; ok && i < arr->len; i++) {
			ak_buf_str(&b, "", 0);
			ok = put_arg(&b, arr->data[i], ak_arr_kind(arr));
		}
	} else if (o && o->tag == AK_TAG_STRUCT) {
		ak_struct *s = v.p;
		const ak_shape *sh = s->shape;
		bool named = !tuple_shape(sh) && sh->names;
		ak_buf_u32(&b, sh->nfields);
		for (uint16_t i = 0; ok && i < sh->nfields; i++) {
			const char *name = named ? sh->names[i] : "";
			ak_buf_str(&b, name, strlen(name));
			ok = put_arg(&b, s->f[i], (ak_kind)sh->kinds[i]);
		}
	} else if (o && o->tag == AK_TAG_MAP && ak_map_is_set(v.p)) {
		// A set's elements, by position, as an array's.
		ak_map *m = v.p;
		ak_kind ek = (ak_kind)(m->h.aux & 0xf);
		ak_buf_u32(&b, m->live);
		for (uint32_t i = 0; ok && i < m->len; i++) {
			if (m->dead[i]) continue;
			ak_buf_str(&b, "", 0);
			ok = put_arg(&b, m->keys[i], ek);
		}
	} else if (o && o->tag == AK_TAG_MAP) {
		ak_map *m = v.p;
		ak_kind vk = (ak_kind)(m->h.aux >> 4);
		// From strings only: a map's keys may be other values (its
		// descriptor says; the VM marks every reference key a string's).
		if (desc ? d != '{' : !(m->h.flags & 1)) ok = false;
		ak_buf_u32(&b, m->live);
		for (uint32_t i = 0; ok && i < m->len; i++) {
			if (m->dead && m->dead[i]) continue;
			ak_str *k = m->keys[i].p;
			ak_buf_str(&b, k->data, k->len);
			ok = put_arg(&b, m->vals[i], vk);
		}
	} else {
		ak_buf_u32(&b, 1);
		ak_buf_str(&b, "", 0);
		ok = put_arg(&b, v, AK_K_REF);
	}
	if (!ok) {
		ak_buf_free(&b);
		ak_throw_msg("sqlite: an argument is an int, a float, a bool, a string, bytes, an enum's case, or an optional of one");
		return (ak_val){0};
	}
	ak_bytes *out = ak_bytes_new(b.data, b.len);
	ak_buf_free(&b);
	return (ak_val){.p = out};
}

// ---- Rows ----


static const ak_json_types *types(void) {
	static ak_json_types t;
	extern void ak_vm_json_types(ak_json_types *out);
	ak_vm_json_types(&t);
	return &t;
}

static ak_kind kind_of(const char *d) {
	switch (*d) {
	case 'i':
	case 'e':
		return AK_K_INT;
	case 'f':
		return AK_K_FLOAT;
	case 'b':
		return AK_K_BOOL;
	default:
		return AK_K_REF;
	}
}

static const ak_shape *shape_of(const char *d, const ak_json_types *t) {
	if (*d != 'S' && *d != 'E' && *d != 'e') return NULL;
	const char *end = strchr(d + 1, ';');
	size_t len = end ? (size_t)(end - d - 1) : strlen(d + 1);
	return t->shape(t->ctx, d + 1, len);
}

static bool cell_text(const cell *c, char *buf, size_t n) {
	size_t k = c->n < n - 1 ? c->n : n - 1;
	memcpy(buf, c->p, k);
	buf[k] = 0;
	return true;
}

// convert makes a value of descriptor d from a cell; false (and msg)
// when it can't be one.
static bool convert(const cell *c, const char *d, const ak_json_types *t, ak_val *out, const char *col, char *msg, size_t msgn) {
	char tmp[64];
	*out = (ak_val){0};
	switch (*d) {
	case '?':
		if (c->type == 0) return true;
		if (!convert(c, d + 1, t, out, col, msg, msgn)) return false;
		if (kind_of(d + 1) != AK_K_REF) *out = (ak_val){.p = ak_box_new(kind_of(d + 1), *out)};
		return true;
	case 'i':
		switch (c->type) {
		case 0: return true;
		case 1: out->i = c->i; return true;
		case 2: out->i = (int64_t)c->f; return true;
		case 3: {
			cell_text(c, tmp, sizeof tmp);
			char *e;
			long long v = strtoll(tmp, &e, 10);
			if (e != tmp && *e == 0) {
				out->i = v;
				return true;
			}
			break;
		}
		}
		break;
	case 'f':
		switch (c->type) {
		case 0: return true;
		case 1: out->f = (double)c->i; return true;
		case 2: out->f = c->f; return true;
		case 3: {
			cell_text(c, tmp, sizeof tmp);
			char *e;
			double v = strtod(tmp, &e);
			if (e != tmp && *e == 0) {
				out->f = v;
				return true;
			}
			break;
		}
		}
		break;
	case 'b':
		switch (c->type) {
		case 0: return true;
		case 1: out->i = c->i != 0; return true;
		case 2: out->i = c->f != 0; return true;
		case 3:
			cell_text(c, tmp, sizeof tmp);
			if (strcmp(tmp, "true") == 0 || strcmp(tmp, "1") == 0) out->i = 1;
			else if (strcmp(tmp, "false") == 0 || strcmp(tmp, "0") == 0 || !tmp[0]) out->i = 0;
			else break;
			return true;
		}
		break;
	case 's':
		switch (c->type) {
		case 0: out->p = ak_str_empty(); return true;
		case 1: out->p = ak_str_from_int(c->i); return true;
		case 2: out->p = ak_str_from_float(c->f); return true;
		default: out->p = ak_str_new((const char *)c->p, c->n); return true;
		}
	case 'y':
		if (c->type == 0) {
			out->p = ak_bytes_new(NULL, 0);
			return true;
		}
		if (c->type == 3 || c->type == 4) {
			out->p = ak_bytes_new(c->p, c->n);
			return true;
		}
		break;
	case 'e': {
		const char *const *names = NULL;
		uint32_t count = 0;
		const char *end = strchr(d + 1, ';');
		size_t len = end ? (size_t)(end - d - 1) : strlen(d + 1);
		if (t->cases) t->cases(t->ctx, d + 1, len, &names, &count);
		if (c->type == 0) return true;
		if (c->type == 1 && c->i >= 0 && (uint64_t)c->i < (count ? count : UINT32_MAX)) {
			out->i = c->i;
			return true;
		}
		if (c->type == 3) {
			for (uint32_t i = 0; i < count; i++) {
				if (strlen(names[i]) == c->n && memcmp(names[i], c->p, c->n) == 0) {
					out->i = i;
					return true;
				}
			}
		}
		break;
	}
	case '[':
		if (c->type == 0) {
			out->p = ak_arr_new(kind_of(d + 1), shape_of(d + 1, t), 0);
			return true;
		}
		goto json;
	case '{':
		if (c->type == 0) {
			out->p = ak_map_new(AK_K_REF, true, kind_of(d + 1), shape_of(d + 1, t));
			return true;
		}
		goto json;
	case '<':
		if (c->type == 0) {
			out->p = ak_set_new(kind_of(d + 1), d[1] == 's');
			return true;
		}
		goto json;
	case 'S':
	case 'E':
	json:
		// A column holding JSON: a struct, a list, a map in a column.
		if (c->type == 3 || c->type == 4) {
			*out = ak_json_decode((const char *)c->p, c->n, d, t);
			if (ak_error_pending()) {
				void *e = ak_error_take();
				snprintf(msg, msgn, "sqlite: column %s: %s", col, ak_error_message(e)->data);
				ak_release(e);
				*out = (ak_val){0};
				return false;
			}
			return true;
		}
		break;
	}
	static const char *types_[] = {"null", "an integer", "a real", "text", "a blob"};
	snprintf(msg, msgn, "sqlite: column %s is %s, which doesn't fit %s", col, c->type < 5 ? types_[c->type] : "?", d);
	return false;
}

// zero is a field's value when no column has it.
static bool zero(const char *d, const ak_json_types *t, ak_val *out) {
	*out = (ak_val){0};
	switch (*d) {
	case 's': out->p = ak_str_empty(); return true;
	case 'y': out->p = ak_bytes_new(NULL, 0); return true;
	case '[': out->p = ak_arr_new(kind_of(d + 1), shape_of(d + 1, t), 0); return true;
	case '{': out->p = ak_map_new(AK_K_REF, true, kind_of(d + 1), shape_of(d + 1, t)); return true;
	case '<': out->p = ak_set_new(kind_of(d + 1), d[1] == 's'); return true;
	case 'S':
	case 'E': return false;
	default: return true;  // a zero scalar, none
	}
}

// sqlite.rows(answer, desc): each row as a T: a struct's fields by its
// columns' names, anything else from the first column.
static ak_val b_rows(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *ans = a[0].p;
	const char *d = ((ak_str *)a[1].p)->data;
	const ak_json_types *t = types();
	ak_reader r = {.p = ans->data, .len = ans->len};
	uint32_t ncols = ak_read_u32(&r);
	if (r.err || ncols > 2000) {
		ak_throw_msg("sqlite: a bad rows answer");
		return (ak_val){0};
	}
	const uint8_t *cname[2000];
	uint32_t clen[2000];
	for (uint32_t i = 0; i < ncols; i++) cname[i] = ak_read_bytes(&r, &clen[i]);
	uint32_t nrows = ak_read_u32(&r);
	if (r.err) {
		ak_throw_msg("sqlite: a bad rows answer");
		return (ak_val){0};
	}
	const ak_shape *sh = *d == 'S' ? shape_of(d, t) : NULL;
	if (*d == 'S' && !sh) {
		ak_throw_msg("sqlite: no shape for %s", d);
		return (ak_val){0};
	}
	ak_kind ek = kind_of(d);
	ak_arr *out = ak_arr_new(ek, *d == 'S' || *d == 'E' ? shape_of(d, t) : NULL, nrows);
	if (*d == 's') out->h.flags = 1;
	// Columns to fields, once.
	int16_t fieldOf[2000];
	bool seen[512];
	if (sh) {
		if (sh->nfields > 512) {
			ak_throw_msg("sqlite: %s has too many fields", sh->name);
			ak_release(out);
			return (ak_val){0};
		}
		memset(seen, 0, sizeof seen);
		for (uint32_t c = 0; c < ncols; c++) {
			fieldOf[c] = -1;
			for (uint16_t i = 0; i < sh->nfields; i++) {
				if (sh->names && strlen(sh->names[i]) == clen[c] && memcmp(sh->names[i], cname[c], clen[c]) == 0) {
					fieldOf[c] = (int16_t)i;
					seen[i] = true;
					break;
				}
			}
		}
	}
	char msg[600] = {0};
	char col[128];
	for (uint32_t row = 0; row < nrows; row++) {
		ak_val v = {0};
		if (sh) {
			ak_struct *s = ak_struct_new(sh);
			for (uint16_t i = 0; i < sh->nfields && !msg[0]; i++) {
				if (seen[i]) continue;
				extern const char *ak_json_field_desc(const ak_shape *s, int i);
				if (!zero(ak_json_field_desc(sh, i), t, &s->f[i])) snprintf(msg, sizeof msg, "sqlite: no column %s for a %s", sh->names[i], sh->name);
			}
			for (uint32_t c = 0; c < ncols && !msg[0]; c++) {
				cell ce;
				if (!read_cell(&r, &ce)) {
					snprintf(msg, sizeof msg, "sqlite: a bad rows answer");
					break;
				}
				int fi = fieldOf[c];
				if (fi < 0) continue;
				extern const char *ak_json_field_desc(const ak_shape *s, int i);
				snprintf(col, sizeof col, "%.*s", (int)(clen[c] < 100 ? clen[c] : 100), (const char *)cname[c]);
				ak_val fv;
				if (!convert(&ce, ak_json_field_desc(sh, fi), t, &fv, col, msg, sizeof msg)) break;
				ak_release_val(s->f[fi], (ak_kind)sh->kinds[fi]);
				s->f[fi] = fv;
			}
			v.p = s;
			if (msg[0]) {
				ak_release(s);
				break;
			}
		} else {
			for (uint32_t c = 0; c < ncols && !msg[0]; c++) {
				cell ce;
				if (!read_cell(&r, &ce)) {
					snprintf(msg, sizeof msg, "sqlite: a bad rows answer");
					break;
				}
				if (c > 0) continue;
				snprintf(col, sizeof col, "%.*s", (int)(clen[c] < 100 ? clen[c] : 100), (const char *)cname[c]);
				convert(&ce, d, t, &v, col, msg, sizeof msg);
			}
			if (ncols == 0) snprintf(msg, sizeof msg, "sqlite: the query has no columns");
			if (msg[0]) {
				ak_release_val(v, ek);
				break;
			}
		}
		ak_arr_push(&out, v);
	}
	if (msg[0]) {
		ak_release(out);
		ak_throw_msg("%s", msg);
		return (ak_val){0};
	}
	return (ak_val){.p = out};
}

// skip_rows moves r past an answer's columns and rows, to its tables.
static void skip_rows(ak_reader *r) {
	uint32_t ncols = ak_read_u32(r);
	for (uint32_t i = 0; i < ncols && !r->err; i++) {
		uint32_t k;
		ak_read_bytes(r, &k);
	}
	uint32_t nrows = ak_read_u32(r);
	for (uint64_t i = 0; i < (uint64_t)nrows * ncols && !r->err; i++) {
		cell c;
		if (!read_cell(r, &c)) r->err = true;
	}
}

static ak_val b_writes(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *ans = a[0].p;
	ak_reader r = {.p = ans->data, .len = ans->len};
	skip_rows(&r);
	return (ak_val){.p = names_of(&r)};
}

static ak_val b_reads(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_bytes *ans = a[0].p;
	ak_reader r = {.p = ans->data, .len = ans->len};
	skip_rows(&r);
	ak_release(names_of(&r));
	return (ak_val){.p = names_of(&r)};
}

// ---- The last answers, by query ----
//
// A live query made again (its arguments changed) starts from the last
// rows of its SQL while the new ones load, as a Loading keeps its value.

#define NCACHED 64

static struct {
	ak_str *key;
	ak_bytes *answer;
	uint64_t used;
} cache[NCACHED];
static uint64_t cache_clock;

static ak_val b_cached(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_str *k = a[0].p;
	for (int i = 0; i < NCACHED; i++) {
		if (cache[i].key && ak_str_eq(cache[i].key, k)) {
			cache[i].used = ++cache_clock;
			return (ak_val){.p = ak_retain(cache[i].answer)};
		}
	}
	return (ak_val){0};
}

static ak_val b_keep(ak_val *a, uint32_t n) {
	AK_UNUSED(n);
	ak_str *k = a[0].p;
	int slot = 0;
	for (int i = 0; i < NCACHED; i++) {
		if (cache[i].key && ak_str_eq(cache[i].key, k)) {
			slot = i;
			break;
		}
		if (cache[i].used < cache[slot].used) slot = i;
	}
	ak_release(cache[slot].key);
	ak_release(cache[slot].answer);
	cache[slot].key = ak_retain(k);
	cache[slot].answer = ak_retain(a[1].p);
	cache[slot].used = ++cache_clock;
	return (ak_val){0};
}

void ak_sqlite_init(void) {
	ak_builtin_register("sqlite.available", b_available);
	ak_builtin_register("sqlite.openConn", b_open);
	ak_builtin_register("sqlite.closeConn", b_close);
	ak_builtin_register("sqlite.execNow", b_exec);
	ak_builtin_register("sqlite.execAsync", b_run);
	ak_builtin_register("sqlite.queryAsync", b_query);
	ak_builtin_register("sqlite.migrateConn", b_migrate);
	ak_builtin_register("sqlite.encodeArgs", b_args);
	ak_builtin_register("sqlite.decodeRows", b_rows);
	ak_builtin_register("sqlite.readsOf", b_reads);
	ak_builtin_register("sqlite.writesOf", b_writes);
	ak_builtin_register("sqlite.cachedAnswer", b_cached);
	ak_builtin_register("sqlite.keepAnswer", b_keep);
}
