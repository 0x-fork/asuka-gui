package update

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
)

// How an installed app is laid out, which is what an update replaces.
// asuka build makes each, and the hosts' update modules replace them
// (DESIGN.md, "Updates"):
//
//   - Bundle (macOS): Name.app, replaced whole once the app has quit, so
//     that the Dock keeps its tile and the code signature is the new
//     version's, sealed as it was built.
//   - Folder (Windows): a folder holding Name.exe and assets, with a
//     FileList; its entries are replaced while the app runs: the running
//     .exe can be renamed, not deleted, so it's put aside and removed at
//     the next launch.
//   - Prefix (Linux): the install prefix of the build's tarball, such as
//     ~/.local, which other programs share: bin/<exe>, share/<exe>/ (its
//     assets and FileList), share/applications/<id>.desktop, its icons,
//     metainfo and MIME types. Only the app's files are replaced, one by
//     one, each by a rename (a running program keeps its old file).
type Kind uint8

const (
	Bundle Kind = iota + 1
	Folder
	Prefix
)

// Install is how an app is installed, as far as the CLI needs: the
// layout, and the executable's name (a prefix's tree is named after it).
type Install struct {
	Kind Kind
	Exe  string
}

// Tree returns the root of the app in dir, an extracted archive: the
// bundle (its one entry, Name.app); a Linux tarball's one directory (it
// unpacks to <exe>/bin/<exe>...); a Windows folder's entries as they are.
func (in *Install) Tree(dir string) (string, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return "", err
	}
	switch in.Kind {
	case Bundle:
		if len(entries) != 1 || !strings.HasSuffix(entries[0].Name(), ".app") || !entries[0].IsDir() {
			return "", errors.New("the update holds no app bundle")
		}
		return filepath.Join(dir, entries[0].Name()), nil
	case Prefix:
		if len(entries) == 1 && entries[0].IsDir() {
			if _, err := os.Stat(filepath.Join(dir, entries[0].Name(), "bin")); err == nil {
				return filepath.Join(dir, entries[0].Name()), nil
			}
		}
		if _, err := os.Stat(filepath.Join(dir, "bin")); err != nil {
			return "", errors.New("the update holds no bin directory")
		}
	}
	return dir, nil
}
