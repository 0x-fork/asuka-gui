// The modules platform/host's desktop packages reach by name, as a page
// has them (a page is no desktop app):
//
//   - dock (platform/dock): a page has no icon of its own: progress and
//     attention do nothing ("attention" answers 0, nothing asked).
//   - recent (platform/recent): a page has no recent documents of its own
//     with the system.
//   - login (platform/login): a page doesn't start at login.
//
// hotkey, tray and apps aren't a page's: their calls answer that they
// aren't on the web, which their packages take as unsupported.

const notOnWeb = (what, why) => new Error(`asuka: ${what} isn't on the web: ${why}`);

export function modules() {
  return {
    dock: {
      progress: () => null,
      attention: () => new Uint8Array(4), // u32 0: nothing asked
      cancelAttention: () => null,
    },
    recent: { add: () => {}, clear: () => {}, list: () => "[]" },
    login: {
      set: () => { throw notOnWeb("login.set", "a page doesn't start at login"); },
      enabled: () => new Uint8Array([0]),
      openedAtLogin: () => new Uint8Array([0]),
    },
  };
}
