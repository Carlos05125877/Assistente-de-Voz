// Voz: /api/turn (ESP32 manda o texto do Whisper -> recebe MP3 + acoes), /api/say (partes de textos longos),
// /api/tts (previa no navegador do painel).
import { isDevice, isPanel } from "../lib/auth.mjs";
import { runTurn } from "../lib/agent.mjs";
import { mp3Response, mp3Buffered } from "../lib/tts.mjs";
import { splitForTts, b64urlJSON } from "../lib/util.mjs";
import { storeSpeech, loadSpeech, saveDeviceState } from "../lib/device.mjs";
import { getJSON, setJSON } from "../lib/store.mjs";

const deny = () => new Response("unauthorized", { status: 401 });
const expose = { "Access-Control-Expose-Headers": "X-Actions, X-Parts, X-Text-Id, X-Listen" };

export default async (req) => {
  const url = new URL(req.url);
  const path = url.pathname;

  if (path === "/api/turn") {
    if (!isDevice(req)) return deny();
    const body = await req.json().catch(() => ({}));
    if (body.st) await saveDeviceState(body.st);
    const text = String(body.text || "").trim();
    if (!text) return new Response(null, { status: 204 });
    const out = await runTurn(text, body.st || {});
    const parts = splitForTts(out.reply);
    const id = parts.length > 1 ? await storeSpeech(parts) : "";
    await setJSON("spoken/last", out.reply);
    const headers = { "X-Actions": b64urlJSON(out.actions), "X-Parts": String(parts.length), "X-Text-Id": id, "X-Listen": out.listen ? "1" : "0", ...expose };
    if (!parts.length) return new Response(null, { status: 204, headers });
    return mp3Response(parts[0], headers); // 1a parte curta em streaming: comeca a tocar em ~1 s
  }

  if (path === "/api/say") {
    if (!isDevice(req) && !isPanel(req)) return deny();
    const direct = url.searchParams.get("text");      // avisos curtos do ESP32 (lembrete, timer)
    if (direct) return mp3Buffered(direct.slice(0, 600));
    const parts = await loadSpeech(url.searchParams.get("id") || "");
    const p = +(url.searchParams.get("p") || 0);
    if (!parts || !parts[p]) return new Response("not found", { status: 404 });
    return mp3Buffered(parts[p], { "X-Parts": String(parts.length) });
  }

  if (path === "/api/tts") {
    if (!isPanel(req)) return deny();
    const key = url.searchParams.get("key");
    let text = url.searchParams.get("text") || "";
    if (key === "preview" || key === "last") text = (await getJSON(`spoken/${key}`, "")) || "";
    text = text.slice(0, 4000) || "Olá, eu sou o Jarvis.";
    return mp3Response(text, {}, url.searchParams.get("voice") || undefined, url.searchParams.get("rate") || undefined);
  }
  return new Response("not found", { status: 404 });
};

export const config = { path: ["/api/turn", "/api/say", "/api/tts"] };
