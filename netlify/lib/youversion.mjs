// Conta pessoal do YouVersion (App Biblia): login OAuth2 do app, sync dos planos e leitura do dia.
// APIs nao oficiais (*.youversionapi.com): podem mudar sem aviso; use so na sua conta.
import { getJSON, setJSON, logEvent } from "./store.mjs";
import { fetchJSON, fetchText, htmlToSpeech, decodeEntities, norm } from "./util.mjs";

const APP_ID = Buffer.from("ODViNjFkOTdhNzliOTZiZTQ2NWViYWVlZTgzYjEzMTM=", "base64").toString();
const APP_SECRET = Buffer.from("NzVjZjBlMTQxY2JmNDFlZjQxMGFkY2U1YjY1MzdhNDk=", "base64").toString();
const HDR = { Accept: "application/json", "Accept-Language": "pt-BR,pt;q=0.9", Referer: "http://android.youversionapi.com/",
  "X-YouVersion-App-Platform": "android", "X-YouVersion-App-Version": "17114", "X-YouVersion-Client": "youversion" };
export const VERSIONS = { NVI: 129, ARC: 211, NVT: 193, ARA: 1608 };
const BOOKS = { GEN: "Gênesis", EXO: "Êxodo", LEV: "Levítico", NUM: "Números", DEU: "Deuteronômio", JOS: "Josué", JDG: "Juízes", RUT: "Rute",
  "1SA": "1 Samuel", "2SA": "2 Samuel", "1KI": "1 Reis", "2KI": "2 Reis", "1CH": "1 Crônicas", "2CH": "2 Crônicas", EZR: "Esdras", NEH: "Neemias",
  EST: "Ester", JOB: "Jó", PSA: "Salmos", PRO: "Provérbios", ECC: "Eclesiastes", SNG: "Cânticos", ISA: "Isaías", JER: "Jeremias",
  LAM: "Lamentações", EZK: "Ezequiel", DAN: "Daniel", HOS: "Oséias", JOL: "Joel", AMO: "Amós", OBA: "Obadias", JON: "Jonas", MIC: "Miquéias",
  NAM: "Naum", HAB: "Habacuque", ZEP: "Sofonias", HAG: "Ageu", ZEC: "Zacarias", MAL: "Malaquias", MAT: "Mateus", MRK: "Marcos", LUK: "Lucas",
  JHN: "João", ACT: "Atos", ROM: "Romanos", "1CO": "1 Coríntios", "2CO": "2 Coríntios", GAL: "Gálatas", EPH: "Efésios", PHP: "Filipenses",
  COL: "Colossenses", "1TH": "1 Tessalonicenses", "2TH": "2 Tessalonicenses", "1TI": "1 Timóteo", "2TI": "2 Timóteo", TIT: "Tito", PHM: "Filemom",
  HEB: "Hebreus", JAS: "Tiago", "1PE": "1 Pedro", "2PE": "2 Pedro", "1JN": "1 João", "2JN": "2 João", "3JN": "3 João", JUD: "Judas", REV: "Apocalipse" };
const PT2USFM = Object.fromEntries(Object.entries(BOOKS).map(([k, v]) => [norm(v), k]).concat([["salmo", "PSA"], ["proverbio", "PRO"], ["jo", "JOB"]]));
const DEF = { version: "NVI", user_plans: [], sync_log: [], plan_meta_cache: {}, current_day: 1, total_days: 1, active_plan_title: "nenhum plano ativo" };

export const loadYv = () => getJSON("yv", DEF);
export const saveYv = (yv) => setJSON("yv", yv);
const log = (yv, msg) => { yv.sync_log = [...(yv.sync_log || []), `${new Date().toISOString().slice(11, 19)} ${msg}`].slice(-40); };

