// Command stdbench-go is ../asuka/main.asuka's work in Go, for
// comparison: the same inputs, the same results. `go run ./bench/std/go
// <name> [dir]` prints the result and the nanoseconds the work took,
// on stderr.
package main

import (
	"archive/tar"
	"archive/zip"
	"bufio"
	"bytes"
	"compress/flate"
	"compress/gzip"
	"container/heap"
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/md5"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/base64"
	"encoding/csv"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"hash/crc32"
	"html"
	"io"
	"math"
	"math/big"
	"net/http"
	"net/url"
	"os"
	"regexp"
	"slices"
	"sort"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"

	"text/template"

	"github.com/BurntSushi/toml"
	"github.com/Masterminds/semver/v3"
	"github.com/google/btree"
	"golang.org/x/crypto/argon2"
	"golang.org/x/crypto/bcrypt"
	"golang.org/x/crypto/chacha20poly1305"
)

var syllables = []string{"ka", "to", "ri", "su", "me", "no", "ha", "yu", "chi", "ra"}

func next(seed int) int { return (seed*1103515245 + 12345) % 2147483648 }

func corpus(n int) string {
	seed := 42
	var b strings.Builder
	for i := 0; i < n; i++ {
		seed = next(seed)
		b.WriteString(syllables[seed%10])
		b.WriteString(syllables[(seed/10)%10])
		if seed%13 == 0 {
			b.WriteString(strconv.Itoa(seed % 1000))
		}
		if i%12 == 11 {
			b.WriteByte('\n')
		} else {
			b.WriteByte(' ')
		}
	}
	return b.String()
}

// jsFloat prints x as Asuka (and JavaScript) do: the shortest digits,
// plain from 1e-6 to 1e21.
func jsFloat(x float64) string {
	if math.IsNaN(x) {
		return "NaN"
	}
	if math.IsInf(x, 0) {
		if x > 0 {
			return "Infinity"
		}
		return "-Infinity"
	}
	a := math.Abs(x)
	if a != 0 && (a < 1e-6 || a >= 1e21) {
		s := strconv.FormatFloat(x, 'e', -1, 64)
		mant, exp, _ := strings.Cut(s, "e")
		e, _ := strconv.Atoi(exp)
		if e < 0 {
			return mant + "e-" + strconv.Itoa(-e)
		}
		return mant + "e+" + strconv.Itoa(e)
	}
	return strconv.FormatFloat(x, 'f', -1, 64)
}

func interp(n int) string {
	parts := make([]string, 0)
	for i := 0; i < n; i++ {
		parts = append(parts, fmt.Sprintf("item %d: %d ok", i, i*3))
	}
	return strconv.Itoa(len(strings.Join(parts, ",")))
}

func concat(n int) string {
	total := 0
	for i := 0; i < n; i++ {
		k := syllables[i%10] + "-" + syllables[(i/10)%10] + "/" + syllables[(i/100)%10]
		total += len(k)
	}
	return strconv.Itoa(total)
}

func lines(s string) []string {
	var out []string
	for len(s) > 0 {
		i := strings.IndexByte(s, '\n')
		if i < 0 {
			out = append(out, strings.TrimSuffix(s, "\r"))
			break
		}
		out = append(out, strings.TrimSuffix(s[:i], "\r"))
		s = s[i+1:]
	}
	return out
}

func splitting(text string) string {
	total := 0
	for range 3 {
		total += len(strings.Split(text, " ")) + len(lines(text)) + len(strings.Fields(text))
	}
	return strconv.Itoa(total)
}

func finding(text string) string {
	total := 0
	for range 5 {
		total += len(strings.ReplaceAll(text, "kato", "X"))
		total += len(strings.Split(text, "chira"))
		if strings.Contains(text, "suyuq") {
			total++
		}
		if strings.Contains(text, "ra\nka") {
			total++
		}
		total += strings.Index(text, "meme1") + strings.LastIndex(text, "kaka")
	}
	return strconv.Itoa(total)
}

