// nowplaying.h: what nowplaying's Windows and Linux C share
// (nowplaying.c): the watchers that hear the commands.
#ifndef NOWPLAYING_H
#define NOWPLAYING_H

#include "nowplaying.asuka.h"

// nowplaying_send gives each watcher a command, if info enables it (a
// skip without its length: info's). Main thread.
void nowplaying_send(const nowplaying_Info *info, nowplaying_Command c, double v);
// nowplaying_has reports whether info enables c.
bool nowplaying_has(const nowplaying_Info *info, nowplaying_Command c);
// nowplaying_copy copies an Info (what set keeps), nowplaying_free_info
// frees one.
nowplaying_Info *nowplaying_copy(const nowplaying_Info *info);
void nowplaying_free_info(nowplaying_Info *info);
// nowplaying_watch is every platform's watch: a watcher until the task
// that watches goes.
void nowplaying_watch(nowplaying_Center *self, nowplaying_fn_Command_float command, asuka_call *call);

#endif
