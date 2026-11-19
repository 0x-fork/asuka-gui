// Command load drives an HTTP server (../../asuka's, ../'s, ../../bun's
// serve): c connections, each sending GET path and reading the answer,
// one after another (HTTP/1.1 keep-alive), for d; it prints requests a
// second and the latency's percentiles.
//
//	go run ./load -url http://127.0.0.1:8080/hello -c 50 -d 10s
package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"slices"
	"sync"
	"time"
)

func main() {
	target := flag.String("url", "http://127.0.0.1:8080/hello", "what to GET")
	conns := flag.Int("c", 50, "connections")
	dur := flag.Duration("d", 10*time.Second, "how long")
	warm := flag.Duration("warm", time.Second, "a warm-up first, not counted")
	flag.Parse()
	u, err := url.Parse(*target)
	if err != nil {
		panic(err)
	}
	req := []byte("GET " + u.RequestURI() + " HTTP/1.1\r\nHost: " + u.Host + "\r\nUser-Agent: load\r\n\r\n")
	var mu sync.Mutex
	var lat []time.Duration
	var errs int
	start := time.Now()
	measureFrom := start.Add(*warm)
	end := measureFrom.Add(*dur)
	var wg sync.WaitGroup
	for i := 0; i < *conns; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			var mine []time.Duration
			c, err := net.Dial("tcp", u.Host)
			if err != nil {
				mu.Lock()
				errs++
				mu.Unlock()
				return
			}
			defer c.Close()
			r := bufio.NewReader(c)
			for {
				t := time.Now()
				if t.After(end) {
					break
				}
				if _, err := c.Write(req); err != nil {
					errs++
					break
				}
				resp, err := http.ReadResponse(r, nil)
				if err == nil {
					_, err = io.Copy(io.Discard, resp.Body)
					resp.Body.Close()
				}
				if err != nil || resp.StatusCode != 200 {
					mu.Lock()
					errs++
					mu.Unlock()
					break
				}
				if t.After(measureFrom) {
					mine = append(mine, time.Since(t))
				}
			}
			mu.Lock()
			lat = append(lat, mine...)
			mu.Unlock()
		}()
	}
	wg.Wait()
	if len(lat) == 0 {
		fmt.Fprintln(os.Stderr, "no requests answered")
		os.Exit(1)
	}
	slices.Sort(lat)
	p := func(q float64) time.Duration { return lat[int(float64(len(lat)-1)*q)] }
	fmt.Printf("%.0f req/s  p50 %v  p99 %v  max %v  errors %d\n", float64(len(lat))/dur.Seconds(), p(0.5), p(0.99), lat[len(lat)-1], errs)
}
