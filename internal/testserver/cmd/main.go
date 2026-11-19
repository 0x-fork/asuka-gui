// Command cmd serves testserver's paths for tests that aren't Go's (the
// web's under Node, Kotlin's, the desktops' C): it prints its base URL
// (http://127.0.0.1:<port>) on a line, the same paths' over HTTPS
// (https://127.0.0.1:<port>, a test authority's certificate) on the
// next, and the directory of that authority's PEM files (PKI.WriteFiles':
// ca.pem, client.pem, client.key...) on the third, then serves until its
// standard input closes.
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
	pki, err := testserver.NewPKI()
	if err != nil {
		log.Fatal(err)
	}
	dir, err := os.MkdirTemp("", "asuka-testserver-pki")
	if err != nil {
		log.Fatal(err)
	}
	defer os.RemoveAll(dir)
	if err := pki.WriteFiles(dir); err != nil {
		log.Fatal(err)
	}
	s := testserver.StartTLS(testserver.Handler(), pki)
	defer s.Close()
	fmt.Printf("http://%s\n%s\n%s\n", ln.Addr(), s.URL, dir)
	go http.Serve(ln, testserver.Handler())
	io.Copy(io.Discard, os.Stdin)
}
