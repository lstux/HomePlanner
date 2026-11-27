// Que montrer à ce moment de la journée ? Logique pure, testée avec Node.
// Mêmes fenêtres que l'écran de la centrale (platform/central/src/life.cpp).
import { suggestRoutine, isDone } from "./routine.js";

export const BEFORE_MIN = 60, AFTER_MIN = 15;
const minutes = (time) => Number(time.slice(0, 2)) * 60 + Number(time.slice(3));
const clock = (date) => date.getHours() * 60 + date.getMinutes();

// Enfants dont la routine est « en cours » : d'une heure avant l'échéance, ou dès la première case
// cochée, jusqu'à 15 min après. Terminée bien avant l'heure : encore 15 min, puis retour à l'accueil.
export function activeMembers(config, progress, now) {
  const routine = suggestRoutine(config, now), deadline = minutes(routine.deadline), at = clock(now);
  if (!routine.steps.length || at > deadline + AFTER_MIN) return [];
  const window = at >= deadline - BEFORE_MIN;
  return config.members.filter((member) => {
    const done = progress.done[`${member.id}/${routine.id}`] || {};
    const latest = Math.max(0, ...Object.entries(done).filter(([k]) => !k.startsWith("_at:")).map(([, t]) => t));
    if (window) return true;
    if (!latest) return false;
    const finished = routine.steps.every((s) => s.optional || s.free || isDone(done, s.id));
    return !finished || now.getTime() - latest <= AFTER_MIN * 60000;
  }).map((m) => m.id);
}

// Prochaine échéance : { routine, tomorrow }.
export function nextRoutine(config, now) {
  for (const day of [0, 1]) {
    for (const hour of [8, 18]) {
      const date = new Date(now.getFullYear(), now.getMonth(), now.getDate() + day, hour);
      const routine = suggestRoutine(config, date);
      if ((hour < 14) !== (minutes(routine.deadline) < 14 * 60)) continue;
      if (day === 0 && clock(now) > minutes(routine.deadline) + AFTER_MIN) continue;
      return { routine, tomorrow: day === 1 };
    }
  }
  return null;
}

// Accueil : la mascotte suit le moment de la journée. La nuit commence après la routine du soir.
export function greeting(config, now) {
  const at = clock(now);
  const evening = suggestRoutine(config, new Date(now.getFullYear(), now.getMonth(), now.getDate(), 18));
  const bedtime = minutes(evening.deadline) >= 14 * 60 ? minutes(evening.deadline) + AFTER_MIN : 21 * 60;
  if (at >= bedtime || at < 5 * 60) return { night: true, who: "rabbit", mood: "comfort", text: "Chut… c'est la nuit. Fais de beaux rêves 🌙" };
  if (at < 11 * 60) return { night: false, who: "fox", mood: "content", text: "Bonjour ! Belle journée à toute la maison." };
  if (at < 17 * 60) return { night: false, who: "rabbit", mood: "content", text: "Bon après-midi !" };
  return { night: false, who: "fox", mood: "content", text: "Bonne soirée !" };
}
