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

// state : empty | done | passed | late | tight | ontime | early
export function evaluate(routine, done, now) {
  const checked = routine.steps.filter((s) => done[s.id]);
  const pending = routine.steps.filter((s) => !done[s.id]);
  const required = pending.filter((s) => !s.optional);
  const remaining = required.reduce((sum, s) => sum + s.minutes, 0);
  const left = minutesUntil(routine.deadline, now);
  const slack = left - remaining;
  let state;
  if (!routine.steps.length) state = "empty";
  else if (!required.length) state = "done";
  else if (left <= 0) state = "passed";
  else if (slack < 0) state = "late";
  else if (slack < 5) state = "tight";
  else if (slack < 15) state = "ontime";
  else state = "early";
  return { state, remaining, left, slack, doneCount: checked.length, total: routine.steps.length, next: required[0] || pending[0] || null };
}

export function formatDuration(minutes) {
  const m = Math.max(0, Math.floor(minutes));
  return m < 60 ? `${m} min` : `${Math.floor(m / 60)} h ${String(m % 60).padStart(2, "0")}`;
}
