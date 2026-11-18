//go:build ignore

// gen.go writes the certificates std/net's TLS tests use
// (core/test/tls_test.c reads this directory's files; std/net's test
// blocks and lang/compile's run test have the same as Asuka,
// std/net/certs_test.asuka), from keys made fresh each run:
//
//	go run gen.go
//
// A test root ("Asuka Test Root CA"), an intermediate it signs, and the
// leaves the intermediate signs: server.pem (localhost, test.example,
// 127.0.0.1, ::1; the chain: leaf, intermediate), rsa.pem (an RSA-2048
// leaf for rsa.example: a TLS 1.2 handshake signs with RSA), expired.pem
// (localhost, valid in 2000 only), client.pem (a client's: "Asuka Test
// Client"); other-ca.pem, a root that signed none of them. Each leaf's
// key beside it (<name>.key, PKCS#8). The roots and leaves are valid
// until 2100, so the files are kept, not made per run.
package main

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"log"
	"math/big"
	"net"
	"os"
	"strings"
	"time"
)

var (
	from  = time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC)
	until = time.Date(2100, 1, 1, 0, 0, 0, 0, time.UTC)
	files = map[string]string{}
	order []string
)

func main() {
	root, rootKey := ca("Asuka Test Root CA", nil, nil, 1)
	inter, interKey := ca("Asuka Test Intermediate CA", root, rootKey, 2)
	other, _ := ca("Asuka Other Root CA", nil, nil, 3)
	put("ca.pem", pemCert(root))
	put("other-ca.pem", pemCert(other))

	leaf("server", ecKey(), &x509.Certificate{
		SerialNumber: big.NewInt(10),
		Subject:      pkix.Name{CommonName: "localhost"},
		DNSNames:     []string{"localhost", "test.example"},
		IPAddresses:  []net.IP{net.ParseIP("127.0.0.1"), net.ParseIP("::1")},
		NotBefore:    from, NotAfter: until,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
	}, inter, interKey)
	rk, err := rsa.GenerateKey(rand.Reader, 2048)
	check(err)
	leaf("rsa", rk, &x509.Certificate{
		SerialNumber: big.NewInt(11),
		Subject:      pkix.Name{CommonName: "rsa.example"},
		DNSNames:     []string{"rsa.example"},
		NotBefore:    from, NotAfter: until,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
	}, inter, interKey)
	leaf("expired", ecKey(), &x509.Certificate{
		SerialNumber: big.NewInt(12),
		Subject:      pkix.Name{CommonName: "localhost"},
		DNSNames:     []string{"localhost"},
		IPAddresses:  []net.IP{net.ParseIP("127.0.0.1")},
		NotBefore:    time.Date(2000, 1, 1, 0, 0, 0, 0, time.UTC),
		NotAfter:     time.Date(2001, 1, 1, 0, 0, 0, 0, time.UTC),
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
	}, inter, interKey)
	leaf("client", ecKey(), &x509.Certificate{
		SerialNumber: big.NewInt(13),
		Subject:      pkix.Name{CommonName: "Asuka Test Client"},
		NotBefore:    from, NotAfter: until,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}, inter, interKey)

	for _, name := range order {
		check(os.WriteFile(name, []byte(files[name]), 0o644))
	}
	// The same as Asuka constants.
	var b strings.Builder
	b.WriteString("// Written by core/test/testdata/tls/gen.go: the TLS tests' certificates\n// (a test root, an intermediate it signs, leaves the intermediate signs).\n// Functions, not constants: a build that isn't the tests' leaves them out.\n\n")
	for _, name := range order {
		id := constName(name)
		fmt.Fprintf(&b, "fn %s() -> string = \"%s\"\n", id, strings.ReplaceAll(files[name], "\n", "\\n"))
	}
	check(os.WriteFile("../../../../std/net/certs_test.asuka", []byte(b.String()), 0o644))
}

func constName(file string) string {
	s := strings.NewReplacer(".pem", "Cert", ".key", "Key", "-", " ").Replace(file)
	parts := strings.Fields(s)
	for i := range parts {
		if i > 0 {
			parts[i] = strings.ToUpper(parts[i][:1]) + parts[i][1:]
		}
	}
	return "test" + strings.ToUpper(parts[0][:1]) + parts[0][1:] + strings.Join(parts[1:], "")
}

func put(name, body string) {
	files[name] = body
	order = append(order, name)
}

func ecKey() crypto.Signer {
	k, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	check(err)
	return k
}

func ca(name string, parent *x509.Certificate, parentKey crypto.Signer, serial int64) (*x509.Certificate, crypto.Signer) {
	k := ecKey()
	t := &x509.Certificate{
		SerialNumber:          big.NewInt(serial),
		Subject:               pkix.Name{CommonName: name, Organization: []string{"Asuka"}},
		NotBefore:             from,
		NotAfter:              until,
		IsCA:                  true,
		BasicConstraintsValid: true,
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
	}
	if parent == nil {
		parent, parentKey = t, k
	}
	der, err := x509.CreateCertificate(rand.Reader, t, parent, k.Public(), parentKey)
	check(err)
	c, err := x509.ParseCertificate(der)
	check(err)
	return c, k
}

func leaf(name string, k crypto.Signer, t *x509.Certificate, parent *x509.Certificate, parentKey crypto.Signer) {
	t.KeyUsage = x509.KeyUsageDigitalSignature
	if _, ok := k.(*rsa.PrivateKey); ok {
		t.KeyUsage |= x509.KeyUsageKeyEncipherment
	}
	der, err := x509.CreateCertificate(rand.Reader, t, parent, k.Public(), parentKey)
	check(err)
	c, err := x509.ParseCertificate(der)
	check(err)
	put(name+".pem", pemCert(c)+pemCert(parent))
	p8, err := x509.MarshalPKCS8PrivateKey(k)
	check(err)
	put(name+".key", string(pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: p8})))
}

func pemCert(c *x509.Certificate) string {
	return string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: c.Raw}))
}

func check(err error) {
	if err != nil {
		log.Fatal(err)
	}
}
