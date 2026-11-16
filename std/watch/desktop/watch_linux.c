// asuka/watch on Linux: inotify, a watch on each directory looked at
// (the file's directory, for a file), read on a thread of the watch's,
// woken through a pipe to stop.
#include "watch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

struct wnative {
	int fd;
	int wake[2];
};

enum { MASK = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_DONT_FOLLOW | IN_EXCL_UNLINK };

static wentry entry(const char *rel, const struct stat *st) {
	return (wentry){asuka_strdup(rel), S_ISDIR(st->st_mode), (uint64_t)st->st_size,
	                (uint64_t)st->st_mtim.tv_sec * 1000000000u + (uint64_t)st->st_mtim.tv_nsec, (uint64_t)st->st_ino ^ (uint64_t)st->st_dev << 48,
	                (uint32_t)(st->st_mode & 07777)};
}

static void walk(watcher *w, const char *path, const char *rel, wsnap *out) {
	if (w->native) inotify_add_watch(w->native->fd, path, MASK);
	DIR *d = opendir(path);
	if (!d) return;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
		char *p = asuka_sprintf("%s/%s", path, de->d_name);
		char *r = strcmp(rel, ".") == 0 ? asuka_strdup(de->d_name) : asuka_sprintf("%s/%s", rel, de->d_name);
		struct stat st;
		if (lstat(p, &st) == 0 && !watch_ignored(&w->ignore, r, S_ISDIR(st.st_mode))) {
			wsnap_add(out, entry(r, &st));
			if (S_ISDIR(st.st_mode) && w->recursive) walk(w, p, r, out);  // not through symlinks: lstat
		}
		free(p);
		free(r);
	}
	closedir(d);
}

bool watch_scan(watcher *w, wsnap *out) {
	struct stat st;
	if (lstat(w->root, &st) != 0 || S_ISDIR(st.st_mode) != w->root_dir) return false;
	wsnap_add(out, entry(".", &st));
	if (w->root_dir) {
		walk(w, w->root, ".", out);
	} else if (w->native) {
		// A file, through its directory: replaced at its path, it's written.
		const char *slash = strrchr(w->root, '/');
		char *dir = slash && slash != w->root ? asuka_strndup(w->root, (size_t)(slash - w->root)) : asuka_strdup("/");
		inotify_add_watch(w->native->fd, dir, MASK & ~(IN_DELETE_SELF | IN_MOVE_SELF));
		free(dir);
	}
	return true;
}

static void run(void *p) {
	watcher *w = p;
	wnative *n = w->native;
	char buf[64 * 1024] __attribute__((aligned(__alignof__(struct inotify_event))));
	while (!watch_stopping(w)) {
		struct pollfd fds[2] = {{n->fd, POLLIN, 0}, {n->wake[0], POLLIN, 0}};
		int r = poll(fds, 2, watch_timeout(w));
		if (r < 0 && errno != EINTR) break;
		if (fds[1].revents) break;
		if (fds[0].revents & POLLIN) {
			bool overflow = false;
			ssize_t got;
			while ((got = read(n->fd, buf, sizeof buf)) > 0) {
				for (char *q = buf; q < buf + got;) {
					struct inotify_event *e = (struct inotify_event *)q;
					if (e->mask & IN_Q_OVERFLOW) overflow = true;
					q += sizeof *e + e->len;
				}
			}
			watch_signal(w, overflow);
		}
		if (!watch_settle(w)) break;
	}
	watch_exit(w);
}

bool watch_native_start(watcher *w, asuka_error *err) {
	wnative *n = calloc(1, sizeof *n);
	n->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (n->fd < 0 || pipe2(n->wake, O_CLOEXEC) != 0) {
		asuka_error_failed(err, "errno", errno, "watch: inotify: %s", strerror(errno));
		if (n->fd >= 0) close(n->fd);
		free(n);
		return false;
	}
	w->native = n;
	if (!watch_scan(w, &w->snap)) {
		asuka_error_failed(err, "watch", 0, "watch: %s went", w->root);
		return false;
	}
	if (!asuka_desktop_spawn(run, w)) {
		asuka_error_failed(err, "watch", 0, "no thread");
		return false;
	}
	return true;
}

void watch_native_wake(watcher *w) {
	if (w->native && write(w->native->wake[1], "x", 1) < 0) {
		// The pipe's full: the thread wakes anyway.
	}
}

void watch_native_free(watcher *w) {
	wnative *n = w->native;
	if (!n) return;
	close(n->fd);
	close(n->wake[0]);
	close(n->wake[1]);
	free(n);
	w->native = NULL;
}
