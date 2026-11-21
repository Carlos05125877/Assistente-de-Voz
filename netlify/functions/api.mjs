// API do painel + sync do ESP32. Uma unica funcao (menos cold starts) que so roda quando alguem chama.
import * as auth from "../lib/auth.mjs";
import { getJSON, setJSON, update, logEvent } from "../lib/store.mjs";
import { splitForTts, zoned, isoDate } from "../lib/util.mjs";
import { VOICES } from "../lib/tts.mjs";
import { getWeather, getNews, getQuotes, wikiSummary, newsSpoken, quotesSpoken } from "../lib/info.mjs";
import { spotify, spotifyCfg, spotifyExchange } from "../lib/spotify.mjs";
import { smartDevices, smartControl } from "../lib/tuya.mjs";
import * as YV from "../lib/youversion.mjs";
import * as A from "../lib/assistant.mjs";
import { runTurn, runRoutine, briefing, loadSettings } from "../lib/agent.mjs";
import { loadDevice, queue, sayOnDevice, saveDeviceState, isOnline, alarmsOf, remindersOf, timersOf } from "../lib/device.mjs";

const T0 = Date.now();
const json = (d, status = 200, headers = {}) => new Response(JSON.stringify(d), { status, headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store", ...headers } });
const redirect = (to, headers = {}) => new Response(null, { status: 302, headers: { Location: to, ...headers } });
const SECRET_KEYS = ["groq_api_key", "gemini_api_key", "gemini_model", "stt_model", "tuya_access_id", "tuya_access_secret", "tuya_host", "smart_webhook_url", "spotify_client_id", "spotify_client_secret"];
const PLAIN_SECRETS = new Set(["gemini_model", "stt_model", "tuya_host", "smart_webhook_url", "spotify_client_id"]);
const bumpCfg = () => setJSON("cfg_v", { v: Date.now() });
const remember = (key, text) => setJSON(`spoken/${key}`, String(text || ""));
async function speak(text) { await remember("last", text); await sayOnDevice(text, splitForTts); }

// ------------------------------------------------------------------ ESP32
async function deviceSync(req) {
  const body = await req.json().catch(() => ({}));
  const { queue: q, hot } = await saveDeviceState(body.st || {}, true);
  const cv = (await getJSON("cfg_v", { v: 1 })).v;
  const out = { q, hot, next: hot ? 10 : 180, t: Math.floor(Date.now() / 1000) };
  if (+body.st?.cv !== cv) {
    const sec = await getJSON("secrets", {});
    out.cfg = { v: cv, gk: sec.groq_api_key || process.env.GROQ_API_KEY || "", sm: sec.stt_model || "whisper-large-v3-turbo", rt: await A.routineSchedule() };
  }
  return json(out);
}

// ------------------------------------------------------------------ painel: estado
async function state(url) {
  const [dev, yv, settings, sec, sp, devices, convs, ev] = await Promise.all([
    loadDevice(), YV.loadYv(), loadSettings(), getJSON("secrets", {}), spotifyCfg(), smartDevices(), getJSON("convs", []), getJSON("events", { items: [] })]);
  if ((dev.hot_until || 0) < Date.now() + 40_000) await update("device", {}, (d) => { d.hot_until = Date.now() + 75_000; });
  const st = dev.st || {}, ago = dev.last_seen ? Math.round((Date.now() - dev.last_seen) / 1000) : null;
  const after = +(url.searchParams.get("after") || 0);
  const secrets = Object.fromEntries(SECRET_KEYS.map((k) => {
    const v = String(sec[k] || "");
    return [k, { set: !!v, value: PLAIN_SECRETS.has(k) ? v : v ? "…" + v.slice(-4) : "" }];
  }));
  return json({
    device: { online: isOnline(dev), last_seen_ago: ago, ip: st.ip, uptime: st.up, heap: st.heap, min_heap: st.minh, rssi: st.rssi, fw: st.fw, busy: !!st.busy,
      queue: (dev.queue || []).length, volume: st.vol },
    alarms: alarmsOf(st), timers: timersOf(st).map((t) => ({ ...t, left: Math.max(0, t.left - (ago || 0)) })),
    notes: ((await getJSON("lists", {})).notas || []).filter((i) => !i.done).map((i) => i.text),
    youversion: { has_token: !!yv.access_token, logged_in: !!yv.logged_in, email: yv.email, user: yv.full_name || yv.username, auth_method: yv.auth_method, auth_error: yv.auth_error,
      last_sync: yv.last_sync ? new Date(yv.last_sync).toLocaleString("pt-BR", { timeZone: "America/Sao_Paulo" }) : "", sync_log: yv.sync_log, plans: yv.user_plans,
      active_plan_id: yv.active_plan_id, active_plan_title: yv.active_plan_title, current_day: yv.current_day, total_days: yv.total_days, version: yv.version || "NVI" },
    spotify: { connected: !!sp.refresh_token, client_id: sp.client_id, redirect_uri: `${url.origin}/spotify/callback` },
    conversations: convs.slice(-25).map((c) => ({ ...c, time: new Date(c.ts).toLocaleTimeString("pt-BR", { timeZone: "America/Sao_Paulo", hour: "2-digit", minute: "2-digit" }) })),
    settings, secrets, smart_devices: devices, voices: VOICES,
    events: ev.items.filter((e) => e.id > after).map((e) => ({ ...e, time: new Date(e.ts).toLocaleTimeString("pt-BR", { timeZone: "America/Sao_Paulo" }) })),
    server: { time: new Date().toLocaleString("sv-SE", { timeZone: "America/Sao_Paulo" }), uptime: Math.round((Date.now() - T0) / 1000), configured: auth.configured() },
  });
}

// ------------------------------------------------------------------ painel: comandos para o ESP32
async function espSend(c) {
  const st = (await loadDevice()).st || {};
  switch (c.type) {
    case "mic": await queue({ t: "listen" }); break;
    case "test": await queue({ t: "tone", m: c.mode || "chime" }); break;
    case "restart": await queue({ t: "restart" }); break;
    case "volume": await queue({ t: "vol", v: Math.max(0, Math.min(100, +c.v || 0)) }); break;
    case "speak": await speak(c.text); break;
    case "say_key": await sayOnDevice((await getJSON(`spoken/${c.key === "preview" ? "preview" : "last"}`, "")) || c.text || "", splitForTts); break;
    case "briefing": await speak(await briefing(st)); break;
    case "bible": await speak((await YV.bible({ action: c.action })).spoken); break;
    case "ask": { const r = await runTurn(c.text, st); if (r.actions.length) await queue(...r.actions); await speak(r.reply); break; }
    case "timer": await queue({ t: "timer", s: Math.max(1, +c.sec || 60), l: String(c.label || "timer").slice(0, 30) }); break;
    case "alarm_add": await queue({ t: "alarm_add", h: +c.h, m: +c.m, r: c.r === false ? 0 : 1, l: String(c.label || "Despertador").slice(0, 40) }); break;
    case "alarm_remove": await queue({ t: "alarm_del", h: +c.h, m: +c.m }); break;
    case "alarm_clear": await queue({ t: "alarm_clear" }); break;
    case "note_add": await A.listAdd("notas", c.text); break;
    case "notes_clear": await A.listClear("notas"); break;
    default: return json({ ok: false, error: `Comando ${c.type} desconhecido` }, 400);
  }
  await logEvent("painel", `Comando ${c.type} enviado ao ESP32`);
  return json({ ok: true, delivered: "queue" });
}

// ------------------------------------------------------------------ painel: assistente
async function assistantData() {
  const st = (await loadDevice()).st || {};
  const reminders = remindersOf(st).sort((a, b) => a.at - b.at).map((r) => {
    const z = zoned(new Date(r.at * 1000));
    return { ...r, date: isoDate(z), time: `${String(z.h).padStart(2, "0")}:${String(z.mi).padStart(2, "0")}` };
  });
  return json({ reminders, lists: await getJSON("lists", {}), routines: await A.loadRoutines(), dates: await A.datesView() });
}
async function assistantOp(b) {
  const st = (await loadDevice()).st || {};
  const lists = (fn) => update("lists", {}, fn);
  switch (b.op) {
    case "reminder_add": { const r = A.reminderAction(b.text, b.date, b.time, b.repeat); if (r.actions) await queue(...r.actions); return json({ ok: !!r.actions, spoken: r.spoken }); }
    case "reminder_remove": await queue({ t: "rem_del", id: b.id }); return json({ ok: true, spoken: "Lembrete removido (o ESP32 confirma no próximo sync)." });
    case "list_add": return json({ ok: true, ...(await A.listAdd(b.list, b.items)) });
    case "list_toggle": await lists((L) => { const i = L[b.list]?.[b.index]; if (i) i.done = !i.done; }); return json({ ok: true });
    case "list_remove_index": await lists((L) => { L[b.list]?.splice(b.index, 1); }); return json({ ok: true });
    case "list_clear_done": await lists((L) => { L[b.list] = (L[b.list] || []).filter((i) => !i.done); }); return json({ ok: true, spoken: "Itens marcados removidos." });
    case "list_delete": await lists((L) => { delete L[b.list]; }); return json({ ok: true, spoken: `Lista ${b.list} apagada.` });
    case "date_add": return json({ ok: true, ...(await A.dateAdd(b.name, b.date, b.yearly !== false)) });
    case "date_remove": await update("dates", [], (D) => { const i = D.findIndex((d) => d.id === b.id); if (i >= 0) D.splice(i, 1); }); return json({ ok: true });
    case "routine_save": {
      if (!String(b.name || "").trim()) return json({ ok: false, error: "Dê um nome para a rotina" });
      const steps = A.parseSteps(b.steps_text);
      const r = { id: b.id || String(b.name).toLowerCase().normalize("NFD").replace(/[^a-z0-9]+/g, "").slice(0, 16) || Date.now().toString(36),
        name: String(b.name).trim(), steps_text: String(b.steps_text || ""), time: /^\d\d:\d\d$/.test(b.time || "") ? b.time : "", days: Array.isArray(b.days) ? b.days.map(Number) : [0, 1, 2, 3, 4, 5, 6] };
      const all = await A.loadRoutines();
      const i = all.findIndex((x) => x.id === r.id);
      i >= 0 ? (all[i] = { ...all[i], ...r }) : all.push(r);
      await setJSON("routines", all); await bumpCfg();
      return json({ ok: true, steps });
    }
    case "routine_delete": await setJSON("routines", (await A.loadRoutines()).filter((r) => r.id !== b.id)); await bumpCfg(); return json({ ok: true });
    case "routine_run": {
      const all = await A.loadRoutines(), r = all.find((x) => x.id === b.id);
      if (!r) return json({ ok: false, error: "Rotina não encontrada" });
      const out = await runRoutine(r, { st });
      if (out.actions.length) await queue(...out.actions);
      await speak(out.spoken);
      r.last_run = new Date().toISOString().slice(0, 16); await setJSON("routines", all);
      return json({ ok: true, spoken: `Rotina ${r.name} enviada ao Jarvis.` });
    }
  }
  return json({ ok: false, error: `Operação ${b.op} desconhecida` }, 400);
}
async function assistantSpeak(p) {
  const a = p.get("action");
  const spoken = a === "news" ? await newsSpoken(p.get("topic") || (await loadSettings()).news_topic)
    : a === "quotes" ? await quotesSpoken()
    : a === "list_read" ? (await A.listRead(p.get("list_name"))).spoken
    : a === "wiki" ? (await wikiSummary(p.get("query"))).spoken : "";
  await remember("last", spoken);
  return json({ ok: !!spoken, spoken });
}

// ------------------------------------------------------------------ paginas pequenas
const LOGIN_HTML = (err = "") => `<!DOCTYPE html><html lang="pt-BR"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>J.A.R.V.I.S. — Entrar</title>
<style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#060a12;color:#e3eefc;font:15px system-ui,sans-serif}form{background:#0b1321;border:1px solid #1a2a42;border-radius:14px;padding:28px;width:300px}
h1{margin:0 0 18px;color:#34e4ff;letter-spacing:4px;font-size:20px;text-align:center}input,button{width:100%;box-sizing:border-box;padding:11px;border-radius:9px;font:inherit;margin-top:8px}
input{background:#070d18;border:1px solid #223652;color:#e3eefc}button{background:#34e4ff;border:0;color:#03121c;font-weight:700;cursor:pointer}.e{color:#ff8fa0;font-size:13px;margin-top:10px}</style></head>
<body><form method="post" action="/login"><h1>J.A.R.V.I.S.</h1><input type="password" name="password" placeholder="Senha do painel" autofocus required><button>Entrar</button>${err ? `<div class="e">${err}</div>` : ""}</form></body></html>`;
const html = (s, status = 200, headers = {}) => new Response(s, { status, headers: { "Content-Type": "text/html; charset=utf-8", ...headers } });
const form = async (req) => Object.fromEntries(new URLSearchParams(await req.text()));

// ------------------------------------------------------------------ roteador
export default async (req) => {
  const url = new URL(req.url), path = url.pathname.replace(/\/+$/, "") || "/", p = url.searchParams, M = req.method;
  try {
    if (path === "/api/sync") return auth.isDevice(req) ? deviceSync(req) : json({ error: "token do dispositivo inválido" }, 401);
    if (path === "/login") {
      if (M === "GET") return html(LOGIN_HTML(auth.configured().panel ? "" : "Defina PANEL_PASSWORD nas variáveis de ambiente do Netlify."));
      const f = await form(req);
      return auth.checkPassword(f.password || "") ? redirect("/", { "Set-Cookie": auth.loginCookie() }) : html(LOGIN_HTML("Senha incorreta."), 401);
    }
    if (path === "/logout") return redirect("/login", { "Set-Cookie": auth.logoutCookie() });
    if (path === "/spotify/callback" && p.get("code")) {
      const r = await spotifyExchange(p.get("code"), `${url.origin}/spotify/callback`);
      await logEvent("spotify", r.ok ? "Spotify conectado" : `Falha OAuth: ${r.error}`, r.ok ? "info" : "error");
      return redirect(r.ok ? "/?spotify=ok#spotify" : "/#spotify");
    }
    if (!auth.isPanel(req)) return path.startsWith("/api/") ? json({ error: "login necessário" }, 401) : redirect("/login");

    // ---- painel
    if (path === "/api/state") return state(url);
    if (path === "/api/events") { const ev = await getJSON("events", { items: [] }); return json({ events: ev.items.filter((e) => e.id > +(p.get("after") || 0)) }); }
    if (path === "/api/esp/send") return espSend(await req.json());
    if (path === "/api/ask") {
      const b = await req.json(), st = (await loadDevice()).st || {};
      const r = await runTurn(b.text, st);
      if (r.actions.length) await queue(...r.actions);
      if (b.speak) await speak(r.reply); else await remember("last", r.reply);
      return json({ ok: true, reply: r.reply, actions: r.actions });
    }
    if (path === "/api/settings" && M === "POST") { const b = await req.json(); await update("settings", {}, (s) => Object.assign(s, b)); await bumpCfg(); await logEvent("config", "Configurações salvas"); return json({ ok: true }); }
    if (path === "/api/secrets" && M === "POST") {
      const b = await req.json();
      await update("secrets", {}, (s) => { for (const k of SECRET_KEYS) if (typeof b[k] === "string" && b[k].trim()) s[k] = b[k].trim(); });
      await bumpCfg(); await logEvent("config", "Chaves de API atualizadas");
      return json({ ok: true });
    }
    if (path === "/api/briefing") { const s = await briefing((await loadDevice()).st || {}); await remember("last", s); return json({ ok: true, spoken: s }); }
    if (path === "/api/weather") return json(await getWeather());
    if (path === "/api/news") return json(await getNews(p.get("topic") || "", 8));
    if (path === "/api/quotes") return json(await getQuotes());
    if (path === "/api/wiki") return json(await wikiSummary(p.get("q")));
    if (path === "/api/spotify") return json(await spotify({ action: p.get("action") || "now_playing", q: p.get("q") || "", type: p.get("type") || "track", volume: p.get("volume") ?? undefined }));
    if (path === "/api/smart/devices" && M === "POST") { const b = await req.json(); await setJSON("smart_devices", b.devices || []); return json({ ok: true, devices: b.devices || [] }); }
    if (path === "/api/smart/control") { const b = await req.json(); return json({ ok: true, message: await smartControl(b.name, b.state, b.brightness ?? -1, b.color || "") }); }
    if (path === "/api/assistant/data") return assistantData();
    if (path === "/api/assistant/op") return assistantOp(await req.json());
    if (path === "/api/assistant") return assistantSpeak(p);
    if (path === "/api/bible/youversion") {
      const r = await YV.bible({ action: p.get("action") || "read_plan_day", plan: p.get("plan") || "", ref: p.get("ref") || "", day: +(p.get("day") || 0), version: p.get("version") || "" });
      await remember("last", r.spoken);
      return json(r);
    }
    if (path === "/api/bible/preview") {
      const yv = await YV.loadYv();
      const r = await YV.preview(p.get("plan") || yv.active_plan_id, Math.max(1, +(p.get("day") || yv.current_day || 1)));
      if (r.ok) await remember("preview", r.spoken);
      return json(r);
    }

    // ---- YouVersion (formularios HTML)
    if (path === "/youversion/login" && M === "POST") {
      const f = await form(req);
      if (!f.username || !f.password) return redirect("/?youversion=missing#bible");
      const r = await YV.login(f.username, f.password);
      await logEvent("biblia", r.ok ? `Login YouVersion ok (${r.plans_count} planos)` : r.error, r.ok ? "info" : "error");
      return redirect(`/?youversion=${r.ok ? "ok" : "error"}#bible`);
    }
    if (path === "/youversion/session" && M === "POST") {
      const f = await form(req);
      if (!f.access_token) return redirect("/?youversion=missing#bible");
      await YV.setManualToken(f.access_token);
      return redirect("/?youversion=session_ok#bible");
    }
    if (path === "/youversion/sync") { await YV.sync(); return redirect("/?youversion=synced#bible"); }
    if (path === "/youversion/logout") {
      const yv = await YV.loadYv();
      for (const k of ["access_token", "refresh_token", "token_expires_at", "user_id", "username", "full_name", "auth_method"]) delete yv[k];
      Object.assign(yv, { logged_in: false, user_plans: [], auth_error: "" });
      await YV.saveYv(yv);
      return redirect("/?youversion=logout#bible");
    }
    if (path === "/youversion/debug") {
      const yv = await YV.loadYv();
      const { access_token, refresh_token, plan_meta_cache, ...safe } = yv;
      return json({ ...safe, has_access_token: !!access_token, has_refresh_token: !!refresh_token });
    }
    // ---- Spotify OAuth
    if (path === "/spotify/login") {
      if (p.get("client_id")) await update("secrets", {}, (s) => { s.spotify_client_id = p.get("client_id").trim(); if (p.get("client_secret")) s.spotify_client_secret = p.get("client_secret").trim(); });
      const sp = await spotifyCfg();
      if (!sp.client_id) return redirect("/#spotify");
      const scope = "user-read-playback-state user-modify-playback-state user-read-currently-playing";
      return redirect("https://accounts.spotify.com/authorize?" + new URLSearchParams({ response_type: "code", client_id: sp.client_id, scope, redirect_uri: `${url.origin}/spotify/callback` }));
    }
    return json({ error: "rota não encontrada" }, 404);
  } catch (e) {
    console.error(e);
    await logEvent("servidor", `${path}: ${e.message}`, "error");
    return json({ ok: false, error: e.message }, 500);
  }
};

export const config = { path: ["/api/*", "/login", "/logout", "/youversion/*", "/spotify/*"], excludedPath: ["/api/turn", "/api/say", "/api/tts"] };
