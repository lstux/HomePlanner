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
// « _go:<étape> » = « C'est parti » ou « je fais celle-là ».
export const goKey = (id) => "_go:" + id;
const stamp = (t) => (Math.abs(t) > 1e12 ? Math.abs(t) : 0);

function stepTimer(routine, done, pending, now) {
  const focus = pending.filter((s) => done[goKey(s.id)] > 1e12).sort((a, b) => done[goKey(b.id)] - done[goKey(a.id)])[0];
  const step = focus || pending.find((s) => !s.optional) || pending[0] || null;
  const started = routine.steps.some((s) => done[s.id] > 1e12)
    || routine.steps.some((s) => done[goKey(s.id)] > 1e12);
  // Départ : la dernière action sur cette routine (étape validée ou « C'est parti »).
  const since = Math.max(0, ...routine.steps.flatMap((s) => [stamp(done[s.id]), stamp(done[goKey(s.id)])]));
  const elapsed = started && step ? Math.max(0, (now.getTime() - since) / 60000) : 0;
  return { step, started: started && !!step, elapsed };
}

// state : empty | done | passed | late | tight | ontime | early
// slack : marge en minutes (négative = retard). Elle reste stable tant que l'étape en cours
// tient dans sa durée, fond si elle déborde, et remonte quand on finit plus tôt.
export function evaluate(routine, done, now) {
  const checked = routine.steps.filter((s) => isDone(done, s.id));
  const pending = routine.steps.filter((s) => !isDone(done, s.id));
  const required = pending.filter((s) => !s.optional);
  const timer = stepTimer(routine, done, pending, now);
  const spent = timer.started && !timer.step.optional ? Math.min(timer.elapsed, timer.step.minutes) : 0;
  const remaining = required.reduce((sum, s) => sum + s.minutes, 0) - spent;
  const left = minutesUntil(routine.deadline, now);
  const finished = routine.steps.length > 0 && !required.length;
  let slack = left - remaining;
  if (finished) {
    // Terminé : on fige l'avance (ou le retard) au moment de la dernière étape obligatoire.
    const last = Math.max(0, ...checked.filter((s) => !s.optional).map((s) => stamp(done[s.id])));
    if (last) slack = minutesUntil(routine.deadline, new Date(last));
  }
  let state;
  if (!routine.steps.length) state = "empty";
  else if (finished) state = "done";
  else if (left <= 0) state = "passed";
  else if (slack < 0) state = "late";
  else if (slack < 5) state = "tight";
  else if (slack < 15) state = "ontime";
  else state = "early";
  const losing = !finished && timer.started && (timer.step.optional || timer.elapsed > timer.step.minutes);
  return { state, remaining, left, slack, losing, timer, doneCount: checked.length, total: routine.steps.length, next: timer.step };
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
