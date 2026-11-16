package lsp

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/textproto"
	"strconv"
	"strings"
	"sync"
)

// JSON-RPC 2.0 over a stream, framed as LSP frames it: a Content-Length
// header, a blank line, the JSON.

// A message is a request (ID and Method), a notification (Method), or a
// response (ID, and Result or Error).
type message struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"`
	Params  json.RawMessage `json:"params,omitempty"`
	Result  json.RawMessage `json:"result,omitempty"`
	Error   *rpcError       `json:"error,omitempty"`
}

func (m *message) isRequest() bool      { return m.Method != "" && len(m.ID) > 0 }
func (m *message) isNotification() bool { return m.Method != "" && len(m.ID) == 0 }

type rpcError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

func (e *rpcError) Error() string { return fmt.Sprintf("%s (%d)", e.Message, e.Code) }

// The error codes LSP uses.
const (
	codeParseError       = -32700
	codeInvalidRequest   = -32600
	codeMethodNotFound   = -32601
	codeInvalidParams    = -32602
	codeInternalError    = -32603
	codeServerNotInit    = -32002
	codeRequestCancelled = -32800
	codeRequestFailed    = -32803
)

// conn reads and writes framed messages. Writes may come from any
// goroutine.
type conn struct {
	r   *bufio.Reader
	mu  sync.Mutex
	w   io.Writer
	err error // the first write error
}

func newConn(r io.Reader, w io.Writer) *conn {
	return &conn{r: bufio.NewReader(r), w: w}
}

// read returns the next message; io.EOF at the end of the stream.
func (c *conn) read() (*message, error) {
	tp := textproto.NewReader(c.r)
	length := -1
	for {
		line, err := tp.ReadLine()
		if err != nil {
			if errors.Is(err, io.EOF) && length < 0 {
				return nil, io.EOF
			}
			return nil, err
		}
		if line == "" {
			break
		}
		name, value, ok := strings.Cut(line, ":")
		if !ok {
			return nil, fmt.Errorf("lsp: bad header line %q", line)
		}
		if strings.EqualFold(strings.TrimSpace(name), "Content-Length") {
			n, err := strconv.Atoi(strings.TrimSpace(value))
			if err != nil || n < 0 {
				return nil, fmt.Errorf("lsp: bad Content-Length %q", value)
			}
			length = n
		}
	}
	if length < 0 {
		return nil, errors.New("lsp: a message without Content-Length")
	}
	body := make([]byte, length)
	if _, err := io.ReadFull(c.r, body); err != nil {
		return nil, err
	}
	m := &message{}
	if err := json.Unmarshal(body, m); err != nil {
		return m, &rpcError{Code: codeParseError, Message: err.Error()}
	}
	return m, nil
}

func (c *conn) write(m *message) error {
	m.JSONRPC = "2.0"
	body, err := json.Marshal(m)
	if err != nil {
		return err
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.err != nil {
		return c.err
	}
	if _, err := fmt.Fprintf(c.w, "Content-Length: %d\r\n\r\n", len(body)); err != nil {
		c.err = err
		return err
	}
	if _, err := c.w.Write(body); err != nil {
		c.err = err
		return err
	}
	return nil
}

func (c *conn) notify(method string, params any) error {
	p, err := json.Marshal(params)
	if err != nil {
		return err
	}
	return c.write(&message{Method: method, Params: p})
}

func (c *conn) reply(id json.RawMessage, result any, err error) error {
	m := &message{ID: id}
	if err != nil {
		var re *rpcError
		if !errors.As(err, &re) {
			re = &rpcError{Code: codeRequestFailed, Message: err.Error()}
		}
		m.Error = re
	} else {
		r, merr := json.Marshal(result)
		if merr != nil {
			m.Error = &rpcError{Code: codeInternalError, Message: merr.Error()}
		} else {
			m.Result = r
		}
	}
	return c.write(m)
}
