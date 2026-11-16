//go:build linux && !android

package print

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"syscall"

	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/internal/localfile"
	"github.com/egoist/asuka/native"
)

// Linux: a file goes to the print portal (its dialog, then the printer:
// what CUPS takes, PDFs and images among them), or, without the portal,
// to GTK's print dialog and a print job; markup prints with WebKit (its
// operation, GTK's dialog), and a PDF is the markup laid out by WebKit and
// printed to a file.

func supported() error { return nil }

func outcome(printed bool) Outcome {
	if printed {
		return OutcomePrinted
	}
	return OutcomeCancelled
}

func (printer) File(ctx context.Context, source string, j Job) (Outcome, error) {
	path, err := localfile.App(source, "")
	if err != nil {
		return OutcomeCancelled, err
	}
	ok, err := wait(ctx, func(done func(bool, error)) { printFile(path, jobName(j, filepath.Base(path)), j.Landscape, done) })
	return outcome(ok), err
}

func (printer) Html(ctx context.Context, markup string, j Job) (Outcome, error) {
	ok, err := wait(ctx, func(done func(bool, error)) {
		loadPage(markup, func(v Ptr, err error) {
			if err != nil {
				done(false, err)
				return
			}
			ok, err := printView(v, j.Landscape, func() { ObjectUnref(v) })
			done(ok, err)
		})
	})
	return outcome(ok), err
}

func (printer) Pdf(ctx context.Context, l Layout) (string, error) {
	return wait(ctx, func(done func(string, error)) {
		loadPage(l.Html, func(v Ptr, err error) {
			if err != nil {
				done("", err)
				return
			}
			path, err := localfile.NewPDF(localfile.AppCacheDir(), GoString(WebKitWebViewGetTitle(v)))
			if err != nil {
				ObjectUnref(v)
				done("", err)
				return
			}
			printPDF(v, l, path, func(err error) {
				ObjectUnref(v)
				if err != nil {
					done("", err)
					return
				}
				done(path, nil)
			})
		})
	})
}

// loadPage loads markup in a web view that's never shown.
func loadPage(html string, done func(Ptr, error)) {
	if err := LoadWebKitPrint(); err != nil {
		done(0, ErrUnsupported)
		return
	}
	v := ObjectRefSink(WebKitWebViewNew())
	finished := false
	var hs []uint64
	end := func(err error) {
		if finished {
			return
		}
		finished = true
		for _, hd := range hs {
			Disconnect(v, hd)
		}
		if err != nil {
			ObjectUnref(v)
			done(0, err)
			return
		}
		done(v, nil)
	}
	hs = append(hs,
		Connect(v, "load-changed", func(ev uintptr) {
			if ev == WebKitLoadFinished {
				// Printing runs a dialog's loop: not inside the signal.
				IdleAdd(PriorityDefault, func() bool { end(nil); return false })
			}
		}),
		Connect(v, "load-failed", func(ev, uri, gerr uintptr) bool {
			end(errors.New("print: the page couldn't load: " + GoString(*At[Ptr](gerr + 8))))
			return true
		}),
	)
	WebKitWebViewLoadHTML(v, html, 0)
}

// printView shows the print dialog for a web view's page: it reports
// whether the user printed. after runs once the page is printed (or not).
func printView(v Ptr, landscape bool, after func()) (bool, error) {
	op := Print.OperationNew(v)
	if landscape {
		setup := Print.PageSetupNew()
		Print.SetOrientation(setup, 1) // GTK_PAGE_ORIENTATION_LANDSCAPE
		Print.OperationSetSetup(op, setup)
		ObjectUnref(setup)
	}
	resp := Print.OperationRunDialog(op, native.Window())
	printed := resp == 0 // WEBKIT_PRINT_OPERATION_RESPONSE_PRINT
	if !printed {
		ObjectUnref(op)
		if after != nil {
			after()
		}
		return false, nil
	}
	// Printing goes on: the operation (and the page) until it's done.
	var hs []uint64
	finish := func() {
		for _, hd := range hs {
			Disconnect(op, hd)
		}
		ObjectUnref(op)
		if after != nil {
			after()
		}
	}
	hs = append(hs,
		Connect(op, "finished", func() { IdleAdd(PriorityDefault, func() bool { finish(); return false }) }),
		Connect(op, "failed", func(uintptr) {}),
	)
	return true, nil
}

