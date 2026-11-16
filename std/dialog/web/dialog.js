// asuka/dialog on the web. A browser gives a page files, never their
// paths, so the paths these give are the app's (its file system):
//
//   - open: the files copied into the app's caches (/tmp/shared), each
//     its own name. Through the system's dialog where the browser has the
//     File System Access API (showOpenFilePicker: Chrome, Edge), with the
//     filters as its types; elsewhere the browser's file input.
//   - folder: the folder copied in, with all it holds
//     (/tmp/folders/<n>/<its name>): showDirectoryPicker, else the file
//     input's folder mode (webkitdirectory). One folder.
//   - save: a path in the app's caches (/tmp/saved/<n>/<name>) whose
//     file, each time the app has written it, goes to the file the user
//     chose (showSaveFilePicker, which asks before replacing one), or
//     where the browser has no such dialog, is downloaded by its name.
//   - start: a path one of these gave, whose file or folder the system's
//     dialog starts by (startIn); else none.

const native = () => globalThis.asuka.native;

// handles are the File System Access handles behind paths the dialogs
// gave, for a later dialog's start.
const handles = new Map();
let folders = 0, saves = 0;

/** pickerTypes are the filters as showOpenFilePicker's and showSaveFilePicker's types; null: any file. */
export function pickerTypes(filters, typeOf) {
  const out = [];
  for (const f of filters || []) {
    const accept = {};
    for (const t of f.types || []) {
      if (t === "*/*" || t === "*") return null;
      accept[t] ??= [];
    }
    for (const e of f.extensions || []) {
      if (e === "*") return null;
      (accept[typeOf("x." + e) || "application/octet-stream"] ??= []).push("." + e);
    }
    out.push({ description: f.name, accept });
  }
  return out.length ? out : null;
}

/** acceptList is the filters as a file input's accept: types and extensions; none: any file. */
export function acceptList(filters) {
  const out = [];
  for (const f of filters || []) {
    for (const t of f.types || []) {
      if (t === "*/*" || t === "*") return [];
      out.push(t);
    }
    for (const e of f.extensions || []) {
      if (e === "*") return [];
      out.push("." + e);
    }
  }
  return out;
}

// pickerOptions are what the system's dialogs share: the types, where
// they start.
function pickerOptions(r) {
  const { files } = native();
  const o = {};
  const types = pickerTypes(r.filters, files.typeOf);
  if (types) o.types = types;
  const start = r.start && handles.get(files.fs.clean(r.start));
  if (start) o.startIn = start;
  return o;
}

// picked is a dialog's answer, null when the user closed it.
async function picked(f) {
  try {
    return await f();
  } catch (e) {
    if (e?.name === "AbortError") return null;
    throw e;
  }
}

async function open(r, win) {
  const { files } = native();
  if (!win.showOpenFilePicker) {
    return (await files.pick(acceptList(r.filters), r.multiple ? 0 : 1)).map((f) => f.path);
  }
  const hs = await picked(() => win.showOpenFilePicker({ ...pickerOptions(r), multiple: !!r.multiple }));
  const paths = [];
  for (const fh of hs || []) {
    const path = await files.fs.saveUpload(await fh.getFile());
    handles.set(path, fh);
    paths.push(path);
  }
  return paths;
}

async function folder(r, win) {
  const at = `/tmp/folders/${++folders}`;
  if (!win.showDirectoryPicker) {
    const root = await pickFolder(at);
    return root ? [root] : [];
  }
  const opts = pickerOptions(r);
  delete opts.types;
  const dh = await picked(() => win.showDirectoryPicker(opts));
  if (!dh) return [];
  const root = `${at}/${safe(dh.name)}`;
  await copyFolder(dh, root);
  handles.set(root, dh);
  return [root];
}

/** copyFolder copies a directory handle's files into the app's, at path. */
async function copyFolder(dh, path) {
  const { fs } = native().files;
  fs.makeDir(path);
  for await (const [name, h] of dh.entries()) {
    const to = `${path}/${safe(name)}`;
    if (h.kind === "directory") await copyFolder(h, to);
    else fs.writeFile(to, new Uint8Array(await (await h.getFile()).arrayBuffer()));
  }
}

// pickFolder is the file input's folder mode: the folder's files, each
// at its path in it, copied in at at; the folder's path, or null (a
// cancel, or an empty folder, whose name the input doesn't say).
function pickFolder(at) {
  const { fs } = native().files;
  return new Promise((resolve) => {
    const i = document.createElement("input");
    i.type = "file";
    i.webkitdirectory = true;
    i.style.display = "none";
    let done = false;
    const finish = async (list) => {
      if (done) return;
      done = true;
      i.remove();
      let root = null;
      for (const f of list) {
        const rel = (f.webkitRelativePath || f.name).split("/").map(safe);
        root ??= `${at}/${rel[0]}`;
        fs.writeFile(`${at}/${rel.join("/")}`, new Uint8Array(await f.arrayBuffer()));
      }
      resolve(root);
    };
    i.addEventListener("change", () => finish([...i.files]));
    i.addEventListener("cancel", () => finish([]));
    document.body.appendChild(i);
    i.click();
  });
}

async function save(r, win) {
  const { files } = native();
  const { fs } = files;
  const at = `/tmp/saved/${++saves}`;
  if (!win.showSaveFilePicker) {
    // No dialog: the file's downloaded by its name each time it's written.
    const name = safe(suggested(r) || "Untitled");
    const path = `${at}/${name}`;
    fs.makeDir(at);
    fs.onWritten(path, (b) => files.download(new File([b], name, { type: files.typeOf(name) || "application/octet-stream" })));
    return path;
  }
  const opts = pickerOptions(r);
  const name = suggested(r);
  if (name) opts.suggestedName = name;
  const fh = await picked(() => win.showSaveFilePicker(opts));
  if (!fh) return null;
  const path = `${at}/${safe(fh.name)}`;
  fs.makeDir(at);
  handles.set(path, fh);
  // Each write after the one before: the file the user chose has the
  // app's last.
  let writing = Promise.resolve();
  fs.onWritten(path, (b) => {
    writing = writing.then(async () => {
      const w = await fh.createWritable();
      await w.write(b);
      await w.close();
    }).catch((e) => console.error(`dialog: couldn't write ${fh.name}: ${e?.message ?? e}`));
  });
  return path;
}

// suggested is the name save suggests: the app's, else start's (a file's,
// not a folder a dialog gave).
function suggested(r) {
  if (r.name) return r.name;
  if (!r.start || handles.get(native().files.fs.clean(r.start))?.kind === "directory") return "";
  return r.start.slice(r.start.lastIndexOf("/") + 1);
}

const safe = (name) => String(name || "file").replace(/[\/\\]/g, "_");

/** makeDialogs takes the window whose dialogs it shows (tests give their own). */
export function makeChooser(win = globalThis) {
  return {
    open: (_signal, r) => open(r, win),
    save: (_signal, r) => save(r, win),
    folder: (_signal, r) => folder(r, win),
  };
}
