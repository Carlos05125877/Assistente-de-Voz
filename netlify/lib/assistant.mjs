// Listas, datas especiais, rotinas e lembretes. Lembretes/alarmes/timers viram ACOES para o ESP32.
import { getJSON, update, logEvent } from "./store.mjs";
import { norm, joinPt, uid, zoned, resolveDate, resolveTime, isoDate, localToEpoch, spokenDate, spokenTime, MONTHS, addDays } from "./util.mjs";
import { remindersOf } from "./device.mjs";

// ---------------------------------------------------------------- listas
const listKey = (n) => norm(n || "compras").replace(/^(a |de |da |do )?lista( de| da| do)?\s*/, "").trim() || "compras";
const splitItems = (s) => String(s || "").split(/,|;|\s+e\s+|\n/).map((x) => x.replace(/\s+/g, " ").trim()).filter(Boolean);

export async function listAdd(name, items) {
  const key = listKey(name), add = splitItems(items);
  if (!add.length) return { spoken: "O que devo adicionar na lista?" };
  const added = await update("lists", {}, (L) => {
    const lst = (L[key] ||= []);
    const have = new Set(lst.filter((i) => !i.done).map((i) => norm(i.text)));
    const fresh = add.filter((x) => !have.has(norm(x)));
    lst.push(...fresh.map((text) => ({ text, done: false })));
    return fresh;
  });
  return { spoken: added.length ? `Adicionei ${joinPt(added)} na lista de ${key}.` : `Isso já está na lista de ${key}.` };
}
export async function listRead(name) {
  const key = listKey(name);
  const items = ((await getJSON("lists", {}))[key] || []).filter((i) => !i.done).map((i) => i.text);
  return { spoken: items.length ? `Na lista de ${key} tem ${items.length} ite${items.length > 1 ? "ns" : "m"}: ${joinPt(items)}.` : `A lista de ${key} está vazia.`, items };
}
export async function listRemove(name, items) {
  const key = listKey(name), targets = splitItems(items).map(norm);
  const removed = await update("lists", {}, (L) => {
    const lst = (L[key] ||= []), out = [];
    L[key] = lst.filter((i) => (targets.some((t) => norm(i.text) === t || norm(i.text).includes(t)) ? (out.push(i.text), false) : true));
    return out;
  });
  return { spoken: removed.length ? `Tirei ${joinPt(removed)} da lista de ${key}.` : `Não encontrei ${items} na lista de ${key}.` };
}
export async function listClear(name) {
  const key = listKey(name);
  await update("lists", {}, (L) => { L[key] = []; });
  return { spoken: `Lista de ${key} limpa.` };
}

// ---------------------------------------------------------------- lembretes (executados pelo ESP32)
const REP = { none: "", daily: ", todo dia", weekdays: ", de segunda a sexta", weekly: ", toda semana" };
export function reminderAction(text, date = "", time = "", repeat = "none", minutes) {
  text = String(text || "").replace(/\s+/g, " ").trim().slice(0, 110);
  if (!text) return { spoken: "Sobre o que devo te lembrar?" };
  const now = zoned();
  let d, t;
  if (minutes !== undefined && minutes !== "" && !Number.isNaN(+minutes)) {
    const at = Math.floor(Date.now() / 1000 + +minutes * 60);
    return { spoken: `Certo. Daqui a ${+minutes} minutos eu te lembro de ${text}.`, actions: [{ t: "rem_add", at, rep: "none", x: text }] };
  }
  d = resolveDate(date, now);
  t = (time && resolveTime(time)) || "09:00";
  if (!d) return { spoken: `Não entendi a data ${date}.` };
  let at = localToEpoch(d.y, d.mo, d.d, +t.slice(0, 2), +t.slice(3));
  if (at <= Date.now() / 1000) {
    if (isoDate(d) !== isoDate(now)) return { spoken: "Essa data já passou." };
    d = addDays(d, 1);
    at = localToEpoch(d.y, d.mo, d.d, +t.slice(0, 2), +t.slice(3));
  }
  const rep = REP[repeat] !== undefined ? repeat : "none";
  return { spoken: `Certo. Vou te lembrar de ${text} ${spokenDate(d, now)} às ${spokenTime(t)}${REP[rep]}.`, actions: [{ t: "rem_add", at, rep, x: text }] };
}
export function remindersSpoken(st) {
  const rs = remindersOf(st).sort((a, b) => a.at - b.at);
  if (!rs.length) return "Você não tem lembretes agendados.";
  return `Você tem ${rs.length} lembrete${rs.length > 1 ? "s" : ""}: ` + rs.slice(0, 8).map((r) => {
    const z = zoned(new Date(r.at * 1000));
    return `${r.text}, ${spokenDate(z)} às ${spokenTime(`${String(z.h).padStart(2, "0")}:${String(z.mi).padStart(2, "0")}`)}`;
  }).join("; ") + ".";
}
export function reminderRemoveAction(st, query) {
  const q = norm(query);
  const hit = remindersOf(st).find((r) => q && norm(r.text).includes(q));
  return hit ? { spoken: `Lembrete ${hit.text} removido.`, actions: [{ t: "rem_del", id: hit.id }] } : { spoken: `Não encontrei o lembrete ${query}.` };
}

