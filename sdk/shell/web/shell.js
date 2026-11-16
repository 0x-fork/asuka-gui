// platform/shell on the web: a page has no file manager, so reveal and trash
// are unsupported and beep does nothing; open shows the file in a new tab
// (what the browser shows: images, video, audio, PDFs, text).
import { ShellError } from "./shell.asuka.js";

const native = () => globalThis.asuka.native;
const viewable = (t) => /^(image|video|audio|text)\//.test(t) || ["application/pdf", "application/json", "application/xml", "application/xhtml+xml"].includes(t);

export function makeDesktop() {
  return {
    async reveal(_signal, _path) { throw new (native().UnsupportedError)("shell: a page has no file manager"); },
    async open(_signal, path) {
      const { files } = native();
      if (!files.fs.readFile(path)) throw new Error("shell: no file at " + path);
      const type = files.typeOf(path);
      if (!viewable(type)) throw new ShellError("noApp");
      // The tab opens now, while the user's tap still counts.
      const w = window.open("", "_blank");
      if (!w) throw new Error("shell: the browser blocked the new tab");
      w.opener = null;
      const u = URL.createObjectURL(await files.blob(path, type.startsWith("text/") && !type.includes("charset") ? type + ";charset=utf-8" : type));
      w.location.href = u;
      setTimeout(() => URL.revokeObjectURL(u), 10 * 60000);
    },
    async trash(_signal, _path) { throw new (native().UnsupportedError)("shell: a page has no trash"); },
    beep() {},
  };
}
