import { $, el, button } from "./dom.js";
import { upsert, remove, alive, newId } from "./lists.js";

// À acheter d'abord, cochés à la fin ; chacun garde la place choisie (`order`), sinon ordre alphabétique.
export function sortItems(items) {
  return [...items].sort((a, b) => (a.done ? 1 : 0) - (b.done ? 1 : 0)
    || (a.order ?? Infinity) - (b.order ?? Infinity) || a.name.localeCompare(b.name, "fr"));
}
// Range les articles dans l'ordre donné (liste d'identifiants) ; ne réécrit que ce qui change.
export function applyOrder(list, ids) {
  const byId = new Map(alive(list).map((i) => [i.id, i]));
  ids.forEach((id, index) => { if (byId.get(id) && byId.get(id).order !== index) upsert(list, id, { order: index }); });
}
export const nextOrder = (items) => items.reduce((m, i) => Math.max(m, i.order ?? -1), -1) + 1;

const withId = (node, id) => { node.dataset.id = id; return node; };

export function initShopping(app) {
  const list = () => app.lists.shopping;
  const done = () => { app.changed(); render(); };
  let editing = null;

  const ids = () => sortItems(alive(list())).map((i) => i.id);
  function move(id, delta) {
    const order = ids(), from = order.indexOf(id), to = from + delta;
    if (to < 0 || to >= order.length) return;
    order.splice(to, 0, order.splice(from, 1)[0]);
    applyOrder(list(), order); done();
  }
  function drag(grip, row) {
    grip.onpointerdown = (down) => {
      down.preventDefault();
      const parent = row.parentNode, rows = () => [...parent.children];
      row.classList.add("dragging");
      const onMove = (e) => {
        for (const other of rows()) {
          if (other === row) continue;
          const box = other.getBoundingClientRect(), middle = box.top + box.height / 2;
          const above = !!(row.compareDocumentPosition(other) & Node.DOCUMENT_POSITION_PRECEDING);
          if (above && e.clientY < middle) { parent.insertBefore(row, other); break; }
          if (!above && e.clientY > middle) parent.insertBefore(other, row);
        }
        if (e.clientY < 70) window.scrollBy(0, -14); else if (e.clientY > innerHeight - 70) window.scrollBy(0, 14);
      };
      const onUp = () => {
        removeEventListener("pointermove", onMove); removeEventListener("pointerup", onUp); removeEventListener("pointercancel", onUp);
        applyOrder(list(), rows().map((r) => r.dataset.id)); done();
      };
      addEventListener("pointermove", onMove); addEventListener("pointerup", onUp); addEventListener("pointercancel", onUp);
    };
  }

  function editRow(item) {
    const input = el("input", { type: "text", value: item.name, maxLength: 60, ariaLabel: "Modifier l'article" });
    const save = () => {
      const name = input.value.trim();
      editing = null;
      if (name && name !== item.name) upsert(list(), item.id, { name });
      done();
    };
    input.onkeydown = (e) => { if (e.key === "Enter") { e.preventDefault(); save(); } if (e.key === "Escape") { editing = null; render(); } };
    input.oninput = () => { editing = { id: item.id, text: input.value }; };
    if (editing?.id === item.id) input.value = editing.text;
    queueMicrotask(() => { input.focus(); input.setSelectionRange(input.value.length, input.value.length); });
    return withId(el("li", { className: "item editing" }, input,
      button("▲", "mini", () => move(item.id, -1), "Monter"), button("▼", "mini", () => move(item.id, 1), "Descendre"),
      button("OK", "mini ok", save, "Enregistrer")), item.id);
  }

  function render() {
    const items = sortItems(alive(list()));
    $("shopping-list").replaceChildren(...items.map((item) => {
      if (editing?.id === item.id) return editRow(item);
      const toggle = button("", "step" + (item.done ? " done" : ""), () => { upsert(list(), item.id, { done: !item.done }); done(); });
      toggle.setAttribute("aria-pressed", String(!!item.done));
      toggle.append(el("span", { className: "step-text" }, el("strong", { textContent: item.name })),
        el("span", { className: "check", textContent: item.done ? "✓" : "" }));
      const grip = el("button", { type: "button", className: "grip", textContent: "⠿", ariaLabel: "Glisser pour déplacer " + item.name });
      const row = withId(el("li", { className: "item" }, grip, toggle,
        button("✏️", "mini", () => { editing = { id: item.id, text: item.name }; render(); }, "Modifier " + item.name),
        button("✕", "mini", () => { remove(list(), item.id); done(); }, "Retirer " + item.name)), item.id);
      drag(grip, row);
      return row;
    }));
    $("shopping-empty").hidden = items.length > 0;
    $("shopping-clear").hidden = !items.some((i) => i.done);
  }

  $("shopping-form").onsubmit = (event) => {
    event.preventDefault();
    const name = $("shopping-input").value.trim();
    if (!name) return;
    const existing = alive(list()).find((i) => i.name.toLowerCase() === name.toLowerCase());
    upsert(list(), existing?.id || newId(), existing ? { name, done: false } : { name, done: false, order: nextOrder(alive(list())) });
    $("shopping-input").value = "";
    done();
  };
  $("shopping-clear").onclick = () => { alive(list()).filter((i) => i.done).forEach((i) => remove(list(), i.id)); done(); };
  return render;
}
