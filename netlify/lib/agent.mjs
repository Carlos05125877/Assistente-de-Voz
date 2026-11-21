// Cerebro sob demanda: Gemini + ferramentas. Devolve o texto a falar e as ACOES que o ESP32 executa.
import { getJSON, setJSON, logEvent, update } from "./store.mjs";
import { zoned, WEEKDAYS, MONTHS, norm, fetchJSON, spokenTime } from "./util.mjs";
import { spotify } from "./spotify.mjs";
import { smartControl, smartDevices } from "./tuya.mjs";
import { bible, loadYv, verseOfDay } from "./youversion.mjs";
import { weatherSpoken, newsSpoken, quotesSpoken, wikiSummary } from "./info.mjs";
import * as A from "./assistant.mjs";
import { alarmsOf, timersOf, remindersOf } from "./device.mjs";

export const DEFAULT_SETTINGS = { user_name: "", voice: "pt-BR-AntonioNeural", rate: "+0%", city: "São Paulo", latitude: "-23.5505", longitude: "-46.6333",
  briefing_weather: true, briefing_alarms: true, briefing_reminders: true, briefing_dates: true, briefing_plan: true, briefing_plan_full: true,
  briefing_verse: true, briefing_quotes: false, briefing_news: false, news_topic: "",
  wake_enabled: true, wake_sensitive: false, barge_in: true, follow_up: true };
export const loadSettings = async () => ({ ...DEFAULT_SETTINGS, ...(await getJSON("settings", {})) });

const S = (d) => ({ type: "STRING", description: d }), I = (d) => ({ type: "INTEGER", description: d });
export const TOOLS = [
  { name: "spotify_control", description: "Spotify: tocar musica/artista/album/playlist, pausar, retomar, pular, voltar, volume, o que esta tocando.",
    parameters: { type: "OBJECT", required: ["action"], properties: { action: S("play_query, pause, resume, next, previous, volume, now_playing"), query: S("O que tocar"), search_type: S("track, artist, album, playlist"), volume: I("0 a 100") } } },
  { name: "bible_app", description: "Conta pessoal do App da Biblia (YouVersion): devocional e passagens do dia do plano, avancar dia, trocar plano (nome ou numero), listar planos, sincronizar, versiculo do dia, ler uma passagem.",
    parameters: { type: "OBJECT", required: ["action"], properties: { action: S("read_plan_day, next_plan_day, start_plan, list_plans, sync, verse_of_day, get_passage"), reference: S("Ex: Salmos 23:1-4"), theme: S("Nome ou numero do plano"), plan_day: I("Dia especifico") } } },
  { name: "smart_home", description: "Liga/desliga e ajusta lampadas, fitas LED, tomadas e ar-condicionado (SmartLife/Tuya).",
    parameters: { type: "OBJECT", required: ["device_name", "state"], properties: { device_name: S("Nome do dispositivo"), state: S("on ou off"), brightness: I("1 a 100"), color: S("Cor em portugues") } } },
  { name: "jarvis_system", description: "Alarmes despertadores, timers, notas rapidas, clima, volume dos alto-falantes, desligar o microfone (privacidade), status e briefing do dia.",
    parameters: { type: "OBJECT", required: ["action"], properties: {
      action: S("set_alarm, list_alarms, remove_alarm, clear_alarms, set_timer, list_timers, cancel_timers, add_note, get_notes, clear_notes, get_weather, set_speaker_volume, mute_microphone, system_status, morning_briefing"),
      alarm_hour: I("0-23"), alarm_minute: I("0-59"), alarm_repeat_daily: { type: "BOOLEAN", description: "Todo dia" }, timer_seconds: I("Duracao"), label: S("Rotulo ou texto da nota"), volume: I("0 a 100") } } },
  { name: "assistant_tools", description: "Lembretes com data/hora, listas (compras, tarefas...), noticias, cotacoes, Wikipedia, rotinas e aniversarios/datas especiais.",
    parameters: { type: "OBJECT", required: ["action"], properties: {
      action: S("reminder_add, reminder_list, reminder_remove, list_add, list_read, list_remove, list_clear, news, quotes, wiki, routine_run, routine_list, date_add, date_countdown"),
      query: S("Texto do lembrete, tema, termo, nome da rotina ou do evento"), date: S("AAAA-MM-DD"), time: S("HH:MM"), minutes: I("Daqui a N minutos"),
      repeat: S("none, daily, weekdays, weekly"), list_name: S("Nome da lista"), items: S("Itens separados por virgula") } } },
];

const alarmsSpoken = (st) => {
  const al = alarmsOf(st);
  return al.length ? "Alarmes: " + al.map((a) => `${String(a.h).padStart(2, "0")}:${String(a.m).padStart(2, "0")} ${a.l}` + (a.r ? "" : " (uma vez)")).join("; ") + "." : "Você não tem alarmes salvos.";
};

