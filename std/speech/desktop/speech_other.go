//go:build !(linux && !android)

package speech

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go synthesizer.
func newSynthesizer() (Synthesizer, error) { return nil, errors.ErrUnsupported }
