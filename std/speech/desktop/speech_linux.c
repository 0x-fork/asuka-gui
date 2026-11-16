// asuka/speech on Linux: Speech Dispatcher (the desktop's speech
// service, which Orca speaks through), over its socket in SSIP, its text
// protocol: no library to load, and the daemon is started as libspeechd
// starts it when it isn't running. A connection's reader is a thread of
// its own, its commands are sent from workers (one at a time); what
// they hear comes back to the main thread, where the state is. macOS
// runs the package's Swift.
#include "asuka_desktop.h"
#include "speech.asuka.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ---- SSIP ----

// A reply: its code and its lines (the last's text after the code).
typedef struct reply {
	int code;
	char **lines;
	size_t n;
	struct reply *next;
} reply;

static void reply_free(reply *r) {
	if (!r) return;
	for (size_t i = 0; i < r->n; i++) free(r->lines[i]);
	free(r->lines);
	free(r);
}

static bool reply_ok(const reply *r) { return r->code >= 200 && r->code < 300; }

static void reply_fail(const reply *r, asuka_error *err) {
	asuka_error_failed(err, "speech-dispatcher", r->code, "speech-dispatcher: %d %s", r->code, r->n ? r->lines[r->n - 1] : "");
}

// A connection to Speech Dispatcher, shared by the main thread, its
// reader and the workers sending commands (each with a reference).
typedef struct {
	int fd;
	atomic_int refs;
	pthread_mutex_t send;  // one command at a time
	pthread_mutex_t mu;    // replies, closed
	pthread_cond_t cv;
	reply *replies, *last;  // command replies, in order
	bool closed;
} conn;

static void conn_ref(conn *c) { atomic_fetch_add(&c->refs, 1); }

static void conn_unref(conn *c) {
	if (atomic_fetch_sub(&c->refs, 1) != 1) return;
	close(c->fd);
	for (reply *r = c->replies, *n; r; r = n) n = r->next, reply_free(r);
	pthread_mutex_destroy(&c->send);
	pthread_mutex_destroy(&c->mu);
	pthread_cond_destroy(&c->cv);
	free(c);
}

static bool write_all(int fd, const char *p, size_t n) {
	while (n > 0) {
		ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) return false;
		p += w, n -= (size_t)w;
	}
	return true;
}

// command_locked sends a command and waits for its reply (NULL with err
// set). The send lock held; not on the main thread.
static reply *command_locked(conn *c, const char *cmd, asuka_error *err) {
	char *line = asuka_sprintf("%s\r\n", cmd);
	bool sent = write_all(c->fd, line, strlen(line));
	free(line);
	if (!sent) {
		asuka_error_failed(err, "errno", errno, "speech-dispatcher: %s", strerror(errno));
		return NULL;
	}
	pthread_mutex_lock(&c->mu);
	while (!c->replies && !c->closed) pthread_cond_wait(&c->cv, &c->mu);
	reply *r = c->replies;
	if (r) {
		c->replies = r->next;
		if (!c->replies) c->last = NULL;
	}
	pthread_mutex_unlock(&c->mu);
	if (!r) asuka_error_failed(err, "speech", 0, "speech-dispatcher: the connection closed");
	return r;
}

static reply *command(conn *c, const char *cmd, asuka_error *err) {
	pthread_mutex_lock(&c->send);
	reply *r = command_locked(c, cmd, err);
	pthread_mutex_unlock(&c->send);
	return r;
}

// ---- The synthesizer's state (main thread) ----

// A speak or a voices waiting for its answer.
typedef struct job {
	asuka_call *call;  // NULL once answered
	bool voices;
	char **settings;
	size_t nsettings;
	char *text;
	int id;
	bool busy;  // a worker has it
	struct job *next;  // in dialing or waiting
} job;

typedef struct {
	int id;
	bool ok;
} ended_early;

static struct {
	conn *conn;
	job *dialing;  // waiting for the connection
	job *waiting;  // spoken, waiting for their ends, by id
	ended_early *early;  // ended before their IDs came back
	size_t nearly;
	int pending;  // speaks sent, their IDs not back yet
	bool paused, connecting;
} s;

