// Command asuka-host is a desktop app's executable: the platform's host
// (AppKit on macOS, Win32 on Windows, GTK on Linux), which runs the app
// on the C core beside it (libasuka.dylib, asuka.dll, libasuka.so: the
// core with the app compiled into it), or, under asuka dev (ASUKA_DEV),
// the bytecode the CLI sends. asuka build copies it into the app as the
// app's own executable; it's the same for every app.
package main

func main() { run() }
