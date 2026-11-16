// Command asuka-host is a macOS or Linux app's executable (package
// host/apphost): the platform's Go host, which runs the app on the C core
// beside it. asuka build copies it into the app as the app's own
// executable; it's the same for every app. (A Windows app is the C host
// of platform/windows, with the core and the app linked in.)
package main

import "github.com/egoist/asuka/host/apphost"

func main() { apphost.Main() }
