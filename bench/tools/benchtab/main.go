// Benchtab tabulates runs of asuka test --bench --json: for each run set
// (a label and its files, one per run), each bench's fastest median over
// the runs, the spread of those medians, and what one iteration did
// (allocations, ops and bytes sent to the host, views measured, layout
// nodes computed), as a Markdown table; with two sets, the change.
//
//	go run ./bench/tools/benchtab before=b1.json,b2.json after=a1.json,a2.json
//
// The fastest of several runs is the least disturbed by the rest of the
// machine; the spread says how much the runs disagreed.
package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"strings"
)

type result struct {
	Bench          string  `json:"bench"`
	NsPerOp        float64 `json:"nsPerOp"`
	AllocsPerOp    float64 `json:"allocsPerOp"`
	BytesPerOp     float64 `json:"bytesPerOp"`
	HostOpsPerOp   float64 `json:"hostOpsPerOp"`
	HostBytesPerOp float64 `json:"hostBytesPerOp"`
	MeasuresPerOp  float64 `json:"measuresPerOp"`
	LayoutsPerOp   float64 `json:"layoutsPerOp"`
}

type summary struct {
	min, max float64
	last     result
}

func load(files []string) (map[string]*summary, []string) {
	m := map[string]*summary{}
	var order []string
	for _, f := range files {
		fh, err := os.Open(f)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		sc := bufio.NewScanner(fh)
		sc.Buffer(make([]byte, 1<<20), 1<<24)
		for sc.Scan() {
			var r result
			if json.Unmarshal(sc.Bytes(), &r) != nil || r.Bench == "" {
				continue
			}
			s := m[r.Bench]
			if s == nil {
				s = &summary{min: math.Inf(1)}
				m[r.Bench] = s
				order = append(order, r.Bench)
			}
			s.min = math.Min(s.min, r.NsPerOp)
			s.max = math.Max(s.max, r.NsPerOp)
			s.last = r
		}
		fh.Close()
	}
	return m, order
}

func dur(ns float64) string {
	switch {
	case ns < 1e3:
		return fmt.Sprintf("%.0f ns", ns)
	case ns < 1e6:
		return fmt.Sprintf("%.1f µs", ns/1e3)
	case ns < 1e9:
		return fmt.Sprintf("%.2f ms", ns/1e6)
	}
	return fmt.Sprintf("%.2f s", ns/1e9)
}

func size(b float64) string {
	switch {
	case b < 1024:
		return fmt.Sprintf("%.0f B", b)
	case b < 1<<20:
		return fmt.Sprintf("%.1f KB", b/1024)
	}
	return fmt.Sprintf("%.1f MB", b/(1<<20))
}

func cell(s *summary) string {
	if s == nil {
		return "–"
	}
	spread := 0.0
	if s.min > 0 {
		spread = (s.max - s.min) / s.min * 100
	}
	r := s.last
	return fmt.Sprintf("%s (±%.0f%%) · %.0f allocs, %s · %.0f ops, %s, %.0f measures, %.0f laid out", dur(s.min), spread, r.AllocsPerOp, size(r.BytesPerOp), r.HostOpsPerOp, size(r.HostBytesPerOp), r.MeasuresPerOp, r.LayoutsPerOp)
}

func main() {
	var labels []string
	var sets []map[string]*summary
	var order []string
	seen := map[string]bool{}
	for _, a := range os.Args[1:] {
		label, files, ok := strings.Cut(a, "=")
		if !ok {
			fmt.Fprintln(os.Stderr, "usage: benchtab label=run1.json,run2.json [label=...]")
			os.Exit(2)
		}
		m, o := load(strings.Split(files, ","))
		labels = append(labels, label)
		sets = append(sets, m)
		for _, b := range o {
			if !seen[b] {
				seen[b] = true
				order = append(order, b)
			}
		}
	}
	fmt.Printf("| bench | %s |", strings.Join(labels, " | "))
	if len(sets) == 2 {
		fmt.Print(" change |")
	}
	fmt.Println()
	fmt.Print("|---|")
	for range labels {
		fmt.Print("---|")
	}
	if len(sets) == 2 {
		fmt.Print("---|")
	}
	fmt.Println()
	for _, b := range order {
		fmt.Printf("| %s |", b)
		for _, s := range sets {
			fmt.Printf(" %s |", cell(s[b]))
		}
		if len(sets) == 2 && sets[0][b] != nil && sets[1][b] != nil && sets[1][b].min > 0 {
			fmt.Printf(" %.2f× |", sets[0][b].min/sets[1][b].min)
		}
		fmt.Println()
	}
}
