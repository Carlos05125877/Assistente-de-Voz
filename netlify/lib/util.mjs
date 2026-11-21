// Utilitarios sem dependencias.
export const TZ = process.env.JARVIS_TZ || "America/Sao_Paulo";
export const WEEKDAYS = ["segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado", "domingo"];
export const MONTHS = ["janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"];

export const norm = (s) => String(s ?? "").toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "").trim();
export const uid = () => crypto.randomUUID().replace(/-/g, "").slice(0, 10);
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export function decodeEntities(s) {
  return String(s ?? "")
    .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(+n))
    .replace(/&#x([0-9a-f]+);/gi, (_, n) => String.fromCodePoint(parseInt(n, 16)))
    .replace(/&quot;/g, '"').replace(/&#39;|&apos;/g, "'").replace(/&lt;/g, "<").replace(/&gt;/g, ">")
    .replace(/&nbsp;/g, " ").replace(/&amp;/g, "&");
}

/** HTML de devocional -> texto corrido, com ponto entre paragrafos (melhor para o TTS). */
export function htmlToSpeech(raw) {
  if (!raw) return "";
  let t = String(raw).replace(/<(script|style)[^>]*>[\s\S]*?<\/\1>/gi, " ");
  t = t.replace(/<br\s*\/?>|<\/p>|<\/div>|<\/li>|<\/h\d>/gi, "\n").replace(/<[^>]+>/g, " ");
  const lines = decodeEntities(t).split("\n").map((l) => l.replace(/\s+/g, " ").trim()).filter(Boolean);
  return lines.map((l, i) => (i < lines.length - 1 && !/[.!?:;…]$/.test(l) ? l + "." : l)).join(" ");
}

export function joinPt(xs) {
  xs = xs.filter(Boolean);
  if (xs.length <= 1) return xs[0] || "";
  return xs.slice(0, -1).join(", ") + " e " + xs[xs.length - 1];
}

/** Divide por frases. 1o bloco curto (a voz comeca rapido); os demais ate `max` caracteres (~45 s de fala). */
export function splitForTts(text, first = 220, max = 650) {
  text = String(text || "").replace(/\s+/g, " ").trim();
  if (!text) return [];
  const sentences = text.split(/(?<=[.!?;:…])\s+/);
  const parts = [];
  let cur = "";
  for (let s of sentences) {
    while (s.length > max) {
      let cut = s.lastIndexOf(", ", max);
      cut = cut > 80 ? cut + 1 : (s.lastIndexOf(" ", max) > 0 ? s.lastIndexOf(" ", max) : max);
      if (cur) { parts.push(cur); cur = ""; }
      parts.push(s.slice(0, cut).trim());
      s = s.slice(cut).trim();
    }
    const limit = parts.length ? max : first;
    if (cur && cur.length + 1 + s.length > limit) { parts.push(cur); cur = s; }
    else cur = (cur + " " + s).trim();
  }
  if (cur) parts.push(cur);
  return parts.filter(Boolean);
}

// ---------------------------------------------------------------- datas no fuso do usuario
export function zoned(date = new Date(), tz = TZ) {
  const f = new Intl.DateTimeFormat("en-US", {
    timeZone: tz, year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false, weekday: "short",
  });
  const p = Object.fromEntries(f.formatToParts(date).map((x) => [x.type, x.value]));
  return {
    y: +p.year, mo: +p.month, d: +p.day, h: +p.hour % 24, mi: +p.minute, s: +p.second,
    wd: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"].indexOf(p.weekday), // 0 = segunda
  };
}

export function tzOffsetMin(date = new Date(), tz = TZ) {
  const p = zoned(date, tz);
  return Math.round((Date.UTC(p.y, p.mo - 1, p.d, p.h, p.mi, p.s) - Math.floor(date.getTime() / 1000) * 1000) / 60000);
}

/** Data/hora local -> epoch (segundos UTC). */
export function localToEpoch(y, mo, d, h, mi, tz = TZ) {
  const guess = Date.UTC(y, mo - 1, d, h, mi);
  return Math.floor((guess - tzOffsetMin(new Date(guess), tz) * 60000) / 1000);
}

export const isoDate = ({ y, mo, d }) => `${y}-${String(mo).padStart(2, "0")}-${String(d).padStart(2, "0")}`;
export function addDays({ y, mo, d }, n) {
  const t = new Date(Date.UTC(y, mo - 1, d + n));
  return { y: t.getUTCFullYear(), mo: t.getUTCMonth() + 1, d: t.getUTCDate(), wd: (t.getUTCDay() + 6) % 7 };
}
const WEEKDAY_WORDS = { segunda: 0, terca: 1, quarta: 2, quinta: 3, sexta: 4, sabado: 5, domingo: 6 };

/** 'hoje', 'amanha', 'depois de amanha', 'sexta', '2026-12-25', '25/12', 'dia 15' -> {y,mo,d} */
export function resolveDate(text, base = zoned()) {
  const s = norm(text);
  const b = { y: base.y, mo: base.mo, d: base.d, wd: base.wd };
  if (!s || s === "hoje") return b;
  if (s.includes("depois de amanha")) return addDays(b, 2);
  if (s.includes("amanha")) return addDays(b, 1);
  let m = s.match(/(\d{4})-(\d{1,2})-(\d{1,2})/);
  if (m) return { y: +m[1], mo: +m[2], d: +m[3] };
  m = s.match(/\b(\d{1,2})\/(\d{1,2})(?:\/(\d{2,4}))?\b/);
  if (m) {
    let y = m[3] ? +m[3] : b.y;
    if (y < 100) y += 2000;
    const out = { y, mo: +m[2], d: +m[1] };
    if (!m[3] && isoDate(out) < isoDate(b)) out.y += 1;
    return out;
  }
  for (const [w, wd] of Object.entries(WEEKDAY_WORDS)) if (s.includes(w)) return addDays(b, ((wd - b.wd + 7) % 7) || 7);
  m = s.match(/\bdia (\d{1,2})\b/);
  if (m) {
    let { y, mo } = b;
    if (+m[1] < b.d) { mo += 1; if (mo > 12) { mo = 1; y += 1; } }
    return { y, mo, d: +m[1] };
  }
  return null;
}

/** '09:30', '9h30', '9h', '21', '7 e meia', '9 da noite' -> 'HH:MM' */
export function resolveTime(text) {
  const s = norm(text);
  const m = s.match(/(\d{1,2})\s*(?:[:h]\s*(\d{2})?)?/);
  if (!m) return null;
  let h = +m[1];
  const mi = m[2] ? +m[2] : s.includes("meia") ? 30 : 0;
  if ((s.includes("tarde") || s.includes("noite")) && h < 12) h += 12;
  if (h > 23 || mi > 59) return null;
  return `${String(h).padStart(2, "0")}:${String(mi).padStart(2, "0")}`;
}

export function spokenDate({ y, mo, d }, base = zoned()) {
  const today = isoDate(base), iso = isoDate({ y, mo, d });
  if (iso === today) return "hoje";
  if (iso === isoDate(addDays(base, 1))) return "amanhã";
  const wd = (new Date(Date.UTC(y, mo - 1, d)).getUTCDay() + 6) % 7;
  return `${WEEKDAYS[wd]}, ${d} de ${MONTHS[mo - 1]}`;
}
export const spokenTime = (hhmm) => { const [h, m] = hhmm.split(":").map(Number); return `${h} horas` + (m ? ` e ${m}` : ""); };

export async function fetchText(url, opts = {}, timeoutMs = 9000) {
  try {
    const r = await fetch(url, { ...opts, signal: AbortSignal.timeout(timeoutMs) });
    return { status: r.status, text: await r.text(), headers: r.headers };
  } catch (e) {
    return { status: 0, text: String(e?.message || e), headers: new Headers() };
  }
}

export async function fetchJSON(url, opts = {}, timeoutMs = 9000) {
  const r = await fetchText(url, opts, timeoutMs);
  let json = null;
  try { json = r.text && /^[\[{]/.test(r.text.trim()) ? JSON.parse(r.text) : null; } catch { json = null; }
  return { ...r, json };
}

export const b64urlJSON = (o) => Buffer.from(JSON.stringify(o)).toString("base64url");
