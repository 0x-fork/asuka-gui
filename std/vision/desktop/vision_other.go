//go:build !(linux && !android)

package vision

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go analyzer.
func newAnalyzer() (Analyzer, error) { return nil, errors.ErrUnsupported }
