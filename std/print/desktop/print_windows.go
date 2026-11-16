//go:build windows

package print

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"os"

	"github.com/egoist/asuka/internal/localfile"
	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: a file's app prints it (the shell's print verb); markup prints
// from a WebView2 page that's never shown, through its print dialog (the
// system's), which doesn't say what the user did: unknown. A PDF is
// Chromium's printing to PDF (DevTools' Page.printToPDF).

func supported() error { return nil }

// lastPage is the page html printed last, kept while its dialog may still
// print it (WebView2 doesn't say when it's done).
var lastPage *WebPage

func (printer) File(_ context.Context, source string, _ Job) (Outcome, error) {
	path, err := localfile.App(source, "")
	if err != nil {
		return OutcomeCancelled, err
	}
	native.OnMain(func() { err = ShellExecute(native.Window(), "print", path) })
	if errors.Is(err, ErrNoAssociation) {
		return OutcomeCancelled, ErrUnsupported
	}
	if err != nil {
		return OutcomeCancelled, err
	}
	return OutcomeUnknown, nil
}

func (printer) Html(ctx context.Context, markup string, _ Job) (Outcome, error) {
	_, err := wait(ctx, func(done func(struct{}, error)) {
		LoadWebPage(localfile.AppName(), markup, func(p *WebPage, err error) {
			if err != nil {
				done(struct{}{}, err)
				return
			}
			if lastPage != nil {
				lastPage.Close()
			}
			lastPage = p
			ShowPrintUI(p.View, PrintDialogSystem)
			done(struct{}{}, nil)
		})
	})
	if err != nil {
		return OutcomeCancelled, err
	}
	return OutcomeUnknown, nil
}

func (printer) Pdf(ctx context.Context, l Layout) (string, error) {
	in := func(pt float64) float64 { return pt / 72 } // DevTools takes inches
	params := map[string]any{
		"paperWidth": in(l.Width), "paperHeight": in(l.Height),
		"marginTop": in(l.Top), "marginRight": in(l.Right), "marginBottom": in(l.Bottom), "marginLeft": in(l.Left),
		"printBackground": true, "preferCSSPageSize": false, "transferMode": "ReturnAsBase64",
	}
	type made struct {
		pdf   []byte
		title string
	}
	m, err := wait(ctx, func(done func(made, error)) {
		LoadWebPage(localfile.AppName(), l.Html, func(p *WebPage, err error) {
			if err != nil {
				done(made{}, err)
				return
			}
			p.Evaluate(native.Post, "document.title", func(t []byte, _ error) {
				var title string
				json.Unmarshal(t, &title)
				p.DevTools(native.Post, "Page.printToPDF", params, func(r json.RawMessage, err error) {
					p.Close()
					if err != nil {
						done(made{}, errors.New("print: the PDF couldn't be made"))
						return
					}
					var res struct {
						Data string `json:"data"`
					}
					if err := json.Unmarshal(r, &res); err != nil {
						done(made{}, err)
						return
					}
					pdf, err := base64.StdEncoding.DecodeString(res.Data)
					done(made{pdf, title}, err)
				})
			})
		})
	})
	if err != nil {
		return "", err
	}
	path, err := localfile.NewPDF(localfile.AppCacheDir(), m.title)
	if err == nil {
		err = os.WriteFile(path, m.pdf, 0o644)
	}
	return path, err
}
