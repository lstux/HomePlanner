// Données par défaut. Tout est modifiable dans l'écran « Réglages ».
const step = (id, icon, label, minutes, optional = false) => ({ id, icon, label, minutes, optional });

const morning = () => [
  step("lever", "🛏️", "Se lever", 5),
  step("dej", "🥣", "Petit-déjeuner", 15),
  step("habits", "👕", "S'habiller", 8),
  step("dents", "🪥", "Se brosser les dents", 3),
  step("cheveux", "💇", "Se coiffer", 2, true),
];
const evening = () => [
  step("ranger", "🧸", "Ranger ses jouets", 10),
  step("douche", "🛁", "Douche ou bain", 15),
  step("pyjama", "🌙", "Mettre son pyjama", 4),
  step("dents", "🪥", "Se brosser les dents", 3),
  step("histoire", "📖", "Histoire du soir", 15, true),
];

export function defaultConfig() {
  return {
    version: 1,
    holidays: false,
    members: [{ id: "m1", name: "Champion" }],
    routines: [
      { id: "school-morning", name: "Matin d'école", icon: "🎒", period: "morning", days: [1, 2, 4, 5], deadline: "08:10", deadlineLabel: "Départ",
        steps: [...morning(), step("cartable", "🎒", "Préparer le cartable", 3), step("chaussures", "👟", "Chaussures et manteau", 4)] },
      { id: "weekend-morning", name: "Matin tranquille", icon: "☀️", period: "morning", days: [0, 3, 6], deadline: "10:00", deadlineLabel: "Objectif", steps: morning() },
      { id: "school-evening", name: "Soir d'école", icon: "🌙", period: "evening", days: [0, 1, 2, 3, 4], deadline: "20:30", deadlineLabel: "Au lit",
        steps: [step("devoirs", "✏️", "Devoirs", 15), step("sac", "🎒", "Préparer les affaires de demain", 5), ...evening()] },
      { id: "weekend-evening", name: "Soir de week-end", icon: "✨", period: "evening", days: [5, 6], deadline: "21:15", deadlineLabel: "Au lit", steps: evening() },
      { id: "holidays", name: "Vacances", icon: "🏖️", period: "any", days: [], deadline: "10:30", deadlineLabel: "Objectif", steps: morning() },
    ],
  };
}
