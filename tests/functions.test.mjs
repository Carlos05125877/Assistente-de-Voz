// Testes offline das funcoes do Netlify: Blobs, TTS, Gemini e APIs externas sao simulados.
// Rode com: npm test
import test from "node:test";
import assert from "node:assert/strict";

// ---- simuladores
const mem = new Map();
globalThis.__JARVIS_STORE__ = {
  async get(k) { return mem.has(k) ? JSON.parse(mem.get(k)) : null; },
  async setJSON(k, v) { mem.set(k, JSON.stringify(v)); },
  async delete(k) { mem.delete(k); },
};
globalThis.__JARVIS_TTS__ = async function* (text) { yield new TextEncoder().encode(`MP3[${text.length}]`); };
globalThis.__JARVIS_ENV__ = { PANEL_PASSWORD: "segredo", DEVICE_TOKEN: "tok123" };
let geminiReply = null;
const calls = [];
globalThis.fetch = async (url, opts = {}) => {
  url = String(url); calls.push(url);
  const J = (o, s = 200) => new Response(JSON.stringify(o), { status: s, headers: { "Content-Type": "application/json" } });
  if (url.includes("generativelanguage")) return J(geminiReply);
  if (url.includes("auth.youversionapi.com/token")) {
    const f = new URLSearchParams(opts.body);
    if (f.get("password") !== "certa") return J({ error: "invalid_grant", error_description: "Bad credentials" }, 400);
    const payload = Buffer.from(JSON.stringify({ user_id: 42, exp: Math.floor(Date.now() / 1000) + 3600 })).toString("base64url");
    return J({ access_token: `x.${payload}.y`, refresh_token: "r1", expires_in: 3600 });
  }
  if (url.includes("users.youversionapi.com")) return J({ response: { data: { username: "carlos", first_name: "Carlos" } } });
  if (url.includes("moments.youversionapi.com")) {
    const kind = new URL(url).searchParams.get("kind"), page = new URL(url).searchParams.get("page");
    if (page !== "1") return J({ response: { data: { moments: [] } } });
    if (kind === "plan_subscription") return J({ response: { data: { moments: [{ object: { plan_id: 777, subscription_id: 9, created_dt: "2026-11-01" } }], next_page: null } } });
    if (kind === "plan_segment_completion") return J({ response: { data: { moments: [{ object: { plan_id: 777, day: 3, created_dt: "2026-11-04" } }], next_page: null } } });
    return J({ response: { data: { moments: [] } } });
  }
  if (url.includes("reading-plans.youversionapi.com/3.1/view.json")) return J({ response: { data: { name: { default: "Ansiedade" }, total_days: 7 } } });
  if (url.includes("references.json")) return J({ response: { data: { additional_content: { html: { default: "<p>Não andem ansiosos.</p>" } }, references: ["PHP.4.6-7"] } } });
  if (url.includes("bible.youversionapi.com/3.1/chapter.json"))
    return J({ response: { data: { content: '<div><span class="verse" data-usfm="PHP.4.6"><span class="label">6</span><span class="content">Não andem ansiosos por coisa alguma.</span></span><span class="verse" data-usfm="PHP.4.7"><span class="content">E a paz de Deus guardará.</span></span></div>' } } });
  if (url.includes("open-meteo")) return J({ current: { temperature_2m: 25, apparent_temperature: 26, relative_humidity_2m: 60, wind_speed_10m: 8, weather_code: 1 }, daily: { time: ["2026-11-21"], temperature_2m_max: [30], temperature_2m_min: [18], precipitation_probability_max: [10], weather_code: [1] } });
  return new Response("", { status: 404 });
};

const { default: api } = await import("../netlify/functions/api.mjs");
const { default: voice } = await import("../netlify/functions/voice.mjs");
const DEV = { "X-Device-Token": "tok123", "Content-Type": "application/json" };
const req = (path, { method = "GET", headers = {}, body } = {}) => new Request("https://jarvis.netlify.app" + path, { method, headers, body: body && typeof body !== "string" ? JSON.stringify(body) : body });
const fnCall = (name, args) => ({ candidates: [{ content: { parts: [{ functionCall: { name, args } }] } }] });
let cookie = "";

test("painel exige login e aceita a senha", async () => {
  assert.equal((await api(req("/api/state"))).status, 401);
  assert.equal((await api(req("/login", { method: "POST", body: "password=errada", headers: { "Content-Type": "application/x-www-form-urlencoded" } }))).status, 401);
  const ok = await api(req("/login", { method: "POST", body: "password=segredo", headers: { "Content-Type": "application/x-www-form-urlencoded" } }));
  assert.equal(ok.status, 302);
  cookie = ok.headers.get("set-cookie").split(";")[0];
  const st = await (await api(req("/api/state", { headers: { cookie } }))).json();
  assert.equal(st.device.online, false);
  assert.ok(Array.isArray(st.voices));
});

