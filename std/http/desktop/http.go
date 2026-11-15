// asuka/http on Windows and Linux: Go's net/http, with the system's
// proxy settings and certificates.
package http

import (
	"bytes"
	"context"
	"io"
	nethttp "net/http"
	"strings"
	"time"
)

type goTransport struct{}

func newTransport() (Transport, error) { return goTransport{}, nil }

func (goTransport) Send(ctx context.Context, method, url string, headers map[string]string, body []byte, timeout time.Duration) (Response, error) {
	if timeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, timeout)
		defer cancel()
	}
	var rd io.Reader
	if len(body) > 0 {
		rd = bytes.NewReader(body)
	}
	req, err := nethttp.NewRequestWithContext(ctx, method, url, rd)
	if err != nil {
		return Response{}, err
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	resp, err := nethttp.DefaultClient.Do(req)
	if err != nil {
		return Response{}, err
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return Response{}, err
	}
	out := map[string]string{}
	for k, v := range resp.Header {
		out[strings.ToLower(k)] = strings.Join(v, ", ")
	}
	return Response{Status: resp.StatusCode, Url: resp.Request.URL.String(), Headers: out, Body: data}, nil
}