export async function execTool(name, a = {}, ctx = {}) {
  const st = ctx.st || {};
  const act = String(a.action || "");
  const R = (spoken, actions = []) => ({ spoken, actions });
  if (name === "spotify_control") return R((await spotify({ action: act || "now_playing", q: a.query, type: a.search_type, volume: a.volume })).message);
  if (name === "bible_app") return R((await bible({ action: act || "read_plan_day", ref: a.reference, plan: a.theme, day: a.plan_day })).spoken);
  if (name === "smart_home") return R(await smartControl(a.device_name, a.state, a.brightness ?? -1, a.color));
  if (name === "assistant_tools") {
    const r = await (async () => {
      switch (act) {
        case "news": return { spoken: await newsSpoken(a.query) };
        case "quotes": return { spoken: await quotesSpoken() };
        case "wiki": return wikiSummary(a.query);
        case "reminder_add": return A.reminderAction(a.query, a.date, a.time, a.repeat, a.minutes);
        case "reminder_list": return { spoken: A.remindersSpoken(st) };
        case "reminder_remove": return A.reminderRemoveAction(st, a.query);
        case "list_add": return A.listAdd(a.list_name, a.items || a.query);
        case "list_read": return A.listRead(a.list_name);
        case "list_remove": return A.listRemove(a.list_name, a.items || a.query);
        case "list_clear": return A.listClear(a.list_name);
        case "routine_list": return { spoken: "Suas rotinas: " + (await A.loadRoutines()).map((r) => r.name).join(", ") + "." };
        case "routine_run": { const r = A.findRoutine(await A.loadRoutines(), a.query); return r ? runRoutine(r, ctx) : { spoken: `Não encontrei a rotina ${a.query}.` }; }
        case "date_add": return A.dateAdd(a.query, a.date, a.repeat !== "none");
        case "date_countdown": return A.dateCountdown(a.query);
        default: return { spoken: `Ação ${act} desconhecida.` };
      }
    })();
    await A.logAssistant(act, r.spoken);
    return R(r.spoken, r.actions || []);
  }
  if (name === "jarvis_system") {
    const h = +(a.alarm_hour ?? -1), m = +(a.alarm_minute ?? 0);
    switch (act) {
      case "set_alarm":
        if (!(h >= 0 && h < 24 && m >= 0 && m < 60)) return R("Horário inválido.");
        return R(`Alarme salvo para ${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}` + (a.alarm_repeat_daily === false ? "." : ", todos os dias."),
          [{ t: "alarm_add", h, m, r: a.alarm_repeat_daily === false ? 0 : 1, l: String(a.label || "Despertador").slice(0, 40) }]);
      case "list_alarms": return R(alarmsSpoken(st));
      case "remove_alarm": return R("Alarme removido.", [{ t: "alarm_del", h, m: a.alarm_minute === undefined ? -1 : m }]);
      case "clear_alarms": return R("Todos os alarmes foram apagados.", [{ t: "alarm_clear" }]);
      case "set_timer": {
        const s = Math.max(1, +a.timer_seconds || 60), mm = Math.floor(s / 60), ss = s % 60;
        const dur = [mm ? `${mm} minuto${mm > 1 ? "s" : ""}` : "", ss ? `${ss} segundo${ss > 1 ? "s" : ""}` : ""].filter(Boolean).join(" e ");
        return R(`Timer de ${dur} iniciado.`, [{ t: "timer", s, l: String(a.label || "timer").slice(0, 30) }]);
      }
      case "list_timers": { const t = timersOf(st); return R(t.length ? "Timers: " + t.map((x) => `${x.label}, faltam ${Math.floor(x.left / 60)} minutos e ${x.left % 60} segundos`).join("; ") + "." : "Nenhum timer ativo."); }
      case "cancel_timers": return R("Timers cancelados.", [{ t: "timer_clear" }]);
      case "add_note": return R((await A.listAdd("notas", a.label)).spoken);
      case "get_notes": return R((await A.listRead("notas")).spoken);
      case "clear_notes": return R((await A.listClear("notas")).spoken);
      case "get_weather": return R(await weatherSpoken());
      case "set_speaker_volume": { const v = Math.max(0, Math.min(100, +a.volume || 70)); return R(`Volume em ${v} por cento.`, [{ t: "vol", v }]); }
      case "mute_microphone": return R("Microfone desligado. Para religar, segure o botão por 2 segundos ou use o painel.", [{ t: "mic", on: false }]);
      case "system_status": return R(`Memória livre ${st.heap ?? "?"} kilobytes mais ${Math.round((st.psram || 0) / 1024)} megabytes de PSRAM, WiFi ${st.rssi ?? "?"} dBm, ligado há ${Math.floor((st.up || 0) / 60)} minutos. ${alarmsSpoken(st)}`);
      case "morning_briefing": return R(await briefing(st));
    }
  }
  return R(`Ferramenta ${name}/${act} não reconhecida.`);
}

