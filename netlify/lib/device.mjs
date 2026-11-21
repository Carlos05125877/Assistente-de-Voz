// Estado do ESP32 e fila de comandos. O ESP32 e o dono de alarmes, lembretes e timers (dispara na hora
// exata, mesmo offline); o Netlify so guarda a ultima copia que o ESP enviou no sync.
import { getJSON, update, setJSON } from "./store.mjs";
import { uid } from "./util.mjs";

const DEF = { st: {}, last_seen: 0, queue: [], hot_until: 0 };
export const loadDevice = () => getJSON("device", DEF);

/** Enfileira comandos para o ESP32 (entregues no proximo sync; com o painel aberto, a cada ~10 s). */
export async function queue(...cmds) {
  await update("device", DEF, (d) => {
    d.queue = [...(d.queue || []), ...cmds.map((c) => ({ ...c, _exp: Date.now() + 15 * 60_000 }))].slice(-20);
  });
}

/** Guarda um texto longo para o ESP32 buscar por partes em /api/say?id=...&p=...
 *  Usa 8 "gavetas" rotativas: o armazenamento nunca cresce e nao precisa de limpeza. */
export async function storeSpeech(parts) {
  const slot = await update("speech_seq", { n: 0 }, (c) => (c.n = (c.n + 1) % 8));
  const id = `${slot}${uid().slice(0, 6)}`;
  await setJSON(`speech/${slot}`, { id, parts, ts: Date.now() });
  return id;
}
export async function loadSpeech(id) {
  const s = await getJSON(`speech/${String(id)[0]}`, null);
  return s && s.id === id ? s.parts : null;
}

/** Fala algo no ESP32 a partir do painel ou de uma rotina. */
export async function sayOnDevice(text, splitFn) {
  const parts = splitFn(text);
  if (!parts.length) return;
  const id = await storeSpeech(parts);
  await queue({ t: "say", id, n: parts.length });
}

export const isOnline = (d) => Date.now() - (d.last_seen || 0) < 12 * 60_000;
export const alarmsOf = (st) => (st?.al || []).map(([h, m, r, l]) => ({ h, m, r: !!r, l: l || "Despertador" }));
export const remindersOf = (st) => (st?.rem || []).map(([id, at, rep, x]) => ({ id, at, repeat: rep || "none", text: x || "" }));
export const timersOf = (st) => (st?.tm || []).map(([l, left]) => ({ label: l, left }));

/** Guarda o estado enviado pelo ESP32 (alarmes, lembretes, timers, RAM, WiFi...). */
export async function saveDeviceState(st, drain = false) {
  return update("device", DEF, (d) => {
    d.st = { ...d.st, ...st };
    d.last_seen = Date.now();
    const now = Date.now();
    const q = (d.queue || []).filter((c) => (c._exp || 0) > now);
    d.queue = drain ? [] : q;
    return { queue: q.map(({ _exp, ...c }) => c), hot: (d.hot_until || 0) > now };
  });
}
