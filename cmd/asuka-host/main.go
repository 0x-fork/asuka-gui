// Command asuka-host is a desktop app's executable (package
// host/apphost): the platform's host, which runs the app on the C core
// beside it. asuka build copies it into the app as the app's own
// executable (a Windows build links the app's icon, manifest and version
// into it); it's the same for every app.
package main

import "github.com/egoist/asuka/host/apphost"

func main() { apphost.Main() }
