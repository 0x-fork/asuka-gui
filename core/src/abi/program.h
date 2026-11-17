// program.h: running a program (program.c): a module whose fn main is
// its entry, with no UI host. asuka.h's asuka_program_main and
// asuka_program_start are these over the program compiled into the
// build; the runner (core/runner, asuka run) gives it a module of
// bytecode.
#ifndef AK_PROGRAM_H
#define AK_PROGRAM_H

#include "asuka.h"
#include "vm/vm.h"

// ak_program_start runs m's init, then starts its main (the core set up:
// the VM, its builtins), with argv the arguments (argv[0] the program)
// and cb a host's callbacks or NULL. It returns the exit code when the
// program has ended already, else -1: ak_program_loop runs it to its end.
int ak_program_start(ak_module *m, int argc, char **argv, const asuka_host_callbacks *cb);
// ak_program_trim makes the files an error's trace names relative to dir
// (the project's: asuka run's).
void ak_program_trim(const char *dir);
// ak_program_data, when a build sets it (lang/cgen), names the folder
// the program's data goes in: an app's, which the program ships with.
extern const char *ak_program_data;
// ak_program_loop runs the main thread's loop until the program ends;
// its exit code.
int ak_program_loop(void);

#endif