const unwrap = (j) => (j && typeof j === "object" && j.response && typeof j.response === "object" ? j.response.data ?? j.response : j);
function deepGet(o, keys, depth = 0) {
  if (!o || typeof o !== "object" || depth > 7) return undefined;
  if (!Array.isArray(o)) for (const k of keys) if (o[k] !== undefined && o[k] !== null && o[k] !== "") return o[k];
  for (const v of Array.isArray(o) ? o.slice(0, 80) : Object.values(o)) {
    if (v && typeof v === "object") { const r = deepGet(v, keys, depth + 1); if (r !== undefined) return r; }
  }
  return undefined;
}
function text(v, depth = 0) {
  if (v === null || v === undefined || typeof v === "boolean" || depth > 6) return "";
  if (typeof v === "number") return String(v);
  if (typeof v === "string") return v.includes("<") ? htmlToSpeech(v) : v.replace(/\s+/g, " ").trim();
  if (Array.isArray(v)) return v.map((x) => text(x, depth + 1)).filter(Boolean).join(" ");
  if (v.l_str) { let s = String(v.l_str).replace(/\{\{\s*(\w+)\s*\}\}/g, (_, k) => text(v.l_args?.[k], depth + 1)); if (s.trim()) return s.trim(); }
  for (const k of ["pt-BR", "pt_BR", "pt", "por", "default", "text", "html", "str", "value", "en"]) if (k in v) { const t = text(v[k], depth + 1); if (t) return t; }
  return "";
}
const planNum = (p) => (String(p ?? "").match(/^\s*(\d+)/) || [])[1] || "";
const claims = (tok) => { try { return JSON.parse(Buffer.from(String(tok).split(".")[1], "base64url").toString()); } catch { return {}; } };

async function api(url, yv, params = {}, auth = true) {
  const qs = new URLSearchParams(Object.entries(params).filter(([, v]) => v !== undefined && v !== null && v !== ""));
  const headers = { ...HDR, ...(auth && yv?.access_token ? { Authorization: `Bearer ${yv.access_token}` } : {}) };
  return fetchJSON(url + (qs.toString() ? "?" + qs : ""), { headers }, 10000);
}

function applyToken(yv, tok, method) {
  yv.access_token = tok.access_token;
  if (tok.refresh_token) yv.refresh_token = tok.refresh_token;
  const c = claims(yv.access_token);
  yv.token_expires_at = (c.exp ? c.exp : Math.floor(Date.now() / 1000) + (tok.expires_in || 3600)) - 60;
  const id = c.user_id || c.uid || c.sub;
  if (id && /^\d+$/.test(String(id))) yv.user_id = String(id);
  Object.assign(yv, { auth_method: method, auth_error: "", logged_in: true });
}

async function tokenRequest(form) {
  const r = await fetchJSON("https://auth.youversionapi.com/token", { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ client_id: APP_ID, client_secret: APP_SECRET, ...form }) }, 15000);
  if (r.status === 200 && r.json?.access_token) return { ok: true, tok: r.json };
  return { ok: false, err: `HTTP ${r.status}: ${r.json?.error_description || r.json?.error || r.text.slice(0, 120)}` };
}

async function fetchProfile(yv) {
  if (!yv.user_id) return;
  const r = await api("https://users.youversionapi.com/3.1/view.json", yv, { id: yv.user_id });
  const d = unwrap(r.json);
  if (r.status === 200 && d) {
    if (d.username) yv.username = String(d.username);
    const name = d.name || [d.first_name, d.last_name].filter(Boolean).join(" ");
    if (name) yv.full_name = name;
  }
}

export async function ensureToken(yv) {
  if (yv.access_token && (!yv.token_expires_at || yv.token_expires_at > Date.now() / 1000 + 30)) return true;
  if (yv.refresh_token) {
    const r = await tokenRequest({ grant_type: "refresh_token", refresh_token: yv.refresh_token });
    if (r.ok) { applyToken(yv, r.tok, yv.auth_method || "oauth_password"); log(yv, "token renovado"); return true; }
    log(yv, `refresh falhou: ${r.err}`);
  }
  return false;
}

