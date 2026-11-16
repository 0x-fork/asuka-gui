package main

import (
	"fmt"
	"os"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/cachedir"
	"github.com/egoist/asuka/internal/ccache"
)

// cmdCache is asuka cache: the build cache's size and hits (stats), or
// everything in it removed (clean).
func cmdCache(args []string) error {
	if len(args) == 0 || args[0] == "-h" || args[0] == "--help" {
		fmt.Fprint(os.Stderr, `usage:
  asuka cache stats    the build cache's size, its cap and its hits and misses
  asuka cache clean    remove everything in the build cache

The cache keeps what builds compile and download, shared by every app on
this computer; what was used least lately goes once it's over its cap:
ASUKA_CACHE_MAX (10G by default; 0 for none). ASUKA_CACHE moves it.
`)
		if len(args) == 0 {
			return errReported
		}
		return errHelp
	}
	switch args[0] {
	case "stats":
		size, entries := ccache.Size()
		max := ccache.Max()
		fmt.Printf("cache:   %s\n", cachedir.Root())
		cap := "none (ASUKA_CACHE_MAX=0)"
		if max > 0 {
			cap = formatSize(max)
		}
		fmt.Printf("size:    %s in %d entries (cap %s)\n", formatSize(size), entries, cap)
		c := ccache.ReadCounts()
		kinds := map[string]bool{}
		for k := range c.Hits {
			kinds[k] = true
		}
		for k := range c.Misses {
			kinds[k] = true
		}
		names := map[string]string{"c": "C objects", "swift": "Swift modules", "prebuilt": "prebuilt packages"}
		var ks []string
		for k := range kinds {
			ks = append(ks, k)
		}
		sort.Strings(ks)
		if len(ks) == 0 {
			fmt.Println("hits:    none yet")
		}
		for _, k := range ks {
			h, m := c.Hits[k], c.Misses[k]
			rate := 0.0
			if h+m > 0 {
				rate = 100 * float64(h) / float64(h+m)
			}
			name := names[k]
			if name == "" {
				name = k
			}
			fmt.Printf("%-18s %d hits, %d misses (%.0f%%)\n", name+":", h, m, rate)
		}
		fmt.Printf("since:   %s\n", c.Since.Local().Format("2006-01-02 15:04"))
		return nil
	case "clean":
		freed, err := ccache.Clean()
		ccache.ResetCounts()
		fmt.Printf("asuka: removed %s from %s\n", formatSize(freed), cachedir.Root())
		return err
	}
	return fmt.Errorf("asuka cache: unknown command %q (stats, clean)", strings.Join(args, " "))
}

// formatSize is a size for people: 1.2 GB.
func formatSize(n int64) string {
	const unit = 1024
	if n < unit {
		return fmt.Sprintf("%d B", n)
	}
	div, exp := int64(unit), 0
	for m := n / unit; m >= unit; m /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(n)/float64(div), "KMGTPE"[exp])
}
