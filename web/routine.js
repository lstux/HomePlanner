// Logique pure des routines : aucune dépendance au navigateur (testée avec Node).

export function dateKey(now) {
  const p = (n) => String(n).padStart(2, "0");
  return `${now.getFullYear()}-${p(now.getMonth() + 1)}-${p(now.getDate())}`;
}

// Routine proposée selon le jour et l'heure (bascule vers le soir à 14 h).
export function suggestRoutine(config, now) {
  if (config.holidays && now.getHours() < 14) {
    const h = config.routines.find((r) => r.id === "holidays");
    if (h) return h;
  }
  const period = now.getHours() < 14 ? "morning" : "evening";
  return config.routines.find((r) => r.period === period && r.days.includes(now.getDay()))
    || config.routines.find((r) => r.period === period)
    || config.routines[0];
}

export function minutesUntil(deadline, now) {
  const [h, m] = deadline.split(":").map(Number);
  const target = new Date(now);
  target.setHours(h, m, 0, 0);
  return (target - now) / 60000;
}

// Marque d'une étape : horodatage positif = cochée, négatif = décochée.
export const isDone = (done, id) => done[id] > 0 || done[id] === true;
export const mark = (previous, checked, now = Date.now()) =>
  (checked ? 1 : -1) * Math.max(now, Math.abs(Number(previous) || 0) + 1);

// Minuteur d'étape, entièrement déduit des marques (donc synchronisé sans rien ajouter) :
// « _go:<étape> » = « C'est parti » ou « je fais celle-là » ;
// « _at:<étape>:HH:MM » = heure fixe décalée pour aujourd'hui (la plus récente gagne).
export const goKey = (id) => "_go:" + id;
const stamp = (t) => (Math.abs(t) > 1e12 ? Math.abs(t) : 0);

const atTime = (deadline, now) => new Date(now.getTime() + minutesUntil(deadline, now) * 60000).getTime();

// Heure prévue d'une étape à heure fixe, décalage du jour compris.
export function stepTime(step, done) {
  if (!step.at) return "";
  const prefix = `_at:${step.id}:`;
  const shifted = Object.keys(done).filter((k) => k.startsWith(prefix) && done[k] > 0).sort((a, b) => done[b] - done[a])[0];
  return shifted ? shifted.slice(prefix.length) : step.at;
}
// Décale une étape à heure fixe. Pour retarder une heure déjà passée, on repart de maintenant.
export function shiftStep(step, done, minutes, now) {
  const [h, m] = stepTime(step, done).split(":").map(Number);
  let base = h * 60 + m;
  if (minutes > 0) base = Math.max(base, now.getHours() * 60 + now.getMinutes());
  const total = Math.min(23 * 60 + 59, Math.max(0, base + minutes));
  const key = `_at:${step.id}:${String(Math.floor(total / 60)).padStart(2, "0")}:${String(total % 60).padStart(2, "0")}`;
  const latest = Math.max(0, ...Object.keys(done).filter((k) => k.startsWith(`_at:${step.id}:`)).map((k) => stamp(done[k])));
  done[key] = mark(latest, true, now.getTime());
}