// printPDF prints a web view's page to a PDF file, on a page of the
// size and margins (points), without a dialog.
func printPDF(v Ptr, l Layout, path string, done func(error)) {
	op := Print.OperationNew(v)
	settings := Print.SettingsNew()
	Print.SettingsSetPrinter(settings, "Print to File")
	Print.SettingsSet(settings, "output-file-format", "pdf")
	f := Sys.FileNewForPath(path)
	Print.SettingsSet(settings, "output-uri", GoStringFree(Sys.FileGetURI(f)))
	ObjectUnref(f)
	setup := Print.PageSetupNew()
	size := Print.PaperSizeNewCustom("asuka", "Page", l.Width, l.Height, UnitPoints)
	Print.SetPaperSize(setup, size)
	Print.PaperSizeFree(size)
	Print.SetOrientation(setup, 0) // the size says it
	Print.SetTopMargin(setup, l.Top, UnitPoints)
	Print.SetBottomMargin(setup, l.Bottom, UnitPoints)
	Print.SetLeftMargin(setup, l.Left, UnitPoints)
	Print.SetRightMargin(setup, l.Right, UnitPoints)
	Print.OperationSetSettings(op, settings)
	Print.OperationSetSetup(op, setup)
	ObjectUnref(settings)
	ObjectUnref(setup)
	answered := false
	var hs []uint64
	end := func(err error) {
		if answered {
			return
		}
		answered = true
		for _, hd := range hs {
			Disconnect(op, hd)
		}
		IdleAdd(PriorityDefault, func() bool {
			ObjectUnref(op)
			if err == nil {
				if st, serr := os.Stat(path); serr != nil || st.Size() == 0 {
					err = errors.New("print: the PDF couldn't be written")
				}
			}
			done(err)
			return false
		})
	}
	hs = append(hs,
		Connect(op, "failed", func(gerr uintptr) {
			msg := "print: the PDF couldn't be made"
			if gerr != 0 {
				msg += ": " + GoString(*At[Ptr](gerr + 8))
			}
			end(errors.New(msg))
		}),
		Connect(op, "finished", func() { end(nil) }),
	)
	Print.OperationPrint(op)
}

// printFile prints a file: through the print portal, else GTK's print
// dialog. done gets whether it was printed.
func printFile(path, title string, landscape bool, done func(bool, error)) {
	fd, err := syscall.Open(path, syscall.O_RDONLY|syscall.O_CLOEXEC, 0)
	if err != nil {
		done(false, err)
		return
	}
	closeFD := func() {
		if fd >= 0 {
			syscall.Close(fd)
			fd = -1
		}
	}
	PortalRequest("org.freedesktop.portal.Print", "PreparePrint", func(token string) Ptr {
		setup := map[string]Ptr{}
		if landscape {
			setup["Orientation"] = VStr("landscape")
		}
		return VTuple(VStr(""), VStr(title), VDict(map[string]Ptr{}), VDict(setup),
			VDict(map[string]Ptr{"handle_token": VStr(token), "modal": VBool(true)}))
	}, nil, func(code uint32, results map[string]any, err error) {
		if ErrNoPortal(err) {
			closeFD()
			printFileDialog(path, title, done)
			return
		}
		if err != nil || code != 0 {
			closeFD()
			if code == 1 {
				done(false, nil)
			} else {
				done(false, err)
			}
			return
		}
		tok, _ := results["token"].(uint32)
		PortalRequest("org.freedesktop.portal.Print", "Print", func(token string) Ptr {
			return VTuple(VStr(""), VStr(title), VHandle(0),
				VDict(map[string]Ptr{"handle_token": VStr(token), "token": VU32(tok), "modal": VBool(true)}))
		}, []int32{int32(fd)}, func(code uint32, _ map[string]any, err error) {
			closeFD()
			switch {
			case err != nil:
				done(false, err)
			case code == 0:
				done(true, nil)
			default:
				done(false, nil)
			}
		})
	})
}

// printFileDialog prints a file with GTK's print dialog and a print job
// (no portal).
func printFileDialog(path, title string, done func(bool, error)) {
	if err := LoadWebKitPrint(); err != nil {
		done(false, ErrUnsupported)
		return
	}
	d := Print.UnixDialogNew(title, native.Window())
	Print.UnixDialogSetCaps(d, PrintPreviewCaps)
	WindowSetModal(d, true)
	answered := false
	Connect(d, "response", func(resp uintptr) {
		if answered {
			return
		}
		answered = true
		if int32(resp) != ResponseOK {
			WindowDestroy(d)
			done(false, nil)
			return
		}
		printer := Print.UnixDialogPrinter(d)
		settings := Print.UnixDialogSettings(d)
		setup := Print.UnixDialogSetup(d)
		job := Print.JobNew(title, printer, settings, setup)
		ObjectUnref(settings)
		WindowDestroy(d)
		var gerr Ptr
		if !Print.JobSetSourceFile(job, path, &gerr) {
			ObjectUnref(job)
			done(false, GError(gerr))
			return
		}
		PrintJobSend(job, func(err error) {
			ObjectUnref(job)
			done(err == nil, err)
		})
	})
	WindowPresent(d)
}