func casing(text, uni string) string {
	total := 0
	for range 3 {
		total += len(strings.ToLower(strings.ToUpper(text)))
		total += len(strings.ToUpper(uni)) + len(strings.ToLower(uni)) + len(foldCase(uni))
		if strings.Contains(strings.ToLower(text), "kato") {
			total++
		}
		if strings.HasSuffix(foldCase(uni), foldCase("STRASSE")) {
			total++
		}
	}
	return strconv.Itoa(total)
}

// foldCase is Unicode's full folding for what the benchmark's text has
// (Go has only simple folding): ß to ss, final ς to σ.
func foldCase(s string) string {
	return strings.NewReplacer("ß", "ss", "ς", "σ").Replace(strings.ToLower(s))
}

func counting(text string) string {
	total := 0
	for range 20 {
		total += utf8.RuneCountInString(text)
	}
	runes := []rune(text)
	step := len(runes) / 2000
	for i := 0; i < 2000; i++ {
		total += len(string(runes[i*step : i*step+8]))
	}
	return strconv.Itoa(total)
}

func numbers(n int) string {
	ints := make([]string, 0)
	for i := 0; i < n; i++ {
		ints = append(ints, strconv.Itoa((i*7919)%1000003-500000))
	}
	total := 0
	for _, s := range ints {
		v, _ := strconv.Atoi(s)
		total += v
	}
	floats := make([]string, 0)
	for i := 0; i < n; i++ {
		floats = append(floats, jsFloat(float64(i)/7.0+0.1))
	}
	sum := 0.0
	for _, s := range floats {
		v, _ := strconv.ParseFloat(s, 64)
		sum += v
	}
	total += len(strings.Join(floats, ""))
	return fmt.Sprintf("%d %.3f", total, sum)
}

func grouped(i int) string {
	s := strconv.Itoa(i)
	var b strings.Builder
	for k := range len(s) {
		if k > 0 && (len(s)-k)%3 == 0 {
			b.WriteByte(',')
		}
		b.WriteByte(s[k])
	}
	return b.String()
}

func formats(n int) string {
	total := 0
	for i := 0; i < n; i++ {
		x := float64(i) * 1.25
		total += len(fmt.Sprintf("%12.3f|%08d|%s|%e", x, i, grouped(i), x))
	}
	return strconv.Itoa(total)
}

func mapInt(n int) string {
	m := map[int]int{}
	for i := 0; i < n; i++ {
		m[(i*7919)%1000003]++
	}
	hits := 0
	for i := 0; i < n; i++ {
		hits += m[i]
	}
	return fmt.Sprintf("%d %d", len(m), hits)
}

func mapStr(keys []string) string {
	m := map[string]int{}
	for _, k := range keys {
		m[k]++
	}
	hits := 0
	for _, k := range keys {
		hits += m[k]
	}
	return fmt.Sprintf("%d %d", len(m), hits)
}

func sets(n int) string {
	s := map[int]struct{}{}
	for i := 0; i < n; i++ {
		s[(i*7919)%1000003] = struct{}{}
	}
	hits := 0
	for i := 0; i < n; i++ {
		if _, ok := s[i]; ok {
			hits++
		}
	}
	return fmt.Sprintf("%d %d", len(s), hits)
}

func ints(n int) []int {
	seed := 7
	xs := make([]int, 0)
	for range n {
		seed = next(seed)
		xs = append(xs, seed)
	}
	return xs
}

type score struct{ id, score int }

func regexes(text string) string {
	digits := regexp.MustCompile(`\d+`)
	word := regexp.MustCompile(`\b\w+chi\w*\b`)
	lit := regexp.MustCompile("chira")
	mail := regexp.MustCompile(`(\w+)@(\w+)\.com`)
	alt := regexp.MustCompile("(?i)kato|meme|yuyu")
	total := len(digits.FindAllString(text, -1))
	total += len(word.FindAllString(text, -1))
	total += len(lit.FindAllString(text, -1))
	total += len(mail.FindAllString(text, -1))
	total += len(alt.FindAllString(text, -1))
	for _, l := range lines(text) {
		if digits.MatchString(l) {
			total++
		}
	}
	total += len(digits.ReplaceAllString(text, "#"))
	return strconv.Itoa(total)
}

