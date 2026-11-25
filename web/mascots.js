// Interventions des mascottes, séparées de la logique des routines (ADR-009).
const LINES = {
  welcome: [["fox", "content", "Coucou {name} ! On commence par : {next} ?"], ["rabbit", "content", "Salut {name} ! Première mission : {next}."]],
  early: [["rabbit", "content", "Tu es en avance, bravo ! La suite : {next}."], ["fox", "content", "Super rythme ! On continue avec : {next}."]],
  ontime: [["fox", "content", "Tout va bien. Prochaine étape : {next}."], ["rabbit", "content", "Pile dans les temps ! Maintenant : {next}."]],
  tight: [["rabbit", "think", "Hmm, c'est juste… On se concentre sur : {next}."], ["fox", "think", "Pas le moment de rêvasser ! {next}, c'est parti."]],
  late: [["fox", "comfort", "Pas de panique, une chose à la fois : {next}."], ["rabbit", "comfort", "On a un peu de retard, ça arrive. Allez : {next} !"]],
  passed: [["rabbit", "comfort", "L'heure est passée. On termine tranquillement : {next}."]],
  checked: [["fox", "bravo", "Bravo, {step} : c'est fait !"], ["rabbit", "bravo", "Génial ! {step}, terminé !"], ["fox", "bravo", "Et hop, une de plus !"]],
  unchecked: [["rabbit", "think", "D'accord, on reprend : {step}."]],
  done: [["fox", "bravo", "Mission accomplie, {name} ! Tu peux être fier de toi."], ["rabbit", "bravo", "Tout est prêt, champion·ne !"]],
  empty: [["fox", "think", "Cette routine n'a pas encore d'étapes. Direction les réglages !"]],
};

// Activités calmes proposées quand il reste du temps. Jamais d'écran.
export const SPARE_IDEAS = ["construire en Lego 🧱", "jouer au train en bois 🚂", "faire un dessin 🖍️", "lire quelques pages 📚", "finir un puzzle 🧩"];

export function pickLine(situation, values, seed) {
  const list = LINES[situation] || LINES.ontime;
  const [who, mood, text] = list[Math.abs(seed) % list.length];
  return { who, mood, image: `./mascots/${who}-${mood}.webp`, text: text.replace(/\{(\w+)\}/g, (_, k) => values[k] ?? "") };
}
