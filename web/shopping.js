import { $, el, button } from "./dom.js";
import { upsert, remove, alive, newId } from "./lists.js";

export function initShopping(app) {
  const list = () => app.lists.shopping;
  const done = () => { app.changed(); render(); };

  function render() {
    const items = alive(list()).sort((a, b) => (a.done ? 1 : 0) - (b.done ? 1 : 0) || a.name.localeCompare(b.name, "fr"));
    $("shopping-list").replaceChildren(...items.map((item) => {
      const toggle = button("", "step" + (item.done ? " done" : ""), () => { upsert(list(), item.id, { done: !item.done }); done(); });
      toggle.setAttribute("aria-pressed", String(!!item.done));
      toggle.append(el("span", { className: "step-text" }, el("strong", { textContent: item.name })),
        el("span", { className: "check", textContent: item.done ? "✓" : "" }));
      return el("li", { className: "item" }, toggle, button("✕", "mini", () => { remove(list(), item.id); done(); }, "Retirer " + item.name));
    }));
    $("shopping-empty").hidden = items.length > 0;
    $("shopping-clear").hidden = !items.some((i) => i.done);
  }

  $("shopping-form").onsubmit = (event) => {
    event.preventDefault();
    const name = $("shopping-input").value.trim();
    if (!name) return;
    const existing = alive(list()).find((i) => i.name.toLowerCase() === name.toLowerCase());
    upsert(list(), existing?.id || newId(), { name, done: false });
    $("shopping-input").value = "";
    done();
  };
  $("shopping-clear").onclick = () => { alive(list()).filter((i) => i.done).forEach((i) => remove(list(), i.id)); done(); };
  return render;
}