func urls(n int) string {
	total := 0
	for i := 0; i < n; i++ {
		w := syllables[i%10] + syllables[(i/10)%10]
		s := fmt.Sprintf("https://user%d@host%d.example.com:%d/a/%s/%d?q=%s+x%d&page=%d#f%d", i%7, i%50, 8000+i%1000, w, i, w, i, i%10, i)
		if u, err := url.Parse(s); err == nil {
			port, _ := strconv.Atoi(u.Port())
			total += len(u.Hostname()) + len(u.Path) + len(u.Query().Get("q")) + port
		}
		total += len(url.QueryEscape(w + " & " + strconv.Itoa(i) + "/é"))
	}
	return strconv.Itoa(total)
}

func base64s(data []byte) string {
	total := 0
	for range 4 {
		s := base64.StdEncoding.EncodeToString(data)
		b, _ := base64.StdEncoding.DecodeString(s)
		total += len(s) + len(b)
	}
	return strconv.Itoa(total)
}

func hexes(data []byte) string {
	total := 0
	for range 2 {
		s := hex.EncodeToString(data)
		b, _ := hex.DecodeString(s)
		total += len(s) + len(b)
	}
	return strconv.Itoa(total)
}

func htmls(text string) string {
	total := 0
	for range 3 {
		e := html.EscapeString(text)
		total += len(e) + len(html.UnescapeString(e))
	}
	return strconv.Itoa(total)
}

type afile struct {
	name string
	data []byte
}

func archiveFiles(text string) []afile {
	b := []byte(text)
	step := len(b) / 100
	var files []afile
	for i := 0; i < 100; i++ {
		files = append(files, afile{fmt.Sprintf("dir/file%d.txt", i), b[i*step : (i+1)*step]})
	}
	return files
}

func zips(files []afile) string {
	var buf bytes.Buffer
	w := zip.NewWriter(&buf)
	w.RegisterCompressor(zip.Deflate, func(out io.Writer) (io.WriteCloser, error) { return flate.NewWriter(out, 6) })
	for _, f := range files {
		fw, _ := w.CreateHeader(&zip.FileHeader{Name: f.name, Method: zip.Deflate, Modified: time.Now()})
		fw.Write(f.data)
	}
	w.Close()
	r, _ := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	total := 0
	for _, f := range r.File {
		rc, _ := f.Open()
		b, _ := io.ReadAll(rc)
		rc.Close()
		total += len(b) + len(f.Name)
	}
	return fmt.Sprintf("%d %d", len(r.File), total)
}

func tars(files []afile) string {
	var buf bytes.Buffer
	w := tar.NewWriter(&buf)
	for _, f := range files {
		w.WriteHeader(&tar.Header{Name: f.name, Mode: 0o644, Size: int64(len(f.data)), ModTime: time.Now(), Typeflag: tar.TypeReg})
		w.Write(f.data)
	}
	w.Close()
	r := tar.NewReader(bytes.NewReader(buf.Bytes()))
	n, total := 0, 0
	for {
		h, err := r.Next()
		if err != nil {
			break
		}
		b, _ := io.ReadAll(r)
		n++
		total += len(b) + len(h.Name)
	}
	return fmt.Sprintf("%d %d", n, total)
}

func hashes(data []byte, which int) string {
	var out []byte
	crc := uint32(0)
	for range 4 {
		switch which {
		case 0:
			s := sha1.Sum(data)
			out = s[:]
		case 1:
			s := md5.Sum(data)
			out = s[:]
		default:
			crc = crc32.Update(crc, crc32.IEEETable, data)
		}
	}
	if which == 2 {
		return strconv.FormatUint(uint64(crc), 10)
	}
	return hex.EncodeToString(out)
}

func buffered(n int) string {
	w := bufio.NewWriter(os.Stdout)
	for i := 0; i < n; i++ {
		w.WriteString("line " + strconv.Itoa(i) + " of the output\n")
	}
	w.Flush()
	return strconv.Itoa(n)
}