export async function runRoutine(r, ctx = {}) {
  const speech = [], actions = [];
  for (const s of A.parseSteps(r.steps_text)) {
    try {
      if (s.type === "speak") speech.push(s.arg);
      else if (s.type === "ask" && (ctx.depth || 0) < 1) { const x = await runTurn(s.arg, ctx.st, { depth: 1, quiet: true }); speech.push(x.reply); actions.push(...x.actions); }
      else if (s.type === "volume") actions.push({ t: "vol", v: Math.max(0, Math.min(100, parseInt(s.arg) || 50)) });
      else if (s.type === "music") await spotify({ action: "play_query", q: s.arg.replace(/playlist/i, "").trim(), type: /playlist/i.test(s.arg) ? "playlist" : "track" });
      else if (s.type === "spotify") await spotify({ action: s.arg.split(/\s+/)[0] || "pause" });
      else if (s.type === "briefing") speech.push(await briefing(ctx.st || {}));
      else if (s.type === "bible") speech.push((await bible({ action: s.arg || "read_plan_day" })).spoken);
      else if (s.type === "tone") actions.push({ t: "tone", m: s.arg || "chime" });
      else if (s.type === "list") speech.push((await A.listRead(s.arg || "compras")).spoken);
      else if (s.type === "timer") { const m = s.arg.match(/(\d+)\s*(.*)/); if (m) actions.push({ t: "timer", s: +m[1] * 60, l: (m[2] || "timer").slice(0, 30) }); }
    } catch (e) { await logEvent("rotina", `passo ${s.type} falhou: ${e.message}`, "error"); }
  }
  await logEvent("rotina", `Rotina ${r.name} executada`);
  return { spoken: speech.join(" ") || `Rotina ${r.name} concluída.`, actions };
}

export async function briefing(st = {}) {
  const s = await loadSettings(), now = zoned();
  const nome = s.user_name || "senhor";
  const parts = [`${now.h < 12 ? "Bom dia" : now.h < 18 ? "Boa tarde" : "Boa noite"}, ${nome}. Hoje é ${WEEKDAYS[now.wd]}, ${now.d} de ${MONTHS[now.mo - 1]}, e agora são ${now.h} e ${String(now.mi).padStart(2, "0")}.`];
  const jobs = [];
  if (s.briefing_weather) jobs.push(weatherSpoken());
  if (s.briefing_quotes) jobs.push(quotesSpoken());
  if (s.briefing_news) jobs.push(newsSpoken(s.news_topic, 3));
  const nowMin = now.h * 60 + now.mi;
  if (s.briefing_alarms) {
    const nx = alarmsOf(st).filter((a) => a.h * 60 + a.m > nowMin).sort((x, y) => x.h * 60 + x.m - (y.h * 60 + y.m));
    if (nx.length) jobs.push(Promise.resolve("Próximos alarmes: " + nx.slice(0, 4).map((a) => `${spokenTime(`${a.h}:${String(a.m).padStart(2, "0")}`)}, ${a.l}`).join("; ") + "."));
  }
  if (s.briefing_reminders) {
    const end = Date.now() / 1000 + (24 * 60 - nowMin) * 60;
    const rs = remindersOf(st).filter((r) => r.at < end).sort((a, b) => a.at - b.at);
    if (rs.length) jobs.push(Promise.resolve("Lembretes de hoje: " + rs.slice(0, 5).map((r) => { const z = zoned(new Date(r.at * 1000)); return `${spokenTime(`${z.h}:${String(z.mi).padStart(2, "0")}`)}, ${r.text}`; }).join("; ") + "."));
  }
  if (s.briefing_dates) jobs.push(A.datesView().then((ds) => { const soon = ds.filter((e) => e.days_left <= 7).slice(0, 3); return soon.length ? soon.map((e) => (e.days_left === 0 ? `Hoje é ${e.name}` : `Em ${e.days_left} dias, ${e.name}`)).join("; ") + "." : ""; }));
  parts.push(...(await Promise.all(jobs)).filter(Boolean));
  if (s.briefing_plan) {
    if (s.briefing_plan_full) parts.push("Agora, seu devocional de hoje. " + (await bible({ action: "read_plan_day" })).spoken);
    else { const yv = await loadYv(); if (yv.active_plan_id) parts.push(`Seu plano ${yv.active_plan_title} está no dia ${yv.current_day} de ${yv.total_days}.`); }
  } else if (s.briefing_verse) { const v = await verseOfDay(); parts.push(`Versículo do dia, ${v.reference}: ${v.text}`); }
  parts.push(now.h < 18 ? "Tenha um ótimo dia." : "Tenha uma boa noite.");
  return parts.join(" ");
}

