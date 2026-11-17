package main

import (
	"context"
	"io"
	"os"
	"os/signal"

	"github.com/egoist/asuka/lang/lsp"
)

// cmdLSP runs the language server on stdin and stdout, for editors.
func cmdLSP(args []string) error {
	var stdio, verbose bool
	fs := newFlagSet("lsp", "")
	fs.about = "Runs the Asuka language server (LSP 3.17) on standard input and output, for editors:\n" +
		"diagnostics, hover, completion, go to definition, references, rename, formatting,\n" +
		"symbols, signature help and semantic tokens for .asuka files."
	fs.Bool(&stdio, "stdio", 0, "talk over standard input and output (the default, and the only way)")
	fs.Bool(&verbose, "verbose", 'v', "log what the server does to standard error")
	if err := fs.parse(args); err != nil {
		return err
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	var log io.Writer = os.Stderr
	if !verbose {
		log = nil
	}
	return lsp.Serve(ctx, os.Stdin, os.Stdout, &lsp.Options{Version: version, Log: log})
}
