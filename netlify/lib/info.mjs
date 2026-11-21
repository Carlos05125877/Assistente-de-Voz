// Clima (Open-Meteo), noticias (Google News RSS), cotacoes (AwesomeAPI) e Wikipedia. Tudo gratis e sem chave.
import { fetchJSON, fetchText, decodeEntities } from "./util.mjs";
import { getJSON } from "./store.mjs";

const WMO = {
  0: "céu limpo", 1: "predominantemente limpo", 2: "parcialmente nublado", 3: "nublado", 45: "neblina", 48: "neblina",
  51: "garoa fraca", 53: "garoa", 55: "garoa forte", 61: "chuva fraca", 63: "chuva", 65: "chuva forte",
  80: "pancadas de chuva fracas", 81: "pancadas de chuva", 82: "pancadas de chuva fortes", 95: "trovoadas",
  96: "trovoadas com granizo", 99: "trovoadas fortes",
};
const n0 = (v) => (v === null || v === undefined || Number.isNaN(+v) ? "?" : String(Math.round(+v)));

export async function getWeather() {
  const st = await getJSON("settings", {});
  const lat = st.latitude || "-23.5505", lon = st.longitude || "-46.6333", city = st.city || "São Paulo";
  const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&timezone=auto&forecast_days=3` +
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m" +
    "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code";
  const r = await fetchJSON(url, {}, 8000);
  const j = r.json;
  if (!j?.current) return { ok: false, city, error: `Open-Meteo HTTP ${r.status}` };
  const dly = j.daily || {};
  return {
    ok: true, city, temp: j.current.temperature_2m, feels: j.current.apparent_temperature,
    humidity: j.current.relative_humidity_2m, wind: j.current.wind_speed_10m,
    desc: WMO[j.current.weather_code] || "tempo variável",
    days: (dly.time || []).slice(0, 3).map((date, i) => ({
      date, max: dly.temperature_2m_max[i], min: dly.temperature_2m_min[i],
      rain: dly.precipitation_probability_max?.[i], desc: WMO[dly.weather_code[i]] || "tempo variável",
    })),
  };
}

export async function weatherSpoken() {
  const w = await getWeather();
  if (!w.ok) return "Não consegui consultar o clima agora.";
  let s = `Em ${w.city}, ${w.desc}, ${n0(w.temp)} graus, sensação de ${n0(w.feels)}.`;
  const [d0, d1] = w.days;
  if (d0) s += ` Hoje, máxima de ${n0(d0.max)} e mínima de ${n0(d0.min)}, com ${n0(d0.rain)} por cento de chance de chuva.`;
  if (d1) s += ` Amanhã, ${d1.desc}, entre ${n0(d1.min)} e ${n0(d1.max)} graus.`;
  return s;
}

export async function getNews(topic = "", limit = 6) {
  const base = "https://news.google.com/rss";
  const url = topic ? `${base}/search?q=${encodeURIComponent(topic)}&hl=pt-BR&gl=BR&ceid=BR:pt-419` : `${base}?hl=pt-BR&gl=BR&ceid=BR:pt-419`;
  const r = await fetchText(url, {}, 8000);
  const items = [];
  for (const m of r.text.matchAll(/<item>([\s\S]*?)<\/item>/g)) {
    const get = (tag) => decodeEntities((m[1].match(new RegExp(`<${tag}[^>]*>([\\s\\S]*?)</${tag}>`)) || [])[1] || "").replace(/<!\[CDATA\[|\]\]>/g, "").trim();
    let title = get("title");
    const source = get("source");
    if (source && title.endsWith(" - " + source)) title = title.slice(0, -(source.length + 3));
    items.push({ title, source, link: get("link"), date: get("pubDate") });
    if (items.length >= limit) break;
  }
  return { ok: items.length > 0, topic, items, error: items.length ? "" : `Google News HTTP ${r.status}` };
}

export async function newsSpoken(topic = "", n = 5) {
  const nw = await getNews(topic);
  if (!nw.ok) return "Não consegui buscar as notícias agora.";
  return (topic ? `Notícias sobre ${topic}: ` : "Principais notícias: ") +
    nw.items.slice(0, n).map((x, i) => `${i + 1}. ${x.title}${x.source ? ", do " + x.source : ""}.`).join(" ");
}

export async function getQuotes() {
  const r = await fetchJSON("https://economia.awesomeapi.com.br/json/last/USD-BRL,EUR-BRL,BTC-BRL", {}, 8000);
  const out = [];
  for (const [k, name] of [["USDBRL", "Dólar"], ["EURBRL", "Euro"], ["BTCBRL", "Bitcoin"]]) {
    const q = r.json?.[k];
    if (q?.bid) out.push({ code: k.slice(0, 3), name, bid: +q.bid, pct: +(q.pctChange || 0) });
  }
  return { ok: out.length > 0, quotes: out, error: out.length ? "" : `AwesomeAPI HTTP ${r.status}` };
}

function brl(v) {
  if (v >= 10000) return `${Math.round(v / 1000).toLocaleString("pt-BR")} mil reais`;
  let reais = Math.floor(v), cent = Math.round((v - reais) * 100);
  if (cent === 100) { reais += 1; cent = 0; }
  return `${reais} rea${reais === 1 ? "l" : "is"}` + (cent ? ` e ${cent} centavos` : "");
}

export async function quotesSpoken() {
  const q = await getQuotes();
  if (!q.ok) return "Não consegui consultar as cotações agora.";
  return "Cotações agora: " + q.quotes.map((x) => {
    const t = x.pct > 0 ? "alta" : x.pct < 0 ? "queda" : "";
    return `${x.name} a ${brl(x.bid)}` + (t ? `, ${t} de ${Math.abs(x.pct).toFixed(1).replace(".", ",")} por cento` : "");
  }).join("; ") + ".";
}

export async function wikiSummary(q) {
  q = String(q || "").trim();
  if (!q) return { ok: false, spoken: "Sobre o que você quer saber?" };
  const s = await fetchJSON(`https://pt.wikipedia.org/w/api.php?action=opensearch&limit=1&namespace=0&format=json&search=${encodeURIComponent(q)}`);
  const title = s.json?.[1]?.[0] || q;
  const r = await fetchJSON(`https://pt.wikipedia.org/api/rest_v1/page/summary/${encodeURIComponent(title.replace(/ /g, "_"))}`);
  const extract = String(r.json?.extract || "").replace(/\s+/g, " ").trim();
  if (!extract) return { ok: false, spoken: `Não encontrei ${q} na Wikipédia.` };
  const short = extract.split(/(?<=[.!?])\s+/).slice(0, 3).join(" ");
  return { ok: true, title: r.json.title || title, extract, url: r.json.content_urls?.desktop?.page || "", spoken: `Segundo a Wikipédia, ${short}` };
}