static void job_free(job *j) {
	for (size_t i = 0; i < j->nsettings; i++) free(j->settings[i]);
	free(j->settings);
	free(j->text);
	free(j);
}

// answer answers j's call (err NULL: spoken), and frees it unless a
// worker still has it.
static void answer(job *j, asuka_error *err) {
	asuka_call *c = j->call;
	j->call = NULL;
	if (c) {
		if (err) asuka_call_fail(c, err);
		else speech_Synthesizer_speak_done(c);
	}
	if (!j->busy) job_free(j);
}

static void answer_stopped(job *j) {
	asuka_error e = {0};
	asuka_error_declared(&e, speech_SpeechError_stopped);
	answer(j, &e);
}

static bool unlink_job(job **list, job *j) {
	for (job **p = list; *p; p = &(*p)->next)
		if (*p == j) {
			*p = j->next;
			j->next = NULL;
			return true;
		}
	return false;
}

// ended fails every message waiting: stopped (err NULL), or err.
static void ended(const char *why) {
	job *w = s.waiting;
	s.waiting = NULL;
	while (w) {
		job *n = w->next;
		w->next = NULL;
		if (why) {
			asuka_error e = {0};
			asuka_error_failed(&e, "speech", 0, "%s", why);
			answer(w, &e);
		} else {
			answer_stopped(w);
		}
		w = n;
	}
}

// An event, from the reader: 702 END, 703 CANCELED end a message; 704
// PAUSED, 705 RESUMED.
typedef struct {
	int code, id;
} event;

static void on_event(void *p) {
	event *ev = p;
	switch (ev->code) {
	case 702:
	case 703: {
		job *j = NULL;
		for (j = s.waiting; j && j->id != ev->id; j = j->next) {
		}
		if (!j) {
			if (s.pending > 0) {
				s.early = realloc(s.early, (s.nearly + 1) * sizeof *s.early);
				s.early[s.nearly++] = (ended_early){ev->id, ev->code == 702};
			}
			break;
		}
		unlink_job(&s.waiting, j);
		if (ev->code == 702) answer(j, NULL);
		else answer_stopped(j);
		break;
	}
	case 704: s.paused = true; break;
	case 705: s.paused = false; break;
	}
	free(ev);
}

static void on_closed(void *p) {
	conn *c = p;
	if (s.conn == c) {
		s.conn = NULL;
		conn_unref(c);
		ended("speech-dispatcher: the connection closed");
	}
	conn_unref(c);
}

// reader reads replies: notifications go to the main thread, the rest to
// the commands waiting.
static void reader(void *p) {
	conn *c = p;
	char buf[4096];
	asuka_writer line = {0};
	char **lines = NULL;
	size_t n = 0;
	for (;;) {
		ssize_t got = recv(c->fd, buf, sizeof buf, 0);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) break;
		for (ssize_t i = 0; i < got; i++) {
			if (buf[i] != '\n') {
				asuka_writer_u8(&line, (uint8_t)buf[i]);
				continue;
			}
			size_t len = line.len;
			while (len > 0 && line.data[len - 1] == '\r') len--;
			const char *t = (const char *)line.data;
			if (len >= 4) {
				int code = atoi((char[4]){t[0], t[1], t[2], 0});
				lines = realloc(lines, (n + 1) * sizeof *lines);
				lines[n++] = asuka_strndup(t + 4, len - 4);
				if (t[3] != '-') {
					if (code >= 700 && code < 800) {
						event *ev = malloc(sizeof *ev);
						*ev = (event){code, atoi(lines[0])};
						asuka_desktop_post(on_event, ev);
						for (size_t k = 0; k < n; k++) free(lines[k]);
						free(lines);
					} else {
						reply *r = calloc(1, sizeof *r);
						*r = (reply){.code = code, .lines = lines, .n = n};
						pthread_mutex_lock(&c->mu);
						if (c->last) c->last->next = r;
						else c->replies = r;
						c->last = r;
						pthread_cond_broadcast(&c->cv);
						pthread_mutex_unlock(&c->mu);
					}
					lines = NULL;
					n = 0;
				}
			}
			line.len = 0;
		}
	}
	for (size_t k = 0; k < n; k++) free(lines[k]);
	free(lines);
	asuka_writer_free(&line);
	pthread_mutex_lock(&c->mu);
	c->closed = true;
	pthread_cond_broadcast(&c->cv);
	pthread_mutex_unlock(&c->mu);
	asuka_desktop_post(on_closed, c);  // the reader's reference, let go there
}

