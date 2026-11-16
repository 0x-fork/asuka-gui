//go:build linux && !android

package shader

// Copied in part from host/linux/shader_cpu.go (the host's own goes
// with the Go core).

import (
	"log"
	"runtime"
	"sync"

	"github.com/egoist/asuka/internal/shaderir/interp"
)

// kageShade renders the program into buf, pw by ph premultiplied BGRA
// pixels, each sx by sy of the view's pixels, across the cores. It's
// false if the program failed.
func kageShade(prog *interp.Program, words []uint32, buf []byte, pw, ph int, sx, sy, k float32) (ok bool) {
	workers := min(runtime.NumCPU(), ph)
	rows := (ph + workers - 1) / workers
	var wg sync.WaitGroup
	var failed sync.Once
	ok = true
	for y0 := 0; y0 < ph; y0 += rows {
		wg.Add(1)
		go func(y0, y1 int) {
			defer wg.Done()
			defer func() {
				if r := recover(); r != nil {
					failed.Do(func() {
						ok = false
						log.Printf("asuka: a shader failed: %v", r)
					})
				}
			}()
			in := &interp.Inputs{Uniforms: words}
			for y := y0; y < y1; y++ {
				py := (float32(y) + 0.5) * sy
				o := y * pw * 4
				for x := range pw {
					px := (float32(x) + 0.5) * sx
					c, drawn := prog.Color(in, [4]float32{px, py, 0, 1}, px/k, py/k)
					if !drawn {
						c = [4]float32{}
					}
					a := unitClamp(c[3])
					buf[o] = byte(min(unitClamp(c[2]), a)*255 + 0.5)
					buf[o+1] = byte(min(unitClamp(c[1]), a)*255 + 0.5)
					buf[o+2] = byte(min(unitClamp(c[0]), a)*255 + 0.5)
					buf[o+3] = byte(a*255 + 0.5)
					o += 4
				}
			}
		}(y0, min(y0+rows, ph))
	}
	wg.Wait()
	return ok
}

func unitClamp(v float32) float32 {
	if !(v > 0) { // NaN too
		return 0
	}
	return min(v, 1)
}