// ---------------------------------------------------------------- datas especiais
function nextOccurrence(e, today) {
  const m = String(e.date).match(/^(?:(\d{4})-)?(\d{1,2})-(\d{1,2})$/);
  if (!m) return null;
  if (!e.yearly && m[1]) { const d = { y: +m[1], mo: +m[2], d: +m[3] }; return isoDate(d) >= isoDate(today) ? { d, years: null } : null; }
  for (const y of [today.y, today.y + 1]) {
    const d = { y, mo: +m[2], d: +m[3] };
    if (isoDate(d) >= isoDate(today)) return { d, years: m[1] ? y - +m[1] : null };
  }
  return null;
}
export async function datesView() {
  const today = zoned();
  const t0 = Date.UTC(today.y, today.mo - 1, today.d);
  return (await getJSON("dates", [])).map((e) => {
    const n = nextOccurrence(e, today);
    return n && { ...e, next: isoDate(n.d), days_left: Math.round((Date.UTC(n.d.y, n.d.mo - 1, n.d.d) - t0) / 864e5), years: n.years };
  }).filter(Boolean).sort((a, b) => a.days_left - b.days_left);
}
export async function dateAdd(name, when, yearly = true) {
  const d = resolveDate(when);
  if (!name || !d) return { spoken: "Preciso do nome e da data do evento." };
  const withYear = /\d{4}/.test(String(when));
  const date = yearly ? (withYear ? isoDate(d) : `${String(d.mo).padStart(2, "0")}-${String(d.d).padStart(2, "0")}`) : isoDate(d);
  await update("dates", [], (D) => { D.push({ id: uid(), name: String(name).trim(), date, yearly: !!yearly }); });
  return { spoken: `Guardei ${name}: ${d.d} de ${MONTHS[d.mo - 1]}${yearly ? ", todo ano" : ""}.` };
}
export async function dateCountdown(query = "") {
  const q = norm(query);
  let items = await datesView();
  if (q) items = items.filter((e) => norm(e.name).includes(q) || q.includes(norm(e.name)));
  if (!items.length) return { spoken: "Não encontrei essa data. Cadastre no painel ou diga: guarda o aniversário da Maria em 10 de março." };
  const s = items.slice(0, 4).map((e) => {
    const extra = e.years ? `, completando ${e.years} anos` : "";
    return e.days_left === 0 ? `hoje é ${e.name}${extra}` : e.days_left === 1 ? `amanhã é ${e.name}${extra}` : `faltam ${e.days_left} dias para ${e.name}${extra}`;
  }).join("; ");
  return { spoken: s[0].toUpperCase() + s.slice(1) + "." };
}

// ---------------------------------------------------------------- rotinas
export const DEFAULT_ROUTINES = [
  { id: "bomdia", name: "Bom dia", steps_text: "som\nbriefing", time: "", days: [0, 1, 2, 3, 4, 5, 6] },
  { id: "boanoite", name: "Boa noite", steps_text: "falar: Boa noite, senhor. Vou pausar a música e baixar o volume.\nspotify: pause\nvolume: 30\ncomando: desliga a luz da sala", time: "", days: [0, 1, 2, 3, 4, 5, 6] },
  { id: "foco", name: "Foco", steps_text: "falar: Modo foco ativado. Vinte e cinco minutos de concentração.\nmusica: lofi para estudar\ntimer: 25 foco", time: "", days: [0, 1, 2, 3, 4] },
];
const STEP = { falar: "speak", dizer: "speak", comando: "ask", volume: "volume", musica: "music", spotify: "spotify", briefing: "briefing",
  biblia: "bible", som: "tone", lista: "list", timer: "timer" };
export function parseSteps(text) {
  return String(text || "").split("\n").map((l) => l.trim()).filter((l) => l && !l.startsWith("#")).map((l) => {
    const m = l.match(/^([A-Za-zÀ-ÿ]+)\s*:?\s*(.*)$/);
    return m && STEP[norm(m[1])] ? { type: STEP[norm(m[1])], arg: m[2].trim() } : null;
  }).filter(Boolean);
}
export const loadRoutines = async () => (await getJSON("routines", null)) || DEFAULT_ROUTINES;
export function findRoutine(rts, q) {
  const n = norm(q);
  return rts.find((r) => r.id === q) || rts.find((r) => n && (n.includes(norm(r.name)) || norm(r.name).includes(n)));
}
/** Agenda das rotinas para o ESP32 disparar na hora: [[id, h, m, mascaraDias]] */
export async function routineSchedule() {
  return (await loadRoutines()).filter((r) => /^\d\d:\d\d$/.test(r.time || "")).map((r) => [r.id, +r.time.slice(0, 2), +r.time.slice(3), r.days.reduce((m, d) => m | (1 << d), 0)]);
}
export async function logAssistant(action, spoken) { await logEvent("assistente", `${action}: ${String(spoken).slice(0, 150)}`); }