// socket_path is where the daemon listens.
static char *socket_path(void) {
	const char *a = getenv("SPEECHD_ADDRESS");
	if (a && !strncmp(a, "unix_socket:", 12)) return asuka_strdup(a + 12);
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (dir && *dir) return asuka_sprintf("%s/speech-dispatcher/speechd.sock", dir);
	const char *tmp = getenv("TMPDIR");
	return asuka_sprintf("%s/speech-dispatcher-%d/speech-dispatcher/speechd.sock", tmp && *tmp ? tmp : "/tmp", (int)getuid());
}

static int dial_unix(const char *path) {
	struct sockaddr_un a = {.sun_family = AF_UNIX};
	if (strlen(path) >= sizeof a.sun_path) return -1;
	strcpy(a.sun_path, path);
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0) return fd;
	close(fd);
	return -1;
}

// look_path is an executable's path on PATH, or NULL.
static char *look_path(const char *name) {
	const char *path = getenv("PATH");
	if (!path) path = "/usr/local/bin:/usr/bin:/bin";
	for (const char *p = path; *p;) {
		const char *e = strchr(p, ':');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char *f = asuka_sprintf("%.*s/%s", (int)n, n ? p : ".", name);
		if (access(f, X_OK) == 0) return f;
		free(f);
		p += n;
		if (*p == ':') p++;
	}
	return NULL;
}

// dial connects, starting the daemon if it isn't running. Not on the main
// thread.
static conn *dial(asuka_error *err) {
	char *path = socket_path();
	int fd = dial_unix(path);
	if (fd < 0) {
		char *exe = look_path("speech-dispatcher");
		if (!exe) {
			free(path);
			asuka_error_failed(err, "speech", 0, "speech: speech-dispatcher isn't installed");
			return NULL;
		}
		// As libspeechd autospawns it: it forks, and exits once it listens.
		pid_t pid = fork();
		if (pid == 0) {
			execl(exe, exe, "--spawn", "--communication-method", "unix_socket", "--socket-path", path, (char *)NULL);
			_exit(127);
		}
		if (pid > 0) waitpid(pid, NULL, 0);
		free(exe);
		for (int i = 0; i < 50 && (fd = dial_unix(path)) < 0; i++) nanosleep(&(struct timespec){0, 100000000}, NULL);
		if (fd < 0) {
			asuka_error_failed(err, "errno", errno, "speech: speech-dispatcher doesn't start: %s", strerror(errno));
			free(path);
			return NULL;
		}
	}
	free(path);
	conn *c = calloc(1, sizeof *c);
	c->fd = fd;
	atomic_init(&c->refs, 2);  // the dialer's, the reader's
	pthread_mutex_init(&c->send, NULL);
	pthread_mutex_init(&c->mu, NULL);
	pthread_cond_init(&c->cv, NULL);
	if (!asuka_desktop_spawn(reader, c)) {
		conn_unref(c);
		conn_unref(c);
		asuka_error_failed(err, "speech", 0, "speech: no thread");
		return NULL;
	}
	const char *user = getenv("USER");
	char *app = asuka_strdup(program_invocation_short_name);
	for (char *q = app; *q; q++)
		if (*q == ':') *q = '_';
	char *name = asuka_sprintf("SET self CLIENT_NAME %s:%s:main", user && *user ? user : "user", app);
	free(app);
	const char *cmds[2] = {name, "SET self NOTIFICATION ALL on"};
	for (int i = 0; i < 2; i++) {
		reply *r = command(c, cmds[i], err);
		bool ok = r && reply_ok(r);
		if (r && !ok) reply_fail(r, err);
		reply_free(r);
		if (!ok) {
			free(name);
			shutdown(c->fd, SHUT_RDWR);  // the reader ends, and lets its reference go
			conn_unref(c);
			return NULL;
		}
	}
	free(name);
	return c;
}

