// dev.h: asuka dev's connection to a player. The player (a host built
// for development: the iOS simulator's or a phone's, an emulator's, a
// desktop's) connects to the CLI; the CLI sends it the app's bytecode,
// and again on every save; the player sends back its logs.
//
// Frames both ways: u32 length (of what follows, little-endian), u8
// kind, payload.
//   CLI → player:  1 module (bytecode)    2 ping
//   player → CLI:  3 log (u8 fd: 1 out, 2 err; then the text)
//                  4 hello (JSON: the platform, the device)
//                  5 reloaded (u32 state cells kept, u32 microseconds)
//                  6 error (text: a module that didn't load, an uncaught error)
#ifndef AK_DEV_H
#define AK_DEV_H

#include "base/base.h"

enum {
	AK_DEV_MODULE = 1,
	AK_DEV_PING = 2,
	AK_DEV_LOG = 3,
	AK_DEV_HELLO = 4,
	AK_DEV_RELOADED = 5,
	AK_DEV_ERROR = 6,
};

// ak_dev_set_reload sets what a module that arrives is given to, on the
// main thread (the ABI's reload: keep state, swap the module, mount).
void ak_dev_set_reload(void (*reload)(const uint8_t *module, size_t len));

// ak_dev_connect connects to the CLI at host:port and starts reading
// its frames on a thread of its own; hello is the player's JSON. With
// capture, what the process writes to stdout and stderr is sent too.
// False when it can't connect (an error in err).
bool ak_dev_connect(const char *host, int port, const char *hello, bool capture, char *err, size_t errcap);

// ak_dev_send sends a frame to the CLI (any thread); a no-op when not
// connected.
void ak_dev_send(uint8_t kind, const void *p, size_t len);
bool ak_dev_connected(void);

#endif
