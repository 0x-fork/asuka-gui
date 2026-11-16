// asuka/preview on the web: the start file in a new tab, which the
// browser shows (images, video, audio, PDFs, text, HTML); noViewer for
// what it doesn't.
import { PreviewError } from "./preview.asuka.js";

const viewable = (t) => /^(image|video|audio|text)\//.test(t) || ["application/pdf", "application/json", "application/xml", "application/xhtml+xml"].includes(t);

export function makeViewer() {
  return {
    async show(_signal, sources, start, _edit) {
      const { files } = globalThis.asuka.native;
      const source = sources[Math.min(Math.max(start, 0), sources.length - 1)];
      const type = files.typeOf(source);
      if (!viewable(type)) throw new PreviewError("noViewer");
      // The tab opens now, while the user's tap still counts; the file follows.
      const w = window.open("", "_blank");
      if (!w) throw new Error("preview: the browser blocked the new tab");
      w.opener = null;
      try {
        const url = files.sourceURL(source);
        if (url && /^https?:/.test(url)) {
          w.location.href = url;
          return;
        }
        const blob = await files.blob(source, type.startsWith("text/") && !type.includes("charset") ? type + ";charset=utf-8" : type);
        const u = URL.createObjectURL(blob);
        w.location.href = u;
        setTimeout(() => URL.revokeObjectURL(u), 10 * 60000);
      } catch (e) {
        w.close();
        throw e;
      }
    },
  };
}