export async function login(email, password) {
  const yv = await loadYv();
  yv.email = String(email).trim();
  yv.sync_log = [];
  const r = await tokenRequest({ grant_type: "password", username: yv.email, password });
  if (!r.ok) {
    Object.assign(yv, { auth_error: `Login recusado pelo YouVersion (${r.err}). Conta Google/Apple? Cole o token no painel.`, logged_in: false });
    await saveYv(yv);
    return { ok: false, error: yv.auth_error };
  }
  applyToken(yv, r.tok, "oauth_password");
  await fetchProfile(yv);
  await saveYv(yv);
  const plans = await sync();
  return { ok: true, plans_count: plans.length };
}

export async function setManualToken(token) {
  const yv = await loadYv();
  applyToken(yv, { access_token: String(token).replace(/^bearer\s+/i, "").trim() }, "manual_token");
  await fetchProfile(yv);
  await saveYv(yv);
  return sync();
}

async function moments(yv, kind, pages = 8) {
  const out = [];
  for (let page = 1; page <= pages; page++) {
    const r = await api("https://moments.youversionapi.com/3.1/items.json", yv, { kind, user_id: yv.user_id, page });
    const d = unwrap(r.json);
    const items = Array.isArray(d) ? d : d?.moments;
    if (r.status !== 200 || !items?.length) { if (r.status !== 200) log(yv, `moments ${kind}: HTTP ${r.status}`); break; }
    out.push(...items);
    if (d && "next_page" in d && !d.next_page) break;
  }
  return out;
}
function momentInfo(m) {
  const o = m.object && typeof m.object === "object" ? m.object : m;
  return { pid: planNum(deepGet(o, ["plan_id", "reading_plan_id"]) ?? deepGet(m, ["plan_id"])), sub: String(deepGet(o, ["subscription_id"]) || ""),
    day: +(deepGet(o, ["day", "day_number"]) || 0), created: String(deepGet(o, ["created_dt", "created_at", "updated_dt"]) || deepGet(m, ["created_dt"]) || ""),
    total: +(deepGet(o, ["total_days", "days_count"]) || 0), title: text(deepGet(o, ["plan_title", "plan_name"])) || text(deepGet(o, ["title", "name"])) };
}

