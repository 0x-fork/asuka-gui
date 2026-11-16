// Package macos embeds Asuka's macOS host (Sources: the Swift host and its
// decoder, the core's header; App: its entry point), for the Asuka CLI,
// which compiles it with an app's native packages' Swift and links it with
// the C core into the app's executable.
package macos

import "embed"

// Files holds Sources and App.
//
//go:embed Sources App
var Files embed.FS

// HostDefines are the host's compilation conditions: what the iOS host's
// files it shares build in (iCloud's, ICloud.swift). Every app's host has
// them all.
var HostDefines = []string{"ASUKA_ICLOUD"}
