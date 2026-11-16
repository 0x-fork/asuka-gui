// Package e2e drives the example apps and the apps in testdata on a
// simulator, an emulator, headless Chrome and the desktops, through the
// platforms' own widgets. Its tests are behind the e2e build tag:
//
//	go test -tags e2e ./e2e -run Web
//
// e2e_test.go says how each platform is driven.
package e2e
