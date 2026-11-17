// platform/update on the web: the browser loads the app's newest files from
// its server as the page loads, so an update is a reload. asuka build
// --platform web writes version.json beside index.html (the version it
// built, its release notes and when); latest reads it again, past the
// browser's caches, and the package offers a newer version as a release
// whose relaunch reloads the page. A development build (asuka dev) has
// no version to compare: it reloads itself as the app changes.

const native = globalThis.asuka.native;

export function makePage() {
  return {
    current() {
      return native.app.dev ? "" : native.app.version || "";
    },

    async latest(signal) {
      const u = new URL("version.json?t=" + Date.now().toString(36), document.baseURI);
      const res = await fetch(u, { cache: "no-store", signal });
      if (res.status === 404) return { version: "", notes: "", date: "" }; // a build before version.json
      if (!res.ok) throw new Error(`GET ${u}: ${res.status}`);
      const v = await res.json();
      return { version: String(v.version || ""), notes: String(v.notes || ""), date: String(v.date || "") };
    },

    // The app's own files are written as the page goes (fs.js, on
    // pagehide), so a reload keeps them.
    reload() {
      location.reload();
    },
  };
}