// state : empty | done | passed | late | tight | ontime | early
// La marge (slack, en minutes, négative = retard) vise le prochain repère : une étape à heure
// fixe (le repas) puis l'heure de fin. Elle reste stable tant que l'étape en cours tient dans sa
// durée, fond si elle déborde, et remonte quand on finit plus tôt. Une étape « temps libre » n'a
// pas de durée : elle dure ce qui reste avant le repère.
export function evaluate(routine, done, now) {
  const steps = routine.steps;
  const required = (s) => !s.optional && !s.free;
  const pending = steps.filter((s) => !isDone(done, s.id));
  const went = (s) => (done[goKey(s.id)] > 1e12 ? done[goKey(s.id)] : 0);
  const due = (s) => minutesUntil(stepTime(s, done), now) <= 0;
  // Une étape à heure fixe a commencé si on l'a lancée, ou si c'est l'heure et que tout est fait avant.
  const begun = (s) => !!went(s) || (due(s) && !steps.slice(0, steps.indexOf(s)).some((x) => required(x) && !isDone(done, x.id)));
  const anchor = pending.find((s) => s.at && !begun(s)) || null;
  const segment = anchor ? pending.filter((s) => steps.indexOf(s) < steps.indexOf(anchor)) : pending;
  const target = anchor ? { label: anchor.label, time: stepTime(anchor, done), step: anchor }
    : { label: routine.deadlineLabel, time: routine.deadline, step: null };

  // Le temps libre est périmé quand l'heure du repère qui le suit est passée.
  const expired = (s) => {
    const next = steps.slice(steps.indexOf(s) + 1).find((x) => x.at);
    return next ? isDone(done, next.id) || due(next) : minutesUntil(routine.deadline, now) <= 0;
  };
  const usable = pending.filter((s) => !(s.free && expired(s)));
  const focus = usable.filter(went).sort((a, b) => went(b) - went(a))[0];
  const step = focus || usable.find((s) => !s.optional || s.free) || usable[0] || null;

  // Départ : la dernière action sur cette routine (étape validée ou « C'est parti »).
  let since = Math.max(0, ...steps.flatMap((s) => [stamp(done[s.id]), stamp(done[goKey(s.id)])]));
  let started = steps.some((s) => done[s.id] > 1e12) || steps.some(went);
  const waiting = !!step?.at && !begun(step);
  if (step?.at && !waiting && !went(step)) { // commencée à l'heure dite
    since = Math.max(since, atTime(stepTime(step, done), now));
    started = true;
  }
  started = !!step && started && !waiting;
  const elapsed = started ? Math.max(0, (now.getTime() - since) / 60000) : 0;

  const spent = started && required(step) && segment.includes(step) ? Math.min(elapsed, step.minutes) : 0;
  const remaining = segment.filter(required).reduce((sum, s) => sum + s.minutes, 0) - spent;
  const left = minutesUntil(target.time, now);
  const finished = steps.length > 0 && !pending.some(required);
  let slack = left - remaining;
  const timer = { step, started, elapsed, free: !!step?.free, available: Math.max(0, slack),
    waiting, wait: waiting ? Math.max(0, minutesUntil(stepTime(step, done), now)) : 0 };
  if (finished) {
    // Terminé : on fige l'avance (ou le retard) au moment de la dernière étape obligatoire.
    const last = Math.max(0, ...steps.filter(required).map((s) => stamp(done[s.id])));
    if (last) slack = minutesUntil(routine.deadline, new Date(last));
  }
  let state;
  if (!steps.length) state = "empty";
  else if (finished) state = "done";
  else if (left <= 0) state = "passed";
  else if (slack < 0) state = "late";
  else if (slack < 5) state = "tight";
  else if (slack < 15) state = "ontime";
  else state = "early";
  const losing = !finished && started && !timer.free && (step.optional || elapsed > step.minutes);
  return { state, remaining, left, slack, losing, timer, target, doneCount: steps.length - pending.length, total: steps.length, next: step };
}

// Jauge « pile poil » : 50 % = à l'heure prévue, à droite l'avance, à gauche le retard.
export const GAUGE_SPAN = 15; // minutes de chaque côté
export function gauge(slack) {
  const minutes = Math.floor(Math.abs(slack));
  return {
    position: 50 + (50 * Math.max(-GAUGE_SPAN, Math.min(GAUGE_SPAN, slack))) / GAUGE_SPAN,
    tone: slack >= 5 ? "good" : slack > -1 ? "warn" : "late",
    label: minutes < 1 ? "Pile poil" : `${formatDuration(minutes)} ${slack > 0 ? "d'avance" : "de retard"}`,
  };
}

// Compte à rebours « m:ss » de l'étape ; « +m:ss » quand elle déborde.
export function formatClock(minutes) {
  const over = minutes < 0;
  const s = over ? Math.floor(-minutes * 60) : Math.ceil(minutes * 60);
  return `${over && s ? "+" : ""}${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

export function formatDuration(minutes) {
  const m = Math.max(0, Math.floor(minutes));
  return m < 60 ? `${m} min` : `${Math.floor(m / 60)} h ${String(m % 60).padStart(2, "0")}`;
}