static void dispatch(job *j);

typedef struct {
	conn *c;
	asuka_error err;
} dialed;

static void on_dialed(void *p) {
	dialed *d = p;
	s.connecting = false;
	if (d->c) s.conn = d->c;  // the dialer's reference, the state's now
	job *js = s.dialing;
	s.dialing = NULL;
	// In the order they came.
	job *rev = NULL;
	while (js) {
		job *n = js->next;
		js->next = rev;
		rev = js;
		js = n;
	}
	while (rev) {
		job *n = rev->next;
		rev->next = NULL;
		if (!d->c) {
			asuka_error e = {0};
			asuka_error_failed(&e, d->err.domain ? d->err.domain : "speech", d->err.code, "%s", d->err.message ? d->err.message : "speech");
			answer(rev, &e);
		} else {
			dispatch(rev);
		}
		rev = n;
	}
	asuka_error_free(&d->err);
	free(d);
}

static void dial_thread(void *p) {
	dialed *d = p;
	d->c = dial(&d->err);
	asuka_desktop_post(on_dialed, d);
}

// connect dispatches j on the connection, connecting the first time.
static void connect_for(job *j) {
	if (s.conn) {
		dispatch(j);
		return;
	}
	j->next = s.dialing;  // newest first; on_dialed reverses
	s.dialing = j;
	if (s.connecting) return;
	s.connecting = true;
	dialed *d = calloc(1, sizeof *d);
	if (!asuka_desktop_spawn(dial_thread, d)) {
		asuka_error_failed(&d->err, "speech", 0, "speech: no thread");
		on_dialed(d);
	}
}

// ---- Workers ----

typedef struct {
	job *j;
	conn *c;
	reply *r;
	asuka_error err;
} work;

static void spoke(void *p) {
	work *w = p;
	job *j = w->j;
	s.pending--;
	j->busy = false;
	conn_unref(w->c);
	if (w->err.status) {
		answer(j, &w->err);
	} else if (!j->call) {
		job_free(j);  // cancelled while it was sent
	} else {
		size_t i;
		for (i = 0; i < s.nearly && s.early[i].id != j->id; i++) {
		}
		if (i < s.nearly) {
			bool ok = s.early[i].ok;
			s.early[i] = s.early[--s.nearly];
			if (ok) answer(j, NULL);
			else answer_stopped(j);
		} else {
			j->next = s.waiting;
			s.waiting = j;
		}
	}
	if (s.pending == 0) s.nearly = 0;
	asuka_error_free(&w->err);
	free(w);
}

