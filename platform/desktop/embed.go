// Package desktop embeds the C the Windows and Linux hosts share
// (include/desktop.h, src/), for the asuka CLI, which builds it into a
// desktop app's executable with the host and the core; test/ holds its
// tests (desktop_test.go).
package desktop

//go:generate go run gen_words.go

