/* Spousti CELE SPA (servirovane JS) v Node nad univerzalni atrapou DOM a umozni volat jeji funkce -- kdyz test z `grab()` nestaci,
 * protoze funkce potrebuje desitky dalsich (detail Allanu, prepinace). Pouziti: require('./spa_harness.js')(jsPath, testBody)
 * -- `testBody` je text JS vlozeny ZA skript, ve stejnem rozsahu (vidi vsechny `var`/`function` SPA). Vraci hodnotu `return` z testBody.
 * Atrapa: kazdy element pamatuje atributy/deti; neznama vlastnost/metoda = no-op funkce (proto se `.value`, `.checked` atd. nastavuji
 * explicitne v testu). Netestuje vzhled ani layout -- jen to, ze kod nevyhodi vyjimku a nakresli, co ma. */
const fs = require('fs');

function makeEnv() {
  const byId = {};
  const handlers = {};
  function El(tag) {
    const o = {
      tag, attrs: {}, children: [], style: {}, className: '', textContent: '', _html: '', value: '', checked: false, disabled: false, title: '',
      dataset: {}, listeners: {}, firstChild: null,
      setAttribute(k, v) { this.attrs[k] = String(v); },
      getAttribute(k) { return (k in this.attrs) ? this.attrs[k] : null; },
      removeAttribute(k) { delete this.attrs[k]; },
      appendChild(c) { this.children.push(c); this.firstChild = this.children[0]; return c; },
      removeChild(c) { const i = this.children.indexOf(c); if (i >= 0) this.children.splice(i, 1); this.firstChild = this.children[0] || null; return c; },
      addEventListener(ev, fn) { (this.listeners[ev] = this.listeners[ev] || []).push(fn); },
      querySelector() { return El('div'); },
      getElementsByTagName() { return []; },
      querySelectorAll() { return []; },
      getBoundingClientRect() { return { left: 0, top: 0, width: 100, height: 100 }; },
      classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
      set innerHTML(v) { this._html = v; if (v === '') { this.children = []; this.firstChild = null; } },
      get innerHTML() { return this._html; },
      focus() {}, click() {},
    };
    return o;
  }
  const document = {
    getElementById(id) { return byId[id] || (byId[id] = El('div')); },
    createElement: El, createElementNS: (ns, t) => El(t),
    querySelector: () => El('div'), querySelectorAll: () => [], getElementsByClassName: () => [], getElementsByTagName: () => [],
    listeners: {}, addEventListener(ev, fn) { (this.listeners[ev] = this.listeners[ev] || []).push(fn); },
    body: El('body'), documentElement: El('html'), title: '',
  };
  return { document, byId };
}

module.exports = function (jsPath, testBody) {
  const js = fs.readFileSync(jsPath, 'utf8');
  const { document, byId } = makeEnv();
  const store = {};
  const localStorage = { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); }, removeItem: k => { delete store[k]; } };
  const window = { addEventListener() {}, innerWidth: 1200, innerHeight: 800, matchMedia: () => ({ matches: false, addEventListener() {} }), location: { hash: '', search: '' } };
  const fetch = () => Promise.resolve({ ok: false, json: () => Promise.resolve(null), text: () => Promise.resolve('') });
  function EventSource() { this.close = () => {}; }
  const names = ['document', 'window', 'localStorage', 'fetch', 'EventSource', 'setInterval', 'setTimeout', 'clearInterval', 'clearTimeout',
                 'navigator', 'location', 'Blob', 'URL', 'byId'];
  const vals = [document, window, localStorage, fetch, EventSource, () => 0, () => 0, () => {}, () => {},
                { userAgent: 'node' }, window.location, function () {}, { createObjectURL: () => '', revokeObjectURL() {} }, byId];
  const fn = new Function(...names, js + '\n;' + testBody);
  return fn(...vals);
};