async function systemPrompt(st) {
  const s = await loadSettings(), now = zoned(), yv = await loadYv(), devs = await smartDevices();
  return "Voce e o J.A.R.V.I.S., assistente de voz pessoal. Responda SEMPRE em portugues do Brasil, em texto puro para ser falado " +
    `(sem markdown, sem emojis), elegante e conciso (1 a 3 frases), tratando o usuario por ${s.user_name || "senhor"}. ` +
    `Agora: ${WEEKDAYS[now.wd]}, ${now.y}-${String(now.mo).padStart(2, "0")}-${String(now.d).padStart(2, "0")} ${String(now.h).padStart(2, "0")}:${String(now.mi).padStart(2, "0")}. ` +
    `Plano biblico ativo: ${yv.active_plan_title} (dia ${yv.current_day} de ${yv.total_days}). ${alarmsSpoken(st)} ` +
    `Dispositivos da casa: ${devs.map((d) => d.name).join(", ") || "nenhum"}. ` +
    "Use as ferramentas para musica, Biblia/devocional, casa, alarmes, timers, notas, clima, volume, lembretes, listas, noticias, " +
    "cotacoes, Wikipedia, rotinas e datas especiais. Converta datas relativas para AAAA-MM-DD. Perguntas gerais: responda direto.";
}

async function gemini(contents, st) {
  const sec = await getJSON("secrets", {});
  const key = sec.gemini_api_key || process.env.GEMINI_API_KEY;
  if (!key) return { error: "Chave do Gemini não configurada no painel." };
  const model = sec.gemini_model || process.env.GEMINI_MODEL || "gemini-flash-lite-latest";
  const body = { system_instruction: { parts: [{ text: await systemPrompt(st) }] }, contents, tools: [{ functionDeclarations: TOOLS }],
    generationConfig: { temperature: 0.4, maxOutputTokens: 400 } };
  const r = await fetchJSON(`https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${key}`,
    { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }, 20000);
  return r.status === 200 && r.json ? { json: r.json } : { error: `Gemini HTTP ${r.status}: ${r.text.slice(0, 150)}` };
}

/** Um turno de conversa. `text` pode ser '__briefing__' ou '__routine:<id>' (disparados pelo ESP32). */
export async function runTurn(text, st = {}, opts = {}) {
  const t0 = Date.now();
  text = String(text || "").trim();
  if (text === "__briefing__") return { reply: await briefing(st), actions: [], listen: false };
  if (text.startsWith("__routine:")) {
    const r = A.findRoutine(await A.loadRoutines(), text.slice(10));
    const out = r ? await runRoutine(r, { st }) : { spoken: "Rotina não encontrada.", actions: [] };
    return { reply: out.spoken, actions: out.actions, listen: false };
  }
  const history = opts.depth ? [] : await getJSON("history", []);
  const contents = history.flatMap(([u, m]) => [{ role: "user", parts: [{ text: u }] }, { role: "model", parts: [{ text: m }] }]);
  contents.push({ role: "user", parts: [{ text }] });
  const g = await gemini(contents, st);
  let reply = "", actions = [];
  if (g.error) {
    await logEvent("agente", g.error, "error");
    reply = g.error.includes("não configurada") ? g.error : "Perdão, senhor, não consegui falar com a inteligência artificial agora.";
  } else {
    const parts = g.json.candidates?.[0]?.content?.parts || [];
    const calls = parts.filter((p) => p.functionCall);
    const outs = await Promise.all(calls.map(async (p) => {
      try { return await execTool(p.functionCall.name, p.functionCall.args || {}, { st, depth: opts.depth || 0 }); }
      catch (e) { await logEvent("agente", `${p.functionCall.name}: ${e.message}`, "error"); return { spoken: "Tive um problema ao executar isso.", actions: [] }; }
    }));
    actions = outs.flatMap((o) => o.actions || []);
    reply = outs.length ? outs.map((o) => o.spoken).filter(Boolean).join(" ") : parts.map((p) => p.text || "").join("");
  }
  reply = reply.replace(/[*#`_]+/g, "").trim() || "Desculpe, não entendi. Pode repetir?";
  if (!opts.depth) {
    await setJSON("history", [...history, [text, reply.slice(0, 500)]].slice(-6));
    await update("convs", [], (c) => { c.push({ ts: Date.now(), user: text, reply: reply.slice(0, 1200) }); c.splice(0, Math.max(0, c.length - 40)); });
    await logEvent("voz", `Você: ${text} | Jarvis: ${reply.slice(0, 140)} (${((Date.now() - t0) / 1000).toFixed(1)}s)`);
  }
  return { reply, actions, listen: /\?\s*$/.test(reply) && !actions.length };
}
