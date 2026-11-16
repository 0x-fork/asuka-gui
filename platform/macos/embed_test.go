package macos

import (
	"bytes"
	"os"
	"testing"
)

// The Swift the iOS host and the Mac's share is one file each in both:
// typed native modules' runtime, StoreKit's and iCloud's, and the ops
// decoder (go generate ./ops writes both copies of Ops.swift).
func TestSwiftShared(t *testing.T) {
	for mac, ios := range map[string]string{
		"Sources/AsukaHost/NativeModules.swift": "Sources/AsukaHost/NativeModules.swift",
		"Sources/AsukaHost/Purchase.swift":      "Sources/AsukaHost/Purchase.swift",
		"Sources/AsukaHost/ICloud.swift":        "Sources/AsukaHost/ICloud.swift",
		"Sources/AsukaOps/Ops.swift":            "Sources/AsukaOps/Ops.swift",
		"Sources/AsukaOps/Actions.swift":        "Sources/AsukaOps/Actions.swift",
		"Sources/AsukaOps/AlertFields.swift":    "Sources/AsukaOps/AlertFields.swift",
		"Sources/AsukaOps/Keys.swift":           "Sources/AsukaOps/Keys.swift",
		"Sources/AsukaOps/Sidebar.swift":        "Sources/AsukaOps/Sidebar.swift",
	} {
		a, err := os.ReadFile("../ios/" + ios)
		if err != nil {
			t.Fatal(err)
		}
		b, err := Files.ReadFile(mac)
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(a, b) {
			t.Errorf("platform/macos/%s isn't the iOS host's platform/ios/%s: copy it", mac, ios)
		}
	}
}

// The hosts' copies of the core's header are the core's.
func TestHeader(t *testing.T) {
	core, err := os.ReadFile("../../core/include/asuka.h")
	if err != nil {
		t.Fatal(err)
	}
	for _, p := range []string{"Sources/CAsuka/include/asuka.h", "../ios/Sources/CAsuka/include/asuka.h"} {
		var b []byte
		if p[0] == '.' {
			b, err = os.ReadFile(p)
		} else {
			b, err = Files.ReadFile(p)
		}
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(core, b) {
			t.Errorf("%s isn't core/include/asuka.h: copy it", p)
		}
	}
}
