//go:build ignore

// convert.go writes the subsets of Wycheproof's test vectors
// (https://github.com/C2SP/wycheproof, testvectors_v1) that
// core/test/aead_test.c and pubkey_test.c read, one case a line, from
// the JSON files in the directory it's given:
//
//	go run convert.go ~/wycheproof/testvectors_v1
//
// AEAD lines: result key nonce aad msg ct‖tag. Signature files: a line
// "key <SubjectPublicKeyInfo>" before its group's cases, each
// "result msg sig". result is v (valid), i (invalid) or a (acceptable:
// either answer is right); "-" is empty. To keep them small: AEAD cases
// of messages up to 64 bytes; for P-256 (r ‖ s) every group, its keys'
// edge cases too; for the others the group with the most cases, the
// signatures' edge cases; each without messages over 64 bytes. PSS's
// cases of another salt length than the file's are acceptable: std/crypto
// verifies PSS with any salt length, as Go's and OpenSSL's verify by
// default (OpenSSL signs with the longest salt unless told).
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

type test struct {
	TcID    int    `json:"tcId"`
	Key     string `json:"key"`
	IV      string `json:"iv"`
	AAD     string `json:"aad"`
	Msg     string `json:"msg"`
	CT      string `json:"ct"`
	Tag     string `json:"tag"`
	Sig     string `json:"sig"`
	Result  string `json:"result"`
	Comment string `json:"comment"`
}

type group struct {
	IVSize       int    `json:"ivSize"`
	KeySize      int    `json:"keySize"`
	TagSize      int    `json:"tagSize"`
	PublicKeyDer string `json:"publicKeyDer"`
	Tests        []test `json:"tests"`
}

func e(s string) string {
	if s == "" {
		return "-"
	}
	return s
}

func main() {
	dir := os.Args[1]
	aead := func(name string, iv int, keys ...int) {
		var f struct{ TestGroups []group }
		b, err := os.ReadFile(filepath.Join(dir, name+".json"))
		if err != nil {
			panic(err)
		}
		json.Unmarshal(b, &f)
		var out strings.Builder
		for _, g := range f.TestGroups {
			ok := g.IVSize == iv && g.TagSize == 128
			if ok && len(keys) > 0 {
				ok = false
				for _, k := range keys {
					ok = ok || g.KeySize == k
				}
			}
			if !ok {
				continue
			}
			for _, t := range g.Tests {
				if len(t.Msg) > 128 {
					continue
				}
				fmt.Fprintf(&out, "%c %s %s %s %s %s\n", t.Result[0], t.Key, t.IV, e(t.AAD), e(t.Msg), e(t.CT+t.Tag))
			}
		}
		os.WriteFile(name+".txt", []byte(out.String()), 0o644)
	}
	aead("aes_gcm_test", 96, 128, 256)
	aead("chacha20_poly1305_test", 96)
	aead("xchacha20_poly1305_test", 192)
	for _, name := range []string{"ecdsa_secp256r1_sha256_p1363_test", "ecdsa_secp256r1_sha256_test", "ecdsa_secp384r1_sha384_p1363_test",
		"ecdsa_secp521r1_sha512_p1363_test", "rsa_signature_2048_sha256_test", "rsa_pss_2048_sha256_mgf1_32_test"} {
		var f struct{ TestGroups []group }
		b, err := os.ReadFile(filepath.Join(dir, name+".json"))
		if err != nil {
			panic(err)
		}
		json.Unmarshal(b, &f)
		var out strings.Builder
		most := 0
		for i, g := range f.TestGroups {
			if len(g.Tests) > len(f.TestGroups[most].Tests) {
				most = i
			}
		}
		for i, g := range f.TestGroups {
			if i != most && name != "ecdsa_secp256r1_sha256_p1363_test" {
				continue
			}
			fmt.Fprintf(&out, "key %s\n", g.PublicKeyDer)
			for _, t := range g.Tests {
				if len(t.Msg) > 128 {
					continue
				}
				r := t.Result[0]
				if strings.HasPrefix(t.Comment, "s_len changed") {
					r = 'a'
				}
				fmt.Fprintf(&out, "%c %s %s\n", r, e(t.Msg), e(t.Sig))
			}
		}
		os.WriteFile(name+".txt", []byte(out.String()), 0o644)
	}
}
