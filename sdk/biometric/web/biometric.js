// platform/biometric on the web: WebAuthn's platform authenticator (Touch
// ID, Face ID, Windows Hello, Android's screen lock): the first
// authenticate makes a passkey for the site (its ID kept where the web
// host kept it), later ones ask it with user verification. The prompt is
// the browser's: the reason and the cancel title don't show.
import { BiometricError, Kind } from "./biometric.asuka.js";

const b64url = {
  encode: (buf) => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, ""),
  decode: (s) => Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0)),
};

/** userVerified reads WebAuthn authenticator data's UV flag. */
export function userVerified(authData) {
  const b = new Uint8Array(authData);
  return b.length > 32 && (b[32] & 0x04) !== 0;
}

async function available() {
  try {
    return !!globalThis.PublicKeyCredential && await PublicKeyCredential.isUserVerifyingPlatformAuthenticatorAvailable();
  } catch {
    return false;
  }
}

const random = (n) => crypto.getRandomValues(new Uint8Array(n));

function errorOf(e) {
  switch (e?.name) {
    case "NotAllowedError":
    case "AbortError":
      return new BiometricError("canceled");
    case "SecurityError":
    case "NotSupportedError":
      return new BiometricError("notAvailable");
  }
  return new BiometricError("failed");
}

export function makeAuthenticator() {
  const name = () => globalThis.asuka.native.app.name || document.title || "App";
  const key = () => "asuka." + globalThis.asuka.native.app.name + ".biometric.credential";
  return {
    async check(_signal) {
      if (!await available()) return { kind: Kind.none, enrolled: false, passcode: false };
      // Face ID on iPhones and iPads, else a fingerprint (a guess: WebAuthn doesn't say).
      return { kind: /iPhone|iPad/.test(navigator.userAgent) ? Kind.face : Kind.fingerprint, enrolled: true, passcode: true };
    },
    async authenticate(signal, _reason, _cancel, _noPasscode) {
      if (!await available()) throw new BiometricError("notAvailable");
      let id = null;
      try { id = localStorage.getItem(key()); } catch {}
      try {
        if (!id) {
          const c = await navigator.credentials.create({
            signal,
            publicKey: {
              challenge: random(32),
              rp: { name: name() },
              user: { id: random(16), name: name(), displayName: name() },
              pubKeyCredParams: [{ type: "public-key", alg: -7 }, { type: "public-key", alg: -257 }],
              authenticatorSelection: { authenticatorAttachment: "platform", userVerification: "required", residentKey: "discouraged" },
              attestation: "none",
              timeout: 120000,
            },
          });
          try { localStorage.setItem(key(), b64url.encode(c.rawId)); } catch {}
          return; // making it verified the user
        }
        const a = await navigator.credentials.get({
          signal,
          publicKey: {
            challenge: random(32),
            allowCredentials: [{ type: "public-key", id: b64url.decode(id), transports: ["internal"] }],
            userVerification: "required",
            timeout: 120000,
          },
        });
        if (!userVerified(a.response.authenticatorData)) throw new BiometricError("failed");
      } catch (e) {
        if (e instanceof BiometricError) throw e;
        if (signal.aborted) throw e;
        throw errorOf(e);
      }
    },
  };
}