// speak_thread sends the settings, SPEAK and the text; the reply has the
// message's ID.
static void speak_thread(void *p) {
	work *w = p;
	job *j = w->j;
	conn *c = w->c;
	pthread_mutex_lock(&c->send);
	bool failed = false;
	for (size_t i = 0; i < j->nsettings && !failed; i++) {
		reply *r = command_locked(c, j->settings[i], &w->err);
		if (!r) failed = true;
		else if (!reply_ok(r) && !strstr(j->settings[i], "SYNTHESIS_VOICE") && !strstr(j->settings[i], "LANGUAGE")) reply_fail(r, &w->err), failed = true;
		reply_free(r);
	}
	reply *r = failed ? NULL : command_locked(c, "SPEAK", &w->err);
	if (r && r->code != 230) reply_fail(r, &w->err), failed = true;
	reply_free(r);
	r = NULL;
	if (!failed && !w->err.status) {
		// The text: lines, a lone "." escaped as "..", then ".".
		asuka_writer b = {0};
		for (const char *t = j->text; t;) {
			const char *nl = strchr(t, '\n');
			size_t n = nl ? (size_t)(nl - t) : strlen(t);
			if (n > 0 && t[n - 1] == '\r') n--;
			if (*t == '.') asuka_writer_u8(&b, '.');
			asuka_writer_put(&b, t, n);
			asuka_writer_put(&b, "\r\n", 2);
			t = nl ? nl + 1 : NULL;
		}
		asuka_writer_u8(&b, '.');
		asuka_writer_u8(&b, 0);
		r = command_locked(c, (const char *)b.data, &w->err);
		asuka_writer_free(&b);
		if (r && (!reply_ok(r) || !r->n)) reply_fail(r, &w->err);
		else if (r) j->id = atoi(r->lines[0]);
		reply_free(r);
	}
	pthread_mutex_unlock(&c->send);
	asuka_desktop_post(spoke, w);
}

static void listed(void *p) {
	work *w = p;
	job *j = w->j;
	conn_unref(w->c);
	asuka_call *call = j->call;
	if (w->err.status) {
		asuka_call_fail(call, &w->err);
	} else {
		// LIST SYNTHESIS_VOICES's lines: name, language, variant by tabs;
		// the last line the status.
		speech_list_Voice vs = {0};
		reply *r = w->r;
		vs.data = calloc(r->n ? r->n : 1, sizeof *vs.data);
		for (size_t i = 0; i + 1 < r->n; i++) {
			char *l = r->lines[i], *tab = strchr(l, '\t');
			if (!tab || tab == l) continue;
			char *lang = asuka_strndup(tab + 1, strcspn(tab + 1, "\t"));
			char *dash = strchr(lang, '-');
			for (char *q = lang; *q; q++) {
				if (!dash || q < dash) *q = (char)(*q >= 'A' && *q <= 'Z' ? *q + 32 : *q);
				else *q = (char)(*q >= 'a' && *q <= 'z' ? *q - 32 : *q);
			}
			char *id = asuka_strndup(l, (size_t)(tab - l));
			vs.data[vs.len++] = (speech_Voice){.id = id, .name = asuka_strdup(id), .language = lang};
		}
		speech_Synthesizer_voices_done(call, vs);
	}
	reply_free(w->r);
	job_free(j);
	asuka_error_free(&w->err);
	free(w);
}

static void voices_thread(void *p) {
	work *w = p;
	w->r = command(w->c, "LIST SYNTHESIS_VOICES", &w->err);
	if (w->r && !reply_ok(w->r)) reply_fail(w->r, &w->err);
	asuka_desktop_post(listed, w);
}

static void dispatch(job *j) {
	if (!j->call) {  // cancelled while it waited
		job_free(j);
		return;
	}
	work *w = calloc(1, sizeof *w);
	w->j = j;
	w->c = s.conn;
	conn_ref(s.conn);
	j->busy = true;
	if (!j->voices) s.pending++;
	if (!asuka_desktop_spawn(j->voices ? voices_thread : speak_thread, w)) {
		asuka_error_failed(&w->err, "speech", 0, "speech: no thread");
		if (j->voices) listed(w);
		else spoke(w);
	}
}

typedef struct {
	conn *c;
	char *cmd;
} sending;

static void command_thread(void *p) {
	sending *w = p;
	asuka_error e = {0};
	reply_free(command(w->c, w->cmd, &e));
	asuka_error_free(&e);
	conn_unref(w->c);
	free(w->cmd);
	free(w);
}

// run sends a command on the connection, if there's one, off the main
// thread.
static void run(const char *cmd) {
	if (!s.conn) return;
	sending *w = malloc(sizeof *w);
	*w = (sending){s.conn, asuka_strdup(cmd)};
	conn_ref(s.conn);
	if (!asuka_desktop_spawn(command_thread, w)) conn_unref(w->c), free(w->cmd), free(w);
}