test("ESP32: token invalido e recusado", async () => {
  assert.equal((await api(req("/api/sync", { method: "POST", headers: { "X-Device-Token": "x" }, body: {} }))).status, 401);
  assert.equal((await voice(req("/api/turn", { method: "POST", headers: { "X-Device-Token": "x" }, body: { text: "oi" } }))).status, 401);
});

test("sync entrega config 1x, fila de comandos e modo rapido com painel aberto", async () => {
  await api(req("/api/secrets", { method: "POST", headers: { cookie, "Content-Type": "application/json" }, body: { groq_api_key: "gsk_abc", gemini_api_key: "g1" } }));
  let r = await (await api(req("/api/sync", { method: "POST", headers: DEV, body: { st: { heap: 120, al: [] } } }))).json();
  assert.equal(r.cfg.gk, "gsk_abc");
  assert.deepEqual([r.cfg.we, r.cfg.ws, r.cfg.bi, r.cfg.fu], [true, false, true, true]); // "Jarvis" ligado por padrao
  assert.equal(r.hot, true); // o painel acabou de abrir /api/state
  assert.equal(r.next, 10);
  await api(req("/api/esp/send", { method: "POST", headers: { cookie, "Content-Type": "application/json" }, body: { type: "alarm_add", h: 7, m: 30, label: "Devocional" } }));
  r = await (await api(req("/api/sync", { method: "POST", headers: DEV, body: { st: { cv: r.cfg.v } } }))).json();
  assert.equal(r.cfg, undefined);
  assert.deepEqual(r.q, [{ t: "alarm_add", h: 7, m: 30, r: 1, l: "Devocional" }]);
  r = await (await api(req("/api/sync", { method: "POST", headers: DEV, body: { st: { cv: 1 } } }))).json();
  assert.deepEqual(r.q, []);
});

