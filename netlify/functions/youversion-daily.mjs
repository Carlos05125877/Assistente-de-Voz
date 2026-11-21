// Funcao agendada (gratis no plano free): sincroniza os planos do YouVersion 1x por dia, de madrugada,
// para o devocional da manha ja estar no dia certo sem esperar a API.
import { sync } from "../lib/youversion.mjs";
import { logEvent } from "../lib/store.mjs";

export default async () => {
  try { const plans = await sync(); console.log(`youversion-daily: ${plans.length} planos`); }
  catch (e) { await logEvent("biblia", `Sync diário falhou: ${e.message}`, "error"); }
};
export const config = { schedule: "15 8 * * *" }; // 05:15 em Brasilia (UTC-3)
