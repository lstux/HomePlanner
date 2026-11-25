export const $ = (id) => document.getElementById(id);
// Les textes saisis passent toujours par textContent (jamais de HTML).
export function el(tag, props = {}, ...children) {
  const node = Object.assign(document.createElement(tag), props);
  node.append(...children.filter((c) => c !== null && c !== undefined && c !== false));
  return node;
}
export const button = (text, className, onclick, label) =>
  el("button", { type: "button", textContent: text, className, onclick, ...(label ? { ariaLabel: label, title: label } : {}) });
