// asuka/store on the web: localStorage, under the app's name.
export function makePrefs() {
  const k = (key) => "asuka.store." + key;
  return {
    get(key) { return localStorage.getItem(k(key)); },
    set(key, value) { localStorage.setItem(k(key), value); },
    remove(key) { localStorage.removeItem(k(key)); },
    // localStorage writes as it's set.
    flush() {},
    dir() { return "localStorage"; },
  };
}
