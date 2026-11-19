package main

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sort"
	"strings"
)

func fib(n int) int {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func sieve(n int) int {
	composite := make([]bool, n+1)
	count := 0
	for i := 2; i <= n; i++ {
		if !composite[i] {
			count++
			for j := i * i; j <= n; j += i {
				composite[j] = true
			}
		}
	}
	return count
}

type Body struct{ x, y, z, vx, vy, vz, mass float64 }

func energy(b []Body) float64 {
	e := 0.0
	for i := range b {
		e += 0.5 * b[i].mass * (b[i].vx*b[i].vx + b[i].vy*b[i].vy + b[i].vz*b[i].vz)
		for j := i + 1; j < len(b); j++ {
			dx, dy, dz := b[i].x-b[j].x, b[i].y-b[j].y, b[i].z-b[j].z
			e -= b[i].mass * b[j].mass / math.Sqrt(dx*dx+dy*dy+dz*dz)
		}
	}
	return e
}

func nbody(steps int) string {
	const pi = 3.141592653589793
	solar := 4 * pi * pi
	days := 365.24
	b := []Body{
		{0, 0, 0, 0, 0, 0, solar},
		{4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01, 1.66007664274403694e-03 * days, 7.69901118419740425e-03 * days, -6.90460016972063023e-05 * days, 9.54791938424326609e-04 * solar},
		{8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01, -2.76742510726862411e-03 * days, 4.99852801234917238e-03 * days, 2.30417297573763929e-05 * days, 2.85885980666130812e-04 * solar},
		{1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01, 2.96460137564761618e-03 * days, 2.37847173959480950e-03 * days, -2.96589568540237556e-05 * days, 4.36624404335156298e-05 * solar},
		{1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01, 2.68067772490389322e-03 * days, 1.62824170038242295e-03 * days, -9.51592254519715870e-05 * days, 5.15138902046611451e-05 * solar},
	}
	var px, py, pz float64
	for _, x := range b {
		px += x.vx * x.mass
		py += x.vy * x.mass
		pz += x.vz * x.mass
	}
	b[0].vx, b[0].vy, b[0].vz = -px/solar, -py/solar, -pz/solar
	before := energy(b)
	n := len(b)
	dt := 0.01
	for s := 0; s < steps; s++ {
		for i := 0; i < n; i++ {
			for j := i + 1; j < n; j++ {
				dx, dy, dz := b[i].x-b[j].x, b[i].y-b[j].y, b[i].z-b[j].z
				d2 := dx*dx + dy*dy + dz*dz
				mag := dt / (d2 * math.Sqrt(d2))
				mi, mj := b[i].mass*mag, b[j].mass*mag
				b[i].vx -= dx * mj
				b[i].vy -= dy * mj
				b[i].vz -= dz * mj
				b[j].vx += dx * mi
				b[j].vy += dy * mi
				b[j].vz += dz * mi
			}
		}
		for i := 0; i < n; i++ {
			b[i].x += dt * b[i].vx
			b[i].y += dt * b[i].vy
			b[i].z += dt * b[i].vz
		}
	}
	return fmt.Sprintf("%.9f %.9f", before, energy(b))
}

type Tree struct{ left, right *Tree }

func make_(d int) *Tree {
	if d == 0 {
		return &Tree{}
	}
	return &Tree{make_(d - 1), make_(d - 1)}
}

func check(t *Tree) int {
	n := 1
	if t.left != nil {
		n += check(t.left)
	}
	if t.right != nil {
		n += check(t.right)
	}
	return n
}

func trees(max int) int {
	total := check(make_(max + 1))
	long := make_(max)
	for d := 4; d <= max; d += 2 {
		it := 1 << (max - d + 4)
		for i := 0; i < it; i++ {
			total += check(make_(d))
		}
	}
	return total + check(long)
}

func words(n int) string {
	syl := []string{"ka", "to", "ri", "su", "me", "no", "ha", "yu", "chi", "ra"}
	seed := 42
	parts := make([]string, 0)
	for i := 0; i < n; i++ {
		seed = (seed*1103515245 + 12345) % 2147483648
		a := syl[seed%10]
		seed = (seed*1103515245 + 12345) % 2147483648
		parts = append(parts, a+syl[seed%10])
	}
	text := strings.Join(parts, " ")
	counts := map[string]int{}
	for _, w := range strings.Split(text, " ") {
		counts[w]++
	}
	keys := make([]string, 0, len(counts))
	for k := range counts {
		keys = append(keys, k)
	}
	sort.SliceStable(keys, func(i, j int) bool {
		a, b := keys[i], keys[j]
		return counts[a] > counts[b] || (counts[a] == counts[b] && a < b)
	})
	return fmt.Sprintf("%d %s %d", len(counts), keys[0], counts[keys[0]])
}

type Item struct {
	ID     int      `json:"id"`
	Name   string   `json:"name"`
	Price  float64  `json:"price"`
	Tags   []string `json:"tags"`
	Active bool     `json:"active"`
}

func jsonRound(n int) int {
	items := []Item{}
	for i := 0; i < n; i++ {
		items = append(items, Item{i, fmt.Sprintf("item %d", i), float64(i) * 1.5, []string{"a", fmt.Sprintf("b%d", i%7)}, i%2 == 0})
	}
	total := 0
	for r := 0; r < 5; r++ {
		text, _ := json.Marshal(items)
		var back []Item
		if err := json.Unmarshal(text, &back); err != nil {
			panic(err)
		}
		total += len(back) + len(text)%7
	}
	return total
}

func sortInts(n int) int {
	seed := 7
	xs := []int{}
	for i := 0; i < n; i++ {
		seed = (seed*1103515245 + 12345) % 2147483648
		xs = append(xs, seed)
	}
	sort.Ints(xs)
	return xs[0] + xs[n/2] + xs[n-1]
}

func main() {
	name := "fib"
	if len(os.Args) > 1 {
		name = os.Args[1]
	}
	switch name {
	case "fib":
		fmt.Println(fib(38))
	case "sieve":
		fmt.Println(sieve(50000000))
	case "nbody":
		fmt.Println(nbody(5000000))
	case "trees":
		fmt.Println(trees(18))
	case "words":
		fmt.Println(words(2000000))
	case "json":
		fmt.Println(jsonRound(200000))
	case "sort":
		fmt.Println(sortInts(5000000))
	}
}
