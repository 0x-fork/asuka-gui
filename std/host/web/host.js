// asuka/host on the web: the page's host's own modules (native.js's
// callModule).
export function makeModules() {
  const native = globalThis.asuka.native;
  return {
    async call(signal, module, method, args) { return native.callModule(module, method, args); },
    send(module, method, args) { native.callModule(module, method, args).catch(() => {}); },
  };
}
