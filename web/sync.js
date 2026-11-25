// Synchronisation avec la centrale. Logique sans DOM, testée avec Node.
//
// L'état local sert de file d'attente durable : à chaque cycle on lit
// l'instantané de la centrale, on fusionne, puis on envoie ce qui diffère.
//  - progression : par étape, la marque la plus récente gagne (+t cochée, -t décochée) ;
//  - réglages : contrôle de révision ; en cas de modifications des deux côtés, la centrale gagne.

export const MAX_PAYLOAD = 8192; // octets, limite du firmware

export function mergeMarks(a = {}, b = {}) {
  const out = { ...a };
  for (const [step, t] of Object.entries(b))
    if (Number.isFinite(t) && (!(step in out) || Math.abs(t) > Math.abs(out[step]))) out[step] = t;
  return out;
}

// Comparaison insensible à l'ordre des clés.
export function stable(value) {
  if (Array.isArray(value)) return `[${value.map(stable).join(",")}]`;
  if (value && typeof value === "object")
    return `{${Object.keys(value).sort().map((k) => `${JSON.stringify(k)}:${stable(value[k])}`).join(",")}}`;
  return JSON.stringify(value);
}

export const validConfig = (c) => !!c && c.version === 1 && Array.isArray(c.members) && c.members.length > 0
  && Array.isArray(c.routines) && c.routines.length > 0 && c.routines.every((r) => Array.isArray(r.steps) && Array.isArray(r.days));

const parse = (text) => { try { return JSON.parse(text); } catch { return null; } };
const opId = () => `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;

// app : { get config, set config, progress, meta, canApplyConfig(), save() }
// request(path, body?) : renvoie le JSON de la centrale ou lève une erreur.
export async function syncOnce(app, request) {
  const snapshot = await request("/api/v1/sync/snapshot");
  const remote = new Map(snapshot.entities.map((e) => [`${e.type}/${e.id}`, e]));
  const result = { configChanged: false, progressChanged: false, replacedLocalConfig: false, pending: false };
  const meta = app.meta;
  if (meta.central !== snapshot.central_id) Object.assign(meta, { central: snapshot.central_id, rev: 0 });

  const push = async (type, id, current, payload) => {
    if (new TextEncoder().encode(payload).length > MAX_PAYLOAD) throw new Error("Réglages trop volumineux pour la centrale");
    const response = await request("/api/v1/sync/push", { protocol_version: 1, commands: [{
      op_id: opId(), entity_type: type, entity_id: id,
      action: current ? "set" : "create", expected_revision: current ? current.revision : 0, payload }] });
    const r = response.results[0];
    if (r.status === "applied" || r.status === "duplicate") return r.entity_revision;
    result.pending = true; // conflit : nouvelle fusion au prochain cycle
    return null;
  };

  // 1. Réglages
  const entity = remote.get("config/family");
  const server = entity && !entity.deleted ? parse(entity.payload) : null;
  if (app.canApplyConfig()) {
    const same = server && stable(server) === stable(app.config);
    if (same) Object.assign(meta, { rev: entity.revision, dirty: false });
    else if (server && (!meta.dirty || entity.revision !== meta.rev)) {
      if (!validConfig(server)) throw new Error("Réglages de la centrale illisibles");
      result.replacedLocalConfig = !!meta.dirty;
      app.config = server;
      Object.assign(meta, { rev: entity.revision, dirty: false });
      result.configChanged = true;
    } else {
      const sent = stable(app.config);
      const rev = await push("config", "family", entity, JSON.stringify(app.config));
      if (rev !== null) Object.assign(meta, { rev, dirty: stable(app.config) !== sent });
    }
  }

  // 2. Progression du jour
  const today = app.progress.date;
  for (const member of app.config.members) {
    for (const routine of app.config.routines) {
      const key = `${member.id}/${routine.id}`;
      const found = remote.get(`progress/${key}`);
      const data = found && !found.deleted ? parse(found.payload) : null;
      if (data?.date > today) continue; // un autre appareil est déjà au lendemain
      const theirs = data?.date === today ? data.steps : {};
      if (app.progress.date !== today) return result; // minuit pendant la synchro
      const mine = app.progress.done[key] || {};
      const merged = mergeMarks(mine, theirs);
      if (!Object.keys(merged).length) continue;
      if (stable(merged) !== stable(mine)) { app.progress.done[key] = merged; result.progressChanged = true; }
      if (stable(merged) !== stable(theirs)) await push("progress", key, found, JSON.stringify({ date: today, steps: merged }));
    }
  }
  app.save();
  return result;
}
