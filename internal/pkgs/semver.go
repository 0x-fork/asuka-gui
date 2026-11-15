// Package pkgs is Asuka's package manager: packages on the npm registry,
// with the asuka CLI as the client (RFC 0007, "Packages"). It resolves an
// app's dependencies to one version of each package by minimal version
// selection, records them in asuka.lock, and keeps their sources in a
// content-addressed cache shared by every project. It never runs a
// package's scripts and needs no Node.
package pkgs

import (
	"fmt"
	"strconv"
	"strings"
)

// Version is a semantic version.
type Version struct {
	Major, Minor, Patch int
	Pre                 string // the pre-release, after '-'; "" for a release
	Build               string // after '+', ignored in comparisons
}

// ParseVersion reads a semver ("1.2.3", "v1.2.3-beta.1+sha").
func ParseVersion(s string) (Version, error) {
	s = strings.TrimPrefix(strings.TrimSpace(s), "v")
	var v Version
	if i := strings.IndexByte(s, '+'); i >= 0 {
		v.Build = s[i+1:]
		s = s[:i]
	}
	if i := strings.IndexByte(s, '-'); i >= 0 {
		v.Pre = s[i+1:]
		s = s[:i]
	}
	parts := strings.Split(s, ".")
	if len(parts) != 3 {
		return Version{}, fmt.Errorf("pkgs: %q isn't a version (major.minor.patch)", s)
	}
	nums := [3]*int{&v.Major, &v.Minor, &v.Patch}
	for i, p := range parts {
		n, err := strconv.Atoi(p)
		if err != nil || n < 0 || (len(p) > 1 && p[0] == '0') {
			return Version{}, fmt.Errorf("pkgs: %q isn't a version", s)
		}
		*nums[i] = n
	}
	return v, nil
}

func (v Version) String() string {
	s := fmt.Sprintf("%d.%d.%d", v.Major, v.Minor, v.Patch)
	if v.Pre != "" {
		s += "-" + v.Pre
	}
	if v.Build != "" {
		s += "+" + v.Build
	}
	return s
}

// Compare orders versions by semver's precedence.
func (v Version) Compare(o Version) int {
	for _, d := range [][2]int{{v.Major, o.Major}, {v.Minor, o.Minor}, {v.Patch, o.Patch}} {
		if d[0] != d[1] {
			if d[0] < d[1] {
				return -1
			}
			return 1
		}
	}
	switch {
	case v.Pre == o.Pre:
		return 0
	case v.Pre == "":
		return 1
	case o.Pre == "":
		return -1
	}
	a, b := strings.Split(v.Pre, "."), strings.Split(o.Pre, ".")
	for i := 0; i < len(a) && i < len(b); i++ {
		ai, aerr := strconv.Atoi(a[i])
		bi, berr := strconv.Atoi(b[i])
		switch {
		case aerr == nil && berr == nil:
			if ai != bi {
				if ai < bi {
					return -1
				}
				return 1
			}
		case aerr == nil:
			return -1
		case berr == nil:
			return 1
		default:
			if c := strings.Compare(a[i], b[i]); c != 0 {
				return c
			}
		}
	}
	switch {
	case len(a) < len(b):
		return -1
	case len(a) > len(b):
		return 1
	}
	return 0
}

// Range is a set of versions, as npm writes them: "1.2.3", "^1.2.0",
// "~1.2.0", ">=1.0.0 <2.0.0", "1.x", "*", ranges joined by "||".
type Range struct {
	sets [][]cmp // alternatives (||), each a conjunction
	src  string
}

type cmp struct {
	op string // "=", ">", ">=", "<", "<="
	v  Version
}

func (r Range) String() string { return r.src }

// ParseRange reads a range.
func ParseRange(s string) (Range, error) {
	r := Range{src: strings.TrimSpace(s)}
	if r.src == "" || r.src == "*" || r.src == "latest" || r.src == "x" {
		r.sets = [][]cmp{{{">=", Version{}}}}
		return r, nil
	}
	for _, alt := range strings.Split(r.src, "||") {
		var set []cmp
		fields := strings.Fields(alt)
		for i := 0; i < len(fields); i++ {
			f := fields[i]
			if i+2 < len(fields) && fields[i+1] == "-" {
				// a - b
				lo, err := partial(f)
				if err != nil {
					return Range{}, err
				}
				hi, err := partial(fields[i+2])
				if err != nil {
					return Range{}, err
				}
				set = append(set, cmp{">=", lo.floor()}, hi.ceilCmp())
				i += 2
				continue
			}
			cs, err := comparator(f)
			if err != nil {
				return Range{}, err
			}
			set = append(set, cs...)
		}
		if len(set) == 0 {
			set = []cmp{{">=", Version{}}}
		}
		r.sets = append(r.sets, set)
	}
	return r, nil
}

// pv is a partial version: 1, 1.2, 1.2.3, with x wildcards.
type pv struct {
	v   Version
	n   int // how many parts are given
	pre string
}