async function planMeta(yv, pid) {
  const cache = (yv.plan_meta_cache ||= {});
  if (cache[pid]?.title && cache[pid]?.total_days) return cache[pid];
  const r = await api("https://reading-plans.youversionapi.com/3.1/view.json", yv, { id: pid, language_tag: "pt" });
  const d = unwrap(r.json) || {};
  const meta = { title: text(d.name || d.title), total_days: +(d.total_days || d.days || 0) };
  if (!meta.title) {
    const p = await fetchText(`https://www.bible.com/pt/reading-plans/${pid}`, {}, 8000);
    meta.title = decodeEntities((p.text.match(/property="og:title"\s+content="([^"]+)"/) || [])[1] || `Plano #${pid}`).split("|")[0].trim();
  }
  cache[pid] = meta;
  return meta;
}

export async function sync() {
  const yv = await loadYv();
  if (!(await ensureToken(yv))) { log(yv, "sem token válido: faça login de novo"); await saveYv(yv); return yv.user_plans || []; }
  if (!yv.username) await fetchProfile(yv);
  const plans = new Map();
  for (const m of await moments(yv, "plan_subscription")) {
    const i = momentInfo(m);
    if (i.pid && (!plans.has(i.pid) || i.created > plans.get(i.pid).created))
      plans.set(i.pid, { plan_id: i.pid, subscription_id: i.sub, title: i.title, total_days: i.total, max_done: 0, created: i.created, completed: false });
  }
  if (plans.size) {
    for (const m of await moments(yv, "plan_segment_completion", 15)) {
      const i = momentInfo(m), p = plans.get(i.pid);
      if (p && i.day && (!p.created || !i.created || i.created >= p.created)) p.max_done = Math.max(p.max_done, i.day);
    }
    for (const m of await moments(yv, "plan_completion")) {
      const i = momentInfo(m), p = plans.get(i.pid);
      if (p && (!p.created || !i.created || i.created >= p.created)) p.completed = true;
    }
  }
  const list = [];
  for (const p of plans.values()) {
    const meta = await planMeta(yv, p.plan_id);
    p.title = meta.title || p.title || `Plano #${p.plan_id}`;
    p.total_days = meta.total_days || p.total_days || p.max_done + 1;
    p.current_day = Math.max(1, Math.min(p.max_done + 1, p.total_days));
    list.push(p);
  }
  list.sort((a, b) => (a.completed - b.completed) || String(b.created).localeCompare(String(a.created)));
  yv.user_plans = list;
  yv.last_sync = new Date().toISOString();
  log(yv, `sync: ${list.length} planos`);
  const cur = list.find((p) => p.plan_id === yv.active_plan_id);
  if (cur) setActive(yv, cur, Math.max(+yv.current_day || 1, cur.current_day));
  else if (list.length) setActive(yv, list.find((p) => !p.completed) || list[0]);
  await saveYv(yv);
  await logEvent("biblia", `Sync YouVersion: ${list.length} planos`);
  return list;
}

function setActive(yv, p, day) {
  Object.assign(yv, { active_plan_id: p.plan_id, active_subscription_id: p.subscription_id || "", active_plan_title: p.title,
    total_days: p.total_days || 1, current_day: Math.max(1, Math.min(+(day || p.current_day || 1), p.total_days || 1)) });
}

// ---------------------------------------------------------------- texto biblico
const USFM_RE = /\b([1-4]?[A-Z]{2,3})\.(\d{1,3})(?:\.(\d{1,3}))?(?:-(?:[1-4]?[A-Z]{2,3}\.\d{1,3}\.)?(\d{1,3}))?\b/g;
export function parseUsfm(blob) {
  const out = [];
  for (const m of String(blob || "").matchAll(USFM_RE)) {
    if (!BOOKS[m[1]]) continue;
    const ch = +m[2], v1 = m[3] ? +m[3] : 0, v2 = m[4] ? +m[4] : v1, last = out[out.length - 1];
    if (last && last[0] === m[1] && last[1] === ch && v1 && last[2] && v1 <= last[3] + 1) last[3] = Math.max(last[3], v2);
    else if (!out.some((x) => x.join() === [m[1], ch, v1, v2].join())) out.push([m[1], ch, v1, v2]);
  }
  return out;
}
export const usfmLabel = ([b, ch, v1, v2]) => `${BOOKS[b] || b} ${ch}` + (v1 ? `:${v1}` + (v2 && v2 !== v1 ? `-${v2}` : "") : "");

export function ptRefToUsfm(ref) {
  const m = norm(ref).match(/^([1-3]?\s*[a-z]+)\s+(\d+)(?::(\d+(?:-\d+)?))?$/);
  if (!m) return null;
  const book = PT2USFM[m[1].replace(/\s+/g, " ")];
  return book ? `${book}.${m[2]}${m[3] ? "." + m[3] : ""}` : null;
}

/** HTML do capitulo -> {versiculo: texto}, ignorando notas, rotulos e titulos. */
export function parseChapter(html) {
  const SKIP = new Set(["note", "label", "heading", "fr", "ft", "fq", "xt", "x"]);
  const stack = [], out = {};
  for (const m of String(html).matchAll(/<(\/?)(span|div)\b([^>]*)>|<[^>]+>|([^<]+)/g)) {
    if (m[2]) {
      if (m[1]) { stack.pop(); continue; }
      const classes = ((m[3].match(/class="([^"]*)"/) || [])[1] || "").split(/\s+/);
      const usfm = (m[3].match(/data-usfm="([^"]+)"/) || [])[1];
      const verse = classes.includes("verse") && usfm ? +((usfm.split("+")[0].match(/\.(\d+)$/) || [])[1] || 0) || null : null;
      if (!/\/\s*$/.test(m[3])) stack.push({ classes, verse });
    } else if (m[4] && m[4].trim()) {
      let verse = null, content = false;
      for (const f of stack) { if (f.classes.some((c) => SKIP.has(c))) { verse = null; content = false; break; } if (f.classes.includes("content")) content = true; if (f.verse) verse = f.verse; }
      if (verse && content) out[verse] = (out[verse] || "") + decodeEntities(m[4]);
    }
  }
  for (const k of Object.keys(out)) out[k] = out[k].replace(/\s+/g, " ").trim();
  return out;
}

export async function passagesText(passages, version = "NVI") {
  const ver = VERSIONS[version] ? version : "NVI";
  const out = [];
  for (const p of passages.slice(0, 10)) {
    const [book, ch, v1, v2] = p;
    const r = await api("https://bible.youversionapi.com/3.1/chapter.json", null, { id: VERSIONS[ver], reference: `${book}.${ch}` }, false);
    const content = deepGet(unwrap(r.json), ["content"]);
    const verses = typeof content === "string" ? parseChapter(content) : {};
    const keys = Object.keys(verses).map(Number).filter((k) => !v1 || (k >= v1 && k <= (v2 || v1))).sort((a, b) => a - b);
    let t = keys.map((k) => verses[k]).join(" ");
    if (!t) {
      const usfm = `${book}.${ch}` + (v1 ? `.${v1}` + (v2 && v2 !== v1 ? `-${v2}` : "") : "");
      const page = await fetchText(`https://www.bible.com/pt/bible/${VERSIONS[ver]}/${usfm}.${ver}`, {}, 8000);
      t = decodeEntities((page.text.match(/property="og:description"\s+content="([^"]+)"/) || [])[1] || "");
    }
    if (t) out.push({ label: `${usfmLabel(p)} (${ver})`, text: t });
  }
  return out;
}

export async function readDay(yv, pid, day) {
  await ensureToken(yv);
  const r = await api("https://reading-plans.youversionapi.com/3.1/references.json", yv, { id: pid, day, language_tag: "pt", version_id: VERSIONS[yv.version] || 129 });
  const d = unwrap(r.json);
  if (r.status !== 200 || !d) { log(yv, `plano ${pid} dia ${day}: HTTP ${r.status}`); return null; }
  const passages = parseUsfm(JSON.stringify(deepGet(d, ["references"]) ?? d));
  const devotional = text(d.additional_content || d.content || d.devotional);
  const readings = await passagesText(passages, yv.version);
  if (!devotional && !readings.length) return null;
  return { devotional, readings, reference: readings.map((x) => x.label).join(", ") || `Dia ${day}`, url: `https://www.bible.com/pt/reading-plans/${pid}/day/${day}` };
}

export async function verseOfDay() {
  const p = await fetchText("https://www.bible.com/pt/verse-of-the-day", {}, 8000);
  const desc = decodeEntities((p.text.match(/property="og:description"\s+content="([^"]+)"/) || [])[1] || "");
  const title = decodeEntities((p.text.match(/property="og:title"\s+content="([^"]+)"/) || [])[1] || "");
  return desc ? { reference: title.split(" - ")[0].trim() || "Versículo do dia", text: desc } : { reference: "Salmos 46:1", text: "Deus é o nosso refúgio e fortaleza, socorro bem presente na angústia." };
}

function matchPlan(yv, q) {
  const ups = yv.user_plans || [];
  const n = norm(q), num = planNum(q);
  if (num) { const hit = ups.find((p) => p.plan_id === num); if (hit) return hit; if (num.length <= 2 && ups[+num - 1]) return ups[+num - 1]; }
  const ord = { primeiro: 1, segundo: 2, terceiro: 3, quarto: 4, quinto: 5 };
  for (const [w, i] of Object.entries(ord)) if (n.includes(w) && ups[i - 1]) return ups[i - 1];
  return ups.find((p) => norm(p.title).includes(n)) || ups.find((p) => n.split(/\s+/).filter((w) => w.length > 3).some((w) => norm(p.title).includes(w)));
}

/** Acoes de voz/painel. Retorna {spoken, ...extras}. */
export async function bible({ action = "read_plan_day", plan = "", day = 0, ref = "", version = "" } = {}) {
  const yv = await loadYv();
  if (version && VERSIONS[version.toUpperCase()]) yv.version = version.toUpperCase();
  const done = async (spoken, extra = {}) => { await saveYv(yv); return { ok: true, action, spoken, plan_title: yv.active_plan_title, day: yv.current_day, total_days: yv.total_days, ...extra }; };

  if (action === "sync") { const ps = await sync(); return { ok: true, action, spoken: ps.length ? `Sincronizei ${ps.length} planos da sua conta.` : "Não encontrei planos. Confira o login no painel.", plans: ps }; }
  if (action === "verse_of_day") { const v = await verseOfDay(); return done(`Versículo do dia, ${v.reference}: ${v.text}`, { reference: v.reference, text: v.text }); }
  if (action === "get_passage" && ref) {
    const usfm = ptRefToUsfm(ref);
    const rd = usfm ? await passagesText(parseUsfm(usfm), yv.version) : [];
    if (!rd.length) return done(`Não encontrei a passagem ${ref}.`);
    return done(`${rd[0].label}. ${rd[0].text}`, { reference: rd[0].label, text: rd[0].text });
  }
  if (action === "list_plans") {
    const ups = (yv.user_plans || []).filter((p) => !p.completed);
    if (!ups.length) return done("Você não tem planos sincronizados. Entre na sua conta do YouVersion pelo painel.");
    return done(`Você tem ${ups.length} planos em andamento: ` + ups.slice(0, 8).map((p, i) => `${i + 1}, ${p.title}, dia ${p.current_day} de ${p.total_days}`).join("; ") + `. O ativo é ${yv.active_plan_title}.`, { plans: ups });
  }
  if (action === "start_plan" && plan) {
    const p = matchPlan(yv, plan);
    if (!p) return done(`Não encontrei o plano ${plan} na sua conta.`);
    setActive(yv, p);
  }
  let prefix = "";
  if (action === "next_plan_day") {
    if (yv.current_day >= yv.total_days) return done(`Parabéns! Você chegou ao último dia de ${yv.active_plan_title}.`);
    yv.current_day += 1;
    prefix = `Avançando para o dia ${yv.current_day}. `;
  }
  if (!yv.active_plan_id) return done("Nenhum plano ativo. Entre na sua conta do YouVersion pelo painel e sincronize.");
  const d = +day > 0 ? Math.min(+day, yv.total_days || +day) : yv.current_day;
  yv.current_day = d;
  const r = await readDay(yv, yv.active_plan_id, d);
  if (!r) return done(prefix + `Não consegui baixar o dia ${d} de ${yv.active_plan_title} agora.`);
  const spoken = [prefix + `${yv.active_plan_title}. Dia ${d} de ${yv.total_days}.`, r.devotional, ...r.readings.map((x) => `Leitura de ${x.label}. ${x.text}`)].filter(Boolean).join(" ");
  return done(spoken, { reference: r.reference, devotional: r.devotional, readings: r.readings, url: r.url });
}

export async function preview(pid, day) {
  const yv = await loadYv();
  const p = (yv.user_plans || []).find((x) => x.plan_id === pid) || { title: yv.active_plan_title, total_days: yv.total_days };
  const r = await readDay(yv, pid || yv.active_plan_id, day);
  await saveYv(yv);
  if (!r) return { ok: false, error: "Não consegui baixar esse dia." };
  const spoken = [`${p.title}. Dia ${day}.`, r.devotional, ...r.readings.map((x) => `Leitura de ${x.label}. ${x.text}`)].filter(Boolean).join(" ");
  return { ok: true, title: p.title, day, total_days: p.total_days, ...r, spoken, source: "api" };
}