// ---- The interface ----

// scale is a multiplier (0.5 to 2, 1 normal) as SSIP's -100 to 100.
static int scale(double x) {
	if (x <= 0) x = 1;
	double v = 100 * log2(x);
	return (int)lround(v < -100 ? -100 : v > 100 ? 100 : v);
}

// user_language is the user's language (LANGUAGE, LC_ALL, LC_MESSAGES,
// LANG), as a BCP 47 tag.
static char *user_language(void) {
	const char *keys[] = {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"};
	for (int i = 0; i < 4; i++) {
		const char *v = getenv(keys[i]);
		if (!v) continue;
		char *t = asuka_strndup(v, strcspn(v, ":"));
		t[strcspn(t, ".")] = 0;
		if (*t && strcmp(t, "C") && strcmp(t, "POSIX")) {
			for (char *q = t; *q; q++)
				if (*q == '_') *q = '-';
			return t;
		}
		free(t);
	}
	return NULL;
}

static void stop_all(void) {
	ended(NULL);
	s.paused = false;
	run("CANCEL self");
}

// The task that waits went: its utterance answers cancelled, and
// speaking stops.
static void on_cancel(void *p) {
	job *j = p;
	if (!unlink_job(&s.waiting, j)) unlink_job(&s.dialing, j);
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	answer(j, &e);  // frees it unless a worker has it
	stop_all();
}

static void y_speak(speech_Synthesizer *self, const char *text, const speech_Options *o, asuka_call *call) {
	job *j = calloc(1, sizeof *j);
	j->call = call;
	j->text = asuka_strdup(text);
	j->settings = calloc(5, sizeof *j->settings);
	double vol = o->volume < 0 ? 0 : o->volume > 1 ? 1 : o->volume;
	j->settings[j->nsettings++] = asuka_sprintf("SET self RATE %d", scale(o->rate));
	j->settings[j->nsettings++] = asuka_sprintf("SET self PITCH %d", scale(o->pitch));
	j->settings[j->nsettings++] = asuka_sprintf("SET self VOLUME %d", (int)lround(vol * 200 - 100));
	char *lang = o->language && *o->language ? asuka_strdup(o->language) : user_language();
	if (lang && *lang) {
		for (char *q = lang; *q; q++) *q = *q == '_' ? '-' : (char)(*q >= 'A' && *q <= 'Z' ? *q + 32 : *q);
		j->settings[j->nsettings++] = asuka_sprintf("SET self LANGUAGE %s", lang);
	}
	free(lang);
	if (o->voice && *o->voice) j->settings[j->nsettings++] = asuka_sprintf("SET self SYNTHESIS_VOICE %s", o->voice);
	asuka_call_on_cancel(call, on_cancel, j);
	connect_for(j);
}

static void y_stop(speech_Synthesizer *self, asuka_error *err) { stop_all(); }

static void y_pause(speech_Synthesizer *self, asuka_error *err) {
	if (!s.conn) return;
	s.paused = true;
	run("PAUSE self");
}

static void y_resume(speech_Synthesizer *self, asuka_error *err) {
	if (!s.conn) return;
	s.paused = false;
	run("RESUME self");
}

static bool y_speaking(speech_Synthesizer *self, asuka_error *err) { return s.waiting || s.pending > 0; }

static void y_voices(speech_Synthesizer *self, asuka_call *call) {
	job *j = calloc(1, sizeof *j);
	j->call = call;
	j->voices = true;
	connect_for(j);
}

static void y_release(speech_Synthesizer *self) {}  // the one synthesizer lives with the app

static const speech_Synthesizer_vtable vt = {.release = y_release,
                                             .speak = y_speak,
                                             .stop = y_stop,
                                             .pause = y_pause,
                                             .resume = y_resume,
                                             .speaking = y_speaking,
                                             .voices = y_voices};

speech_Synthesizer *speech_make_Synthesizer(asuka_error *err) {
	static speech_Synthesizer synth = {&vt};
	return &synth;
}
