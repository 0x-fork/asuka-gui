// webauth.h: what webauth's Windows and Linux C share (webauth.c).
#ifndef WEBAUTH_H
#define WEBAUTH_H

#include "webauth.asuka.h"

// A sign-in: a window over the app's with a web view of the platform's,
// which loads the URL; it ends when a page navigates to the callback
// scheme (the navigation stopped, its URL the answer), as the user closes
// it (canceled), or as the call is cancelled. Main thread.
typedef struct signin signin;

struct signin {
	char *link, *host, *scheme;
	bool ephemeral, done;
	asuka_call *call;
	void *platform;
};

// webauth_show is the platform's: it shows the sign-in.
void webauth_show(signin *s);
// webauth_finish answers the sign-in, once (to: the callback's URL; NULL:
// canceled, with err if not the user's), and has the platform close it.
void webauth_finish(signin *s, const char *to, asuka_error *err);
// webauth_close is the platform's: it closes the window, then frees s
// (webauth_free) on a later turn.
void webauth_close(signin *s);
void webauth_free(signin *s);
// webauth_is_callback reports whether a URL is of the scheme.
bool webauth_is_callback(const char *url, const char *scheme);
// webauth_host is a URL's host, as a title shows it.
char *webauth_host(const char *url);

#endif
