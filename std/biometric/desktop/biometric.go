// asuka/biometric on Windows (macOS runs the package's Swift; Linux has
// none: Asuka's side answers none and notAvailable).
package biometric

type authenticator struct{}

func newAuthenticator() (Authenticator, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return authenticator{}, nil
}