test("turno de voz: Gemini cria alarme -> acao no header + MP3", async () => {
  geminiReply = fnCall("jarvis_system", { action: "set_alarm", alarm_hour: 6, alarm_minute: 15, label: "Academia" });
  const r = await voice(req("/api/turn", { method: "POST", headers: DEV, body: { text: "me acorda 6:15", st: { al: [] } } }));
  assert.equal(r.status, 200);
  assert.equal(r.headers.get("content-type"), "audio/mpeg");
  const actions = JSON.parse(Buffer.from(r.headers.get("x-actions"), "base64url").toString());
  assert.deepEqual(actions, [{ t: "alarm_add", h: 6, m: 15, r: 1, l: "Academia" }]);
  assert.equal(r.headers.get("x-parts"), "1");
  assert.match(await r.text(), /^MP3\[/);
});

test("texto longo vira partes; o ESP busca cada parte em /api/say", async () => {
  const long = Array.from({ length: 40 }, (_, i) => `Esta é a frase número ${i + 1} do devocional de hoje.`).join(" ");
  geminiReply = { candidates: [{ content: { parts: [{ text: long }] } }] };
  const r = await voice(req("/api/turn", { method: "POST", headers: DEV, body: { text: "lê algo longo" } }));
  const n = +r.headers.get("x-parts"), id = r.headers.get("x-text-id");
  assert.ok(n >= 3, `partes: ${n}`);
  let total = 0;
  for (let p = 1; p < n; p++) {
    const s = await voice(req(`/api/say?id=${id}&p=${p}`, { headers: DEV }));
    assert.equal(s.status, 200);
    assert.ok(+s.headers.get("content-length") > 0);
    total += +(await s.text()).match(/\[(\d+)\]/)[1];
  }
  assert.ok(total > 1500);
  assert.equal((await voice(req(`/api/say?id=${id}&p=${n}`, { headers: DEV }))).status, 404);
});

test("lembrete por voz vira acao rem_add com horario epoch correto", async () => {
  geminiReply = fnCall("assistant_tools", { action: "reminder_add", query: "pagar a luz", minutes: 20 });
  const r = await voice(req("/api/turn", { method: "POST", headers: DEV, body: { text: "me lembra daqui 20 min de pagar a luz" } }));
  const [a] = JSON.parse(Buffer.from(r.headers.get("x-actions"), "base64url").toString());
  assert.equal(a.t, "rem_add");
  assert.ok(Math.abs(a.at - (Date.now() / 1000 + 1200)) < 5);
});

test("YouVersion: senha errada mostra erro; certa sincroniza e le o dia com devocional + passagem", async () => {
  const H = { cookie, "Content-Type": "application/x-www-form-urlencoded" };
  let r = await api(req("/youversion/login", { method: "POST", headers: H, body: "username=c@x.com&password=errada" }));
  assert.match(r.headers.get("location"), /youversion=error/);
  r = await api(req("/youversion/login", { method: "POST", headers: H, body: "username=c@x.com&password=certa" }));
  assert.match(r.headers.get("location"), /youversion=ok/);
  const st = await (await api(req("/api/state", { headers: { cookie } }))).json();
  assert.equal(st.youversion.active_plan_title, "Ansiedade");
  assert.equal(st.youversion.current_day, 4);
  const b = await (await api(req("/api/bible/youversion?action=read_plan_day", { headers: { cookie } }))).json();
  assert.match(b.spoken, /Ansiedade\. Dia 4 de 7\. Não andem ansiosos\. Leitura de Filipenses 4:6-7 \(NVI\)\. Não andem ansiosos por coisa alguma\. E a paz de Deus guardará\./);
});

test("painel: falar no ESP32 enfileira 'say' e o texto fica disponivel em partes", async () => {
  await api(req("/api/esp/send", { method: "POST", headers: { cookie, "Content-Type": "application/json" }, body: { type: "speak", text: "Olá, senhor." } }));
  const r = await (await api(req("/api/sync", { method: "POST", headers: DEV, body: { st: { cv: 1 } } }))).json();
  const say = r.q.find((c) => c.t === "say");
  assert.equal(say.n, 1);
  assert.equal((await voice(req(`/api/say?id=${say.id}&p=0`, { headers: DEV }))).status, 200);
});

test("listas e rotina com horario entram na agenda do ESP32", async () => {
  const H = { cookie, "Content-Type": "application/json" };
  await api(req("/api/assistant/op", { method: "POST", headers: H, body: { op: "list_add", list: "compras", items: "leite, pão e café" } }));
  const d = await (await api(req("/api/assistant/data", { headers: { cookie } }))).json();
  assert.deepEqual(d.lists.compras.map((i) => i.text), ["leite", "pão", "café"]);
  await api(req("/api/assistant/op", { method: "POST", headers: H, body: { op: "routine_save", name: "Acordar", steps_text: "som\nbriefing", time: "06:40", days: [0, 1, 2, 3, 4] } }));
  const r = await (await api(req("/api/sync", { method: "POST", headers: DEV, body: { st: { cv: 1 } } }))).json();
  assert.deepEqual(r.cfg.rt.find((x) => x[0] === "acordar"), ["acordar", 6, 40, 31]);
});

test("rotina disparada pelo ESP32 (__routine:) executa os passos", async () => {
  geminiReply = { candidates: [{ content: { parts: [{ text: "ok" }] } }] };
  const r = await voice(req("/api/turn", { method: "POST", headers: DEV, body: { text: "__routine:foco" } }));
  const actions = JSON.parse(Buffer.from(r.headers.get("x-actions"), "base64url").toString());
  assert.deepEqual(actions, [{ t: "timer", s: 1500, l: "foco" }]);
});

test("aviso curto do ESP32 (lembrete) vira MP3 via /api/say?text=", async () => {
  const r = await voice(req("/api/say?text=" + encodeURIComponent("Lembrete: pagar a luz."), { headers: DEV }));
  assert.equal(r.status, 200);
  assert.equal(await r.text(), "MP3[22]");
});

test("briefing disparado pelo alarme devocional usa o estado do ESP32", async () => {
  await api(req("/api/settings", { method: "POST", headers: { cookie, "Content-Type": "application/json" }, body: { briefing_plan: false, briefing_verse: false, briefing_dates: false, user_name: "Carlos" } }));
  const al = [[23, 59, 1, "Dormir"]];
  const r = await voice(req("/api/turn", { method: "POST", headers: DEV, body: { text: "__briefing__", st: { al } } }));
  assert.equal(r.status, 200);
  const b = await (await api(req("/api/briefing", { headers: { cookie } }))).json();
  assert.match(b.spoken, /Carlos\. Hoje é .*Temperatura|Carlos\. Hoje é/);
  assert.match(b.spoken, /Próximos alarmes: 23 horas e 59, Dormir/);
});

test("voz: 'Jarvis, desliga o microfone' vira acao mic off; painel religa", async () => {
  geminiReply = fnCall("jarvis_system", { action: "mute_microphone" });
  const r = await voice(req("/api/turn", { method: "POST", headers: DEV, body: { text: "desliga o microfone" } }));
  assert.deepEqual(JSON.parse(Buffer.from(r.headers.get("x-actions"), "base64url").toString()), [{ t: "mic", on: false }]);
  await api(req("/api/esp/send", { method: "POST", headers: { cookie, "Content-Type": "application/json" }, body: { type: "mic_unmute" } }));
  const s = await (await api(req("/api/sync", { method: "POST", headers: DEV, body: { st: { cv: 1, muted: true, psram: 7000 } } }))).json();
  assert.deepEqual(s.q, [{ t: "mic", on: true }]);
  const st = await (await api(req("/api/state", { headers: { cookie } }))).json();
  assert.equal(st.device.muted, true);
  assert.equal(st.device.psram, 7000);
});
