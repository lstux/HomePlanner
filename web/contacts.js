// Carnet de contacts partagé par le foyer.
import { $, el, button } from "./dom.js";
import { upsert, remove, alive, newId } from "./lists.js";
import { whereIs, nearbyPlace, position } from "./map.js";
import { searchPlaces } from "./geo.js";

const FIELDS = ["name", "relation", "emoji", "phone", "note", "homeId", "nowId"];
export const telLink = (phone) => "tel:" + phone.replace(/[^\d+*#]/g, "");

export function initContacts(app) {
  let editing = null, address = null; // adresse choisie, pas encore enregistrée
  const note = (text) => $("address-results").replaceChildren(...(text ? [el("li", { className: "muted", textContent: text })] : []));
  async function search() {
    const query = $("contact-address").value.trim();
    if (!query) return;
    note("Recherche…");
    try {
      const results = await searchPlaces(query);
      if (!results.length) { note("Rien trouvé. Essaie avec la ville, ou place le lieu depuis la carte."); return; }
      $("address-results").replaceChildren(...results.map((r) => el("li", {}, button(r.label, "button secondary", () => {
        address = r; $("contact-address").value = r.label; $("contact-homeId").value = "";
        note("✓ Adresse choisie : le lieu sera ajouté à la carte à l'enregistrement.");
      }))));
    } catch (e) { note(e.message); }
  }
  $("address-search").onclick = search;
  $("contact-address").onkeydown = (event) => { if (event.key === "Enter") { event.preventDefault(); search(); } };
  $("contact-address").oninput = () => { address = null; note(""); };
  $("contact-homeId").onchange = () => { address = null; $("contact-address").value = ""; note(""); };
  // Réutilise un lieu tout proche, sinon crée « Chez … ».
  function homeFor(name) {
    const known = nearbyPlace(alive(app.lists.places), address);
    if (known) return known.id;
    return upsert(app.lists.places, newId(), { name: `Chez ${name}`.slice(0, 40), emoji: "🏡", kind: "family", address: address.label, ...position(address) });
  }
  function edit(contact) {
    if (!app.adult()) return;
    editing = contact;
    address = null; $("contact-address").value = ""; note("");
    const places = alive(app.lists.places);
    for (const id of ["homeId", "nowId"])
      $("contact-" + id).replaceChildren(el("option", { value: "", textContent: id === "homeId" ? "Non renseigné" : "Chez elle / chez lui" }),
        ...places.map((p) => el("option", { value: p.id, textContent: `${p.emoji} ${p.name}` })));
    for (const f of FIELDS) $("contact-" + f).value = contact[f] || "";
    $("contact-delete").hidden = !contact.id;
    $("contact-form").hidden = false;
    $("contact-form").scrollIntoView({ block: "start" });
  }
  const close = () => { editing = null; address = null; $("contact-form").hidden = true; };

  function render() {
    const places = alive(app.lists.places);
    const list = alive(app.lists.contacts).sort((a, b) => a.name.localeCompare(b.name, "fr"));
    $("contact-empty").hidden = list.length > 0;
    $("contact-list").replaceChildren(...list.map((c) => {
      const place = whereIs(c, places);
      return el("li", { className: "contact" },
        el("span", { className: "step-icon", textContent: c.emoji || "🙂" }),
        el("div", { className: "step-text" },
          el("strong", { textContent: c.name }),
          el("small", { textContent: [c.relation, place && `${place.emoji} ${place.name}`, c.note].filter(Boolean).join(" · ") })),
        c.phone ? el("a", { className: "call", href: telLink(c.phone), textContent: "📞", ariaLabel: "Appeler " + c.name }) : null,
        button("✏️", "mini", () => edit(c), "Modifier " + c.name));
    }));
  }

  $("contact-add").onclick = () => edit({});
  $("contact-cancel").onclick = close;
  $("contact-form").onsubmit = (event) => {
    event.preventDefault();
    const values = Object.fromEntries(FIELDS.map((f) => [f, $("contact-" + f).value.trim()]));
    if (!values.name || !editing) return;
    if (!address && $("contact-address").value.trim() && !confirm("L'adresse tapée n'a pas été choisie dans la recherche. Enregistrer sans elle ?")) return;
    if (address) values.homeId = homeFor(values.name);
    upsert(app.lists.contacts, editing.id || newId(), values);
    close(); app.changed(); render();
  };
  $("contact-delete").onclick = () => {
    if (!editing?.id || !confirm("Supprimer ce contact ?")) return;
    remove(app.lists.contacts, editing.id); close(); app.changed(); render();
  };
  return render;
}
