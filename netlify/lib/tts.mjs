// Voz neural gratis (Microsoft Edge Read Aloud via msedge-tts) em MP3 24 kHz 48 kbps (~6 KB/s).
// O ESP32 decodifica o MP3 (libhelix), entao a funcao termina rapido e gasta pouco credito.
// Nos testes: globalThis.__JARVIS_TTS__ = async (text, voice, rate) => AsyncIterable<Uint8Array>.
import { Readable } from "node:stream";
import { getJSON, logEvent } from "./store.mjs";

export const VOICES = [
  { id: "pt-BR-AntonioNeural", label: "Antonio (masculina, padrão)" },
  { id: "pt-BR-FranciscaNeural", label: "Francisca (feminina)" },
  { id: "pt-BR-ThalitaMultilingualNeural", label: "Thalita (feminina, multilíngue)" },
  { id: "pt-PT-DuarteNeural", label: "Duarte (Portugal)" },
  { id: "pt-PT-RaquelNeural", label: "Raquel (Portugal)" },
];

const xmlEscape = (s) => String(s).replace(/[<>&"']/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", '"': "&quot;", "'": "&apos;" })[c]);

async function edgeStream(text, voice, rate) {
  if (globalThis.__JARVIS_TTS__) return globalThis.__JARVIS_TTS__(text, voice, rate);
  const { MsEdgeTTS, OUTPUT_FORMAT } = await import("msedge-tts");
  const tts = new MsEdgeTTS();
  await tts.setMetadata(voice, OUTPUT_FORMAT.AUDIO_24KHZ_48KBITRATE_MONO_MP3);
  const { audioStream } = tts.toStream(xmlEscape(text), { rate });
  audioStream.on("close", () => { try { tts.close(); } catch {} });
  return audioStream; // Node Readable (async iterable)
}

/** Fallback se o Edge falhar: Google Translate TTS (MP3, ate ~190 caracteres por pedaco). */
async function* googleFallback(text) {
  const pieces = String(text).match(/.{1,180}(\s|$)/g) || [];
  for (const piece of pieces) {
    const url = "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=pt-BR&q=" + encodeURIComponent(piece.trim());
    const r = await fetch(url, { headers: { "User-Agent": "Mozilla/5.0" }, signal: AbortSignal.timeout(8000) });
    if (!r.ok) throw new Error("Google TTS HTTP " + r.status);
    yield new Uint8Array(await r.arrayBuffer());
  }
}

export async function voiceSettings() {
  const st = await getJSON("settings", {});
  return { voice: st.voice || "pt-BR-AntonioNeural", rate: st.rate || "+0%" };
}

/** Async iterable de bytes MP3 para `text`. */
export async function* mp3Chunks(text, voice, rate) {
  if (!voice) ({ voice, rate } = await voiceSettings());
  let sent = 0;
  try {
    for await (const chunk of await edgeStream(text, voice, rate)) {
      sent += chunk.length;
      yield chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk);
    }
    if (sent > 0) return;
    throw new Error("Edge TTS devolveu audio vazio");
  } catch (e) {
    if (sent > 0) return; // ja comecou a tocar: nao mistura vozes
    await logEvent("tts", `Edge-TTS falhou (${e.message}); usando voz reserva`, "warn");
    yield* googleFallback(text);
  }
}

/** Response HTTP com o MP3 em streaming. */
export function mp3Response(text, headers = {}, voice, rate) {
  const body = Readable.toWeb(Readable.from(mp3Chunks(text, voice, rate)));
  return new Response(body, {
    status: 200,
    headers: { "Content-Type": "audio/mpeg", "Cache-Control": "no-store", ...headers },
  });
}

/** MP3 inteiro em memoria: a funcao termina em ~1-3 s e o Netlify entrega o arquivo no ritmo do ESP32
 *  (sem cobrar computacao enquanto o ESP toca). Ideal para as partes 2..N de textos longos. */
export async function mp3Buffered(text, headers = {}, voice, rate) {
  const chunks = [];
  for await (const c of mp3Chunks(text, voice, rate)) chunks.push(c);
  const buf = Buffer.concat(chunks.map((c) => Buffer.from(c.buffer, c.byteOffset, c.byteLength)));
  return new Response(buf, { status: 200, headers: { "Content-Type": "audio/mpeg", "Content-Length": String(buf.length), "Cache-Control": "no-store", ...headers } });
}