func csvText(n int) string {
	rows := []string{"id,name,price,tags,note"}
	for i := 0; i < n; i++ {
		rows = append(rows, fmt.Sprintf("%d,item %d,%s,\"a,b%d\",plain text here", i, i, jsFloat(float64(i)*0.25), i%7))
	}
	return strings.Join(rows, "\n") + "\n"
}

func tomlText(n int) string {
	var b strings.Builder
	b.WriteString("title = \"bench\"\n")
	for i := 0; i < n; i++ {
		fmt.Fprintf(&b, "[server%d]\nhost = \"10.0.%d.%d\"\nport = %d\nenabled = %t\nweights = [1.5, 2.5, %d]\n\n", i, i%256, i%100, 8000+i, i%2 == 0, i)
	}
	return b.String()
}

func aead(data []byte, a cipher.AEAD) string {
	total := 0
	k := sha256.Sum256([]byte("nonce"))
	nonce := k[:a.NonceSize()]
	for range 2 {
		sealed := a.Seal(nil, nonce, data, nil)
		back, err := a.Open(nil, nonce, sealed, nil)
		if err != nil {
			panic(err)
		}
		total += len(back) + int(sealed[len(sealed)-1])
	}
	return strconv.Itoa(total)
}

func gz(data []byte) string {
	var buf bytes.Buffer
	w, _ := gzip.NewWriterLevel(&buf, 6)
	w.Write(data)
	w.Close()
	packed := buf.Bytes()
	total := len(packed)
	for range 3 {
		r, _ := gzip.NewReader(bytes.NewReader(packed))
		out, _ := io.ReadAll(r)
		total += len(out)
	}
	return strconv.Itoa(total)
}

func bigints(n int) string {
	f := big.NewInt(1)
	for i := 1; i <= n; i++ {
		f.Mul(f, big.NewInt(int64(i)))
	}
	s := f.String()
	p := new(big.Int).Exp(big.NewInt(3), big.NewInt(200000), nil)
	q := new(big.Int).Quo(p, new(big.Int).Quo(f, big.NewInt(7)))
	return fmt.Sprintf("%d %d %d", len(s), len(p.String()), q.BitLen())
}

func modpows(n int) string {
	m := new(big.Int).Lsh(big.NewInt(1), 2048)
	m.Sub(m, big.NewInt(1942289))
	b := big.NewInt(65537)
	e := new(big.Int).Sub(m, big.NewInt(2))
	for range n {
		b = new(big.Int).Exp(b, e, m)
		b.Add(b, big.NewInt(1))
	}
	return strconv.Itoa(b.BitLen())
}

func times(n int) string {
	total := 0
	t := time.Unix(1700000000, 0).UTC()
	for i := 0; i < n; i++ {
		s := t.Add(time.Duration(i*7919) * time.Second).Format(time.RFC3339)
		p, _ := time.Parse(time.RFC3339, s)
		total += int(p.Unix()%1000) + len(s)
	}
	return strconv.Itoa(total)
}

type intHeap []int

func (h intHeap) Len() int           { return len(h) }
func (h intHeap) Less(i, j int) bool { return h[i] < h[j] }
func (h intHeap) Swap(i, j int)      { h[i], h[j] = h[j], h[i] }
func (h *intHeap) Push(x any)        { *h = append(*h, x.(int)) }
func (h *intHeap) Pop() any {
	old := *h
	x := old[len(old)-1]
	*h = old[:len(old)-1]
	return x
}

func heaps(xs []int) string {
	h := &intHeap{}
	for _, x := range xs {
		heap.Push(h, x)
	}
	total := 0
	for h.Len() > 0 {
		total = (total + heap.Pop(h).(int)) % 1000000007
	}
	return strconv.Itoa(total)
}

func tomlParse(text string) string {
	var v map[string]any
	if _, err := toml.Decode(text, &v); err != nil {
		panic(err)
	}
	b, _ := json.Marshal(v)
	return strconv.Itoa(len(b))
}

type item struct {
	Name  string
	Price float64
	Tags  []string
}

