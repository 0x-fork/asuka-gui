package check

import (
	"go/ast"
	"go/parser"
	"go/token"
	"io/fs"
	"path/filepath"
	"strings"
	"testing"
)

// No code outside tests makes a Diagnostic without its code: every one
// is made by NewDiagnostic, FromSyntax or the checker's errorf (whose
// code parameter the compiler asks for), or says Code: in its literal.
func TestNoUncodedDiagnostics(t *testing.T) {
	root := filepath.Join("..", "..")
	n := 0
	for _, dir := range []string{"lang", "internal", "cmd"} {
		filepath.WalkDir(filepath.Join(root, dir), func(p string, d fs.DirEntry, err error) error {
			if err != nil || d.IsDir() || !strings.HasSuffix(p, ".go") || strings.HasSuffix(p, "_test.go") {
				return nil
			}
			fset := token.NewFileSet()
			f, err := parser.ParseFile(fset, p, nil, 0)
			if err != nil {
				return nil
			}
			ast.Inspect(f, func(x ast.Node) bool {
				lit, ok := x.(*ast.CompositeLit)
				if !ok || !isDiagnostic(lit.Type) || len(lit.Elts) == 0 {
					return true
				}
				n++
				for _, e := range lit.Elts {
					if kv, ok := e.(*ast.KeyValueExpr); ok {
						if id, ok := kv.Key.(*ast.Ident); ok && id.Name == "Code" {
							return true
						}
					}
				}
				t.Errorf("%s: a Diagnostic without its code: make it with NewDiagnostic, or say Code:", fset.Position(lit.Pos()))
				return true
			})
			return nil
		})
	}
	if n == 0 {
		t.Fatal("no Diagnostic literals found: the walk looked nowhere")
	}
}

func isDiagnostic(t ast.Expr) bool {
	switch t := t.(type) {
	case *ast.Ident:
		return t.Name == "Diagnostic"
	case *ast.SelectorExpr:
		x, ok := t.X.(*ast.Ident)
		return ok && x.Name == "check" && t.Sel.Name == "Diagnostic"
	}
	return false
}
