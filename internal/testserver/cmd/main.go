// Command cmd serves testserver's paths for tests that aren't Go's (the
// web's under Node, Kotlin's, the desktops' C): it prints its base URL
// (http://127.0.0.1:<port>) on a line, then serves until its standard
// input closes.
//
//	go run ./internal/testserver/cmd
package main

import (
	"fmt"
	"io"
	"log"
	"net/http"
	"os"

	"github.com/egoist/asuka/internal/testserver"
)

func main() {
	ln, err := testserver.Listener()
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("http://%s\n", ln.Addr())
	go http.Serve(ln, testserver.Handler())
	io.Copy(io.Discard, os.Stdin)
}