func templates(n int) string {
	t := template.Must(template.New("t").Funcs(template.FuncMap{
		"upper": strings.ToUpper,
		"fixed": func(x float64) string { return strconv.FormatFloat(x, 'f', 2, 64) },
		"join":  strings.Join,
	}).Parse("{{range .items}}- {{upper .Name}}: {{fixed .Price}}{{if .Tags}} ({{join .Tags \", \"}}){{end}}\n{{end}}"))
	items := []item{}
	for i := 0; i < 200; i++ {
		items = append(items, item{"item " + strconv.Itoa(i), float64(i) * 1.5, []string{"a", "b" + strconv.Itoa(i%7)}})
	}
	total := 0
	for range n {
		var b strings.Builder
		if err := t.Execute(&b, map[string]any{"items": items}); err != nil {
			panic(err)
		}
		total += b.Len()
	}
	return strconv.Itoa(total)
}

func semvers(n int) string {
	vs := []*semver.Version{}
	seed := 3
	for range n {
		seed = next(seed)
		pre := ""
		if seed%5 == 0 {
			pre = "-beta." + strconv.Itoa(seed%4)
		}
		v, err := semver.StrictNewVersion(fmt.Sprintf("%d.%d.%d%s", seed%20, (seed/20)%30, (seed/600)%40, pre))
		if err != nil {
			panic(err)
		}
		vs = append(vs, v)
	}
	sorted := slices.Clone(vs)
	sort.SliceStable(sorted, func(i, j int) bool { return sorted[i].Compare(sorted[j]) < 0 })
	r, err := semver.NewConstraint(">=3.1.0 <12.0.0 || ^15.2")
	if err != nil {
		panic(err)
	}
	ok := 0
	for _, v := range vs {
		if r.Check(v) {
			ok++
		}
	}
	return fmt.Sprintf("%s %s %d", sorted[0], sorted[len(sorted)-1], ok)
}

func sortedMaps(xs []int) string {
	type kv struct{ k, v int }
	m := btree.NewG(32, func(a, b kv) bool { return a.k < b.k })
	for _, x := range xs {
		m.ReplaceOrInsert(kv{x, x % 1000})
	}
	total := 0
	for _, x := range xs {
		if e, ok := m.Get(kv{k: x}); ok {
			total += e.v
		}
	}
	first, _ := m.Min()
	return fmt.Sprintf("%d %d %d", m.Len(), total, first.k)
}

// sortedOps is what else a sorted map does: the nearest keys, a walk of
// it all and of ranges, and removals.
func sortedOps(xs []int) string {
	type kv struct{ k, v int }
	const n = 200000
	m := btree.NewG(32, func(a, b kv) bool { return a.k < b.k })
	for _, x := range xs[:n] {
		m.ReplaceOrInsert(kv{x, x % 1000})
	}
	total := 0
	for _, x := range xs[:n] {
		m.DescendLessOrEqual(kv{k: x + 1}, func(e kv) bool { total += e.v; return false })
		m.AscendGreaterOrEqual(kv{k: x - 1}, func(e kv) bool { total += e.v; return false })
	}
	m.Ascend(func(e kv) bool { total += e.v; return true })
	for _, x := range xs[:2000] {
		m.AscendRange(kv{k: x}, kv{k: x + 100000}, func(e kv) bool { total += e.v; return true })
	}
	for i, x := range xs[:n] {
		if i%2 == 0 {
			if e, ok := m.Delete(kv{k: x}); ok {
				total += e.v
			}
		}
	}
	return fmt.Sprintf("%d %d", m.Len(), total)
}

func seqs(n int) string {
	// The lazy pipeline: Go has no lazy map and filter (iter.Seq's
	// adapters are user code); this is the loop it would be.
	a := 0
	for x := 0; x < n; x++ {
		if y := x * 3; y%2 == 0 {
			a += y + 1
		}
	}
	// The same through arrays, a step at a time, as Asuka's
	// toArray().map().filter().map().reduce() is.
	xs := make([]int, n)
	for i := range xs {
		xs[i] = i
	}
	ys := make([]int, len(xs))
	for i, x := range xs {
		ys[i] = x * 3
	}
	var zs []int
	for _, y := range ys {
		if y%2 == 0 {
			zs = append(zs, y)
		}
	}
	ws := make([]int, len(zs))
	for i, z := range zs {
		ws[i] = z + 1
	}
	b := 0
	for _, w := range ws {
		b += w
	}
	return fmt.Sprintf("%d %d", a, b)
}

