//go:build linux && !android

package dialog

import (
	"os"
	"path/filepath"

	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux: GtkFileDialog (GTK's own dialog, or the desktop's file chooser
// portal in a sandbox), modal to the window the user works in. GTK's
// dialog has its own toggle for hidden files (Ctrl+H).

func supported() error { return nil }

// fileDialog shows GTK's file dialog to open files (kind "open"), save
// one ("save") or choose folders ("folder"). Main thread.
func fileDialog(kind string, r Request, done func(chosen)) {
	d := Sys.FileDialogNew()
	Sys.FileDialogSetModal(d, true)
	if r.Title != "" {
		Sys.FileDialogSetTitle(d, r.Title)
	}
	if r.Button != "" {
		Dialogs.FileDialogSetAcceptLabel(d, r.Button)
	}
	if r.Start != "" {
		if st, err := os.Stat(r.Start); err == nil && st.IsDir() {
			setInitialFolder(d, r.Start)
		} else {
			// A file: its folder. GTK's open dialog (and the portal's)
			// selects nothing in it; save suggests its name.
			setInitialFolder(d, filepath.Dir(r.Start))
			if kind == "save" && r.Name == "" {
				Dialogs.FileDialogSetInitialName(d, filepath.Base(r.Start))
			}
		}
	}
	if kind == "save" && r.Name != "" {
		Dialogs.FileDialogSetInitialName(d, r.Name)
	}
	if kind != "folder" && len(r.Filters) > 0 {
		store := Sys.ListStoreNew(Sys.FileFilterGetType())
		for i, f := range r.Filters {
			ff := Sys.FileFilterNew()
			Sys.FileFilterSetName(ff, f.Name)
			for _, t := range f.Types {
				if t == "*/*" || t == "*" {
					Dialogs.FileFilterAddPattern(ff, "*")
				} else {
					Sys.FileFilterAddMIMEType(ff, t) // "image/*" too
				}
			}
			for _, e := range f.Extensions {
				if e == "*" {
					Dialogs.FileFilterAddPattern(ff, "*")
				} else {
					Sys.FileFilterAddSuffix(ff, e)
				}
			}
			Sys.ListStoreAppend(store, ff)
			if i == 0 {
				Sys.FileDialogSetDefaultFilter(d, ff)
			}
			ObjectUnref(ff)
		}
		Sys.FileDialogSetFilters(d, store)
		ObjectUnref(store)
	}

	parent := native.Window()
	finish := func(paths []string, gerr Ptr) {
		ObjectUnref(d)
		switch {
		case gerr != 0 && Cancelled(gerr):
			ErrorFree(gerr)
			done(chosen{})
		case gerr != 0:
			done(chosen{err: GError(gerr)})
		default:
			done(chosen{paths: paths})
		}
	}
	// one finishes a method that gives a GFile; many one that gives a
	// GListModel of them.
	one := func(fin func(d, res Ptr, err *Ptr) Ptr) func(_, res Ptr) {
		return func(_, res Ptr) {
			var gerr Ptr
			var paths []string
			if f := fin(d, res, &gerr); f != 0 {
				if p := GoStringFree(Sys.FileGetPath(f)); p != "" {
					paths = append(paths, p)
				}
				ObjectUnref(f)
			}
			finish(paths, gerr)
		}
	}
	many := func(fin func(d, res Ptr, err *Ptr) Ptr) func(_, res Ptr) {
		return func(_, res Ptr) {
			var gerr Ptr
			var paths []string
			if list := fin(d, res, &gerr); list != 0 {
				n := Sys.ListModelGetNItems(list)
				for i := uint32(0); i < n; i++ {
					f := Sys.ListModelGetItem(list, i)
					if p := GoStringFree(Sys.FileGetPath(f)); p != "" {
						paths = append(paths, p)
					}
					ObjectUnref(f)
				}
				ObjectUnref(list)
			}
			finish(paths, gerr)
		}
	}
	switch {
	case kind == "save":
		Async(func(c AsyncCall) { Dialogs.FileDialogSave(d, parent, 0, c.Callback, c.Data) }, one(Dialogs.FileDialogSaveFinish))
	case kind == "folder" && r.Multiple:
		Async(func(c AsyncCall) { Dialogs.FileDialogSelectFolders(d, parent, 0, c.Callback, c.Data) }, many(Dialogs.FileDialogSelectFoldersFin))
	case kind == "folder":
		Async(func(c AsyncCall) { Dialogs.FileDialogSelectFolder(d, parent, 0, c.Callback, c.Data) }, one(Dialogs.FileDialogSelectFolderFin))
	case r.Multiple:
		Async(func(c AsyncCall) { Sys.FileDialogOpenMultiple(d, parent, 0, c.Callback, c.Data) }, many(Sys.FileDialogOpenMultipleFin))
	default:
		Async(func(c AsyncCall) { Sys.FileDialogOpen(d, parent, 0, c.Callback, c.Data) }, one(Sys.FileDialogOpenFinish))
	}
}

func setInitialFolder(d Ptr, dir string) {
	f := Sys.FileNewForPath(dir)
	Dialogs.FileDialogSetInitialFolder(d, f)
	ObjectUnref(f)
}