func partial(s string) (pv, error) {
	s = strings.TrimPrefix(s, "v")
	var p pv
	if i := strings.IndexByte(s, '-'); i >= 0 {
		p.pre = s[i+1:]
		s = s[:i]
	}
	if i := strings.IndexByte(s, '+'); i >= 0 {
		s = s[:i]
	}
	parts := strings.Split(s, ".")
	if len(parts) > 3 {
		return pv{}, fmt.Errorf("pkgs: %q isn't a version", s)
	}
	nums := [3]*int{&p.v.Major, &p.v.Minor, &p.v.Patch}
	for i, x := range parts {
		if x == "x" || x == "X" || x == "*" {
			break
		}
		n, err := strconv.Atoi(x)
		if err != nil || n < 0 {
			return pv{}, fmt.Errorf("pkgs: %q isn't a version", s)
		}
		*nums[i] = n
		p.n = i + 1
	}
	p.v.Pre = p.pre
	return p, nil
}

func (p pv) floor() Version { return p.v }

// next is the least version past every version p matches.
func (p pv) next() Version {
	switch p.n {
	case 0:
		return Version{Major: 1 << 30}
	case 1:
		return Version{Major: p.v.Major + 1, Pre: "0"}
	case 2:
		return Version{Major: p.v.Major, Minor: p.v.Minor + 1, Pre: "0"}
	}
	return Version{}
}

func (p pv) ceilCmp() cmp {
	if p.n == 3 {
		return cmp{"<=", p.v}
	}
	return cmp{"<", p.next()}
}

func comparator(s string) ([]cmp, error) {
	switch {
	case strings.HasPrefix(s, "^"):
		p, err := partial(s[1:])
		if err != nil {
			return nil, err
		}
		var hi Version
		switch {
		case p.v.Major > 0 || p.n == 1:
			hi = Version{Major: p.v.Major + 1, Pre: "0"}
		case p.v.Minor > 0 || p.n == 2:
			hi = Version{Minor: p.v.Minor + 1, Pre: "0"}
		default:
			hi = Version{Patch: p.v.Patch + 1, Pre: "0"}
		}
		return []cmp{{">=", p.floor()}, {"<", hi}}, nil
	case strings.HasPrefix(s, "~"):
		p, err := partial(strings.TrimPrefix(s[1:], ">"))
		if err != nil {
			return nil, err
		}
		hi := Version{Major: p.v.Major, Minor: p.v.Minor + 1, Pre: "0"}
		if p.n == 1 {
			hi = Version{Major: p.v.Major + 1, Pre: "0"}
		}
		return []cmp{{">=", p.floor()}, {"<", hi}}, nil
	}
	for _, op := range []string{">=", "<=", ">", "<", "="} {
		if strings.HasPrefix(s, op) {
			p, err := partial(s[len(op):])
			if err != nil {
				return nil, err
			}
			switch op {
			case ">":
				if p.n < 3 {
					return []cmp{{">=", p.next()}}, nil
				}
			case "<=":
				if p.n < 3 {
					return []cmp{{"<", p.next()}}, nil
				}
			case "=":
				return exact(p), nil
			}
			return []cmp{{op, p.v}}, nil
		}
	}
	p, err := partial(s)
	if err != nil {
		return nil, err
	}
	return exact(p), nil
}

func exact(p pv) []cmp {
	if p.n == 3 {
		return []cmp{{"=", p.v}}
	}
	return []cmp{{">=", p.floor()}, {"<", p.next()}}
}

// Match reports whether v is in r. A pre-release matches only a
// comparator of the same major.minor.patch that names a pre-release, as
// npm does.
func (r Range) Match(v Version) bool {
	for _, set := range r.sets {
		ok := true
		preAllowed := v.Pre == ""
		for _, c := range set {
			d := v.Compare(c.v)
			switch c.op {
			case "=":
				ok = ok && d == 0
			case ">":
				ok = ok && d > 0
			case ">=":
				ok = ok && d >= 0
			case "<":
				ok = ok && d < 0
			case "<=":
				ok = ok && d <= 0
			}
			if v.Pre != "" && c.v.Pre != "" && c.v.Pre != "0" && c.v.Major == v.Major && c.v.Minor == v.Minor && c.v.Patch == v.Patch {
				preAllowed = true
			}
		}
		if ok && preAllowed {
			return true
		}
	}
	return false
}

// Min is the least version of versions in r, false if none is.
func (r Range) Min(versions []Version) (Version, bool) {
	var best Version
	found := false
	for _, v := range versions {
		if r.Match(v) && (!found || v.Compare(best) < 0) {
			best, found = v, true
		}
	}
	return best, found
}

// Max is the greatest version of versions in r.
func (r Range) Max(versions []Version) (Version, bool) {
	var best Version
	found := false
	for _, v := range versions {
		if r.Match(v) && (!found || v.Compare(best) > 0) {
			best, found = v, true
		}
	}
	return best, found
}