// serve answers GET /hello with text and /json with JSON, as
// ../asuka's serve does.
func serve(port string) {
	http.HandleFunc("/hello", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		w.Write([]byte("Hello, world!"))
	})
	http.HandleFunc("/json", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(struct {
			Message string   `json:"message"`
			ID      int      `json:"id"`
			Tags    []string `json:"tags"`
		}{"Hello, world!", 42, []string{"a", "b"}})
	})
	panic(http.ListenAndServe("127.0.0.1:"+port, nil))
}

func main() {
	name := ""
	if len(os.Args) > 1 {
		name = os.Args[1]
	}
	if name == "serve" {
		serve(os.Args[2])
		return
	}
	dir := "/tmp"
	if len(os.Args) > 2 {
		dir = os.Args[2]
	}
	in := func(names ...string) bool { return slices.Contains(names, name) }
	var text, uni, csvIn, tomlIn string
	var blob []byte
	var keys, strs []string
	var xs []int
	var scores []score
	if in("split", "find", "case", "count", "regex", "files", "lines", "zip", "tar") {
		text = corpus(2000000)
	}
	var marked string
	var small []byte
	var toPack []afile
	if name == "html" {
		marked = strings.ReplaceAll(strings.ReplaceAll(corpus(2000000), "kato", "<k&o>"), "chi", "c>i")
	}
	if in("base64", "hex") {
		small = []byte(corpus(2000000))
	}
	if in("zip", "tar") {
		toPack = archiveFiles(text)
	}
	if name == "case" {
		uni = strings.Repeat("Ärger über die Straße, ΣΟΦΟΣ λόγος — Ёлка и ЁЖ; ", 40000)
	}
	if in("sha256", "sha512", "aesgcm", "chacha", "gzip", "sha1", "md5", "crc32") {
		blob = []byte(corpus(10000000))
	}
	if name == "mapstr" {
		for i := 0; i < 2000000; i++ {
			keys = append(keys, "key"+strconv.Itoa((i*7919)%500009))
		}
	}
	if in("sortint", "heap", "sortedmap", "sortedops") {
		if name == "sortint" {
			xs = ints(5000000)
		} else {
			xs = ints(1000000)
		}
	}
	if name == "sortstr" {
		for _, x := range ints(1000000) {
			strs = append(strs, "w"+strconv.Itoa(x))
		}
	}
	if name == "sortby" {
		for _, x := range ints(1000000) {
			scores = append(scores, score{x, x % 100000})
		}
	}
	if name == "csv" {
		csvIn = csvText(500000)
	}
	if name == "toml" {
		tomlIn = tomlText(50000)
	}
	// STDBENCH_REPS: the work that many times (the runner's -instr).
	reps, _ := strconv.Atoi(os.Getenv("STDBENCH_REPS"))
	reps = max(reps, 1)
	t0 := time.Now()
	var out string
	for range reps {
		switch name {
		case "interp":
			out = interp(2000000)
		case "concat":
			out = concat(2000000)
		case "split":
			out = splitting(text)
		case "find":
			out = finding(text)
		case "case":
			out = casing(text, uni)
		case "count":
			out = counting(text)
		case "num":
			out = numbers(1000000)
		case "fmt":
			out = formats(1000000)
		case "mapint":
			out = mapInt(2000000)
		case "mapstr":
			out = mapStr(keys)
		case "set":
			out = sets(2000000)
		case "sortint":
			a := slices.Clone(xs)
			slices.Sort(a)
			out = strconv.Itoa(a[0] + a[len(a)/2] + a[len(a)-1])
		case "sortstr":
			a := slices.Clone(strs)
			slices.Sort(a)
			out = fmt.Sprintf("%s %s %s", a[0], a[len(a)/2], a[len(a)-1])
		case "sortby":
			a := slices.Clone(scores)
			sort.SliceStable(a, func(i, j int) bool { return a[i].score < a[j].score })
			b := slices.Clone(scores)
			sort.SliceStable(b, func(i, j int) bool { return b[i].score > b[j].score })
			out = fmt.Sprintf("%d %d %d", a[0].id, a[len(a)/2].id, b[0].id)
		case "regex":
			out = regexes(text)
		case "files":
			path := dir + "/stdbench.txt"
			os.WriteFile(path, []byte(text), 0o644)
			b1, _ := os.ReadFile(path)
			b2, _ := os.ReadFile(path)
			os.Remove(path)
			out = strconv.Itoa(len(string(b1)) + len(b2))
		case "lines":
			path := dir + "/stdbench-lines.txt"
			os.WriteFile(path, []byte(text), 0o644)
			f, _ := os.Open(path)
			sc := bufio.NewScanner(f)
			total := 0
			for sc.Scan() {
				total += len(sc.Text())
			}
			f.Close()
			os.Remove(path)
			out = strconv.Itoa(total)
		case "print":
			for i := 0; i < 1000000; i++ {
				fmt.Println("line " + strconv.Itoa(i) + " of the output")
			}
			out = "1000000"
		case "stdout":
			out = buffered(1000000)
		case "url":
			out = urls(200000)
		case "base64":
			out = base64s(small)
		case "hex":
			out = hexes(small)
		case "html":
			out = htmls(marked)
		case "zip":
			out = zips(toPack)
		case "tar":
			out = tars(toPack)
		case "sha1":
			out = hashes(blob, 0)
		case "md5":
			out = hashes(blob, 1)
		case "crc32":
			out = hashes(blob, 2)
		case "csv":
			rows, _ := csv.NewReader(strings.NewReader(csvIn)).ReadAll()
			total := 0
			for _, r := range rows {
				total += len(r) + len(r[1])
			}
			out = fmt.Sprintf("%d %d", len(rows), total)
		case "sha256":
			var s [32]byte
			for range 4 {
				s = sha256.Sum256(blob)
			}
			out = hex.EncodeToString(s[:])
		case "sha512":
			var s [64]byte
			for range 4 {
				s = sha512.Sum512(blob)
			}
			out = hex.EncodeToString(s[:])
		case "hmac":
			key := []byte("the key")
			msg := []byte("a message of some sixty-four bytes, as a request's body might be")
			for range 500000 {
				h := hmac.New(sha256.New, key)
				h.Write(msg)
				msg = h.Sum(nil)
			}
			out = hex.EncodeToString(msg)
		case "aesgcm":
			k := sha256.Sum256([]byte("key"))
			b, _ := aes.NewCipher(k[:])
			g, _ := cipher.NewGCM(b)
			out = aead(blob, g)
		case "chacha":
			k := sha256.Sum256([]byte("key"))
			a, _ := chacha20poly1305.New(k[:])
			out = aead(blob, a)
		case "argon2":
			var k []byte
			for range 2 {
				k = argon2.IDKey([]byte("password"), []byte("somesaltsomesalt"), 3, 65536, 4, 32)
			}
			out = hex.EncodeToString(k)
		case "bcrypt":
			total := 0
			for range 4 {
				h, _ := bcrypt.GenerateFromPassword([]byte("password"), 10)
				total += len(h)
			}
			out = strconv.Itoa(total)
		case "gzip":
			out = gz(blob)
		case "bigint":
			out = bigints(10000)
		case "modpow":
			out = modpows(20)
		case "time":
			out = times(300000)
		case "toml":
			out = tomlParse(tomlIn)
		case "template":
			out = templates(200)
		case "semver":
			out = semvers(200000)
		case "sortedmap":
			out = sortedMaps(xs)
		case "sortedops":
			out = sortedOps(xs)
		case "heap":
			out = heaps(xs)
		case "seq":
			out = seqs(5000000)
		default:
			out = "unknown"
		}
	}
	fmt.Fprintf(os.Stderr, "%s\t%d\n", out, time.Since(t0).Nanoseconds())
}
