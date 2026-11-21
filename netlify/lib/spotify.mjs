// Spotify Web API (OAuth2 Authorization Code). Refresh token guardado no Netlify Blobs.
import { getJSON, setJSON } from "./store.mjs";
import { fetchJSON } from "./util.mjs";

export async function spotifyCfg() {
  const sec = await getJSON("secrets", {});
  const sp = await getJSON("spotify", {});
  return { ...sp, client_id: sec.spotify_client_id || process.env.SPOTIFY_CLIENT_ID || "", client_secret: sec.spotify_client_secret || process.env.SPOTIFY_CLIENT_SECRET || "" };
}

export async function spotifyToken() {
  const sp = await spotifyCfg();
  if (!sp.refresh_token || !sp.client_id) return null;
  if (sp.access_token && Date.now() < (sp.expires_at || 0)) return sp.access_token;
  const r = await fetchJSON("https://accounts.spotify.com/api/token", {
    method: "POST",
    headers: { Authorization: "Basic " + Buffer.from(`${sp.client_id}:${sp.client_secret}`).toString("base64"), "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ grant_type: "refresh_token", refresh_token: sp.refresh_token }),
  });
  if (!r.json?.access_token) return null;
  const stored = await getJSON("spotify", {});
  stored.access_token = r.json.access_token;
  stored.expires_at = Date.now() + (r.json.expires_in - 90) * 1000;
  if (r.json.refresh_token) stored.refresh_token = r.json.refresh_token;
  await setJSON("spotify", stored);
  return stored.access_token;
}

export async function spotifyExchange(code, redirectUri) {
  const sp = await spotifyCfg();
  const r = await fetchJSON("https://accounts.spotify.com/api/token", {
    method: "POST",
    headers: { Authorization: "Basic " + Buffer.from(`${sp.client_id}:${sp.client_secret}`).toString("base64"), "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ grant_type: "authorization_code", code, redirect_uri: redirectUri }),
  });
  if (!r.json?.refresh_token) return { ok: false, error: r.text.slice(0, 200) };
  const stored = await getJSON("spotify", {});
  Object.assign(stored, { refresh_token: r.json.refresh_token, access_token: r.json.access_token, expires_at: Date.now() + (r.json.expires_in - 90) * 1000 });
  await setJSON("spotify", stored);
  return { ok: true };
}

const api = (tok, path, method = "GET", body) =>
  fetchJSON("https://api.spotify.com/v1" + path, { method, headers: { Authorization: `Bearer ${tok}`, "Content-Type": "application/json" }, body: body ? JSON.stringify(body) : undefined });

export async function spotify({ action = "now_playing", q = "", type = "track", volume } = {}) {
  const tok = await spotifyToken();
  if (!tok) return { ok: false, message: "O Spotify ainda não está conectado. Conecte no painel, na aba Spotify." };
  const noDevice = (r) => r.status === 404 ? " Abra o Spotify em algum aparelho para eu conseguir tocar." : "";
  if (volume !== undefined && volume !== "" && +volume >= 0) {
    const r = await api(tok, `/me/player/volume?volume_percent=${Math.max(0, Math.min(100, +volume))}`, "PUT");
    if (action === "volume") return { ok: r.status < 300, message: r.status < 300 ? `Volume do Spotify em ${volume} por cento.` : "Não consegui mudar o volume." + noDevice(r) };
  }
  if (action === "play_query" && q) {
    const t = ["track", "artist", "album", "playlist"].includes(type) ? type : "track";
    const s = await api(tok, `/search?type=${t}&limit=1&market=BR&q=${encodeURIComponent(q)}`);
    const item = s.json?.[t + "s"]?.items?.[0];
    if (!item) return { ok: false, message: `Não encontrei ${q} no Spotify.` };
    const body = t === "track" ? { uris: [item.uri] } : { context_uri: item.uri };
    const r = await api(tok, "/me/player/play", "PUT", body);
    const name = item.name + (item.artists?.[0]?.name ? ", de " + item.artists[0].name : "");
    return r.status < 300 ? { ok: true, message: `Tocando ${name}.`, track: name } : { ok: false, message: `Não consegui tocar ${name}.` + noDevice(r) };
  }
  const simple = { pause: ["/me/player/pause", "PUT", "Spotify pausado."], resume: ["/me/player/play", "PUT", "Retomando a música."],
    next: ["/me/player/next", "POST", "Próxima música."], previous: ["/me/player/previous", "POST", "Voltando a música."] };
  if (simple[action]) {
    const [p, m, msg] = simple[action];
    const r = await api(tok, p, m);
    return r.status < 300 ? { ok: true, message: msg } : { ok: false, message: "O Spotify não respondeu." + noDevice(r) };
  }
  const c = await api(tok, "/me/player/currently-playing");
  const item = c.json?.item;
  if (!item) return { ok: true, message: "Nada está tocando no Spotify agora.", track: "" };
  const artist = item.artists?.[0]?.name || "";
  return {
    ok: true, message: `${c.json.is_playing ? "Tocando agora" : "Pausado em"}: ${item.name}, de ${artist}.`, track: `${item.name} - ${artist}`,
    name: item.name, artist, album: item.album?.name || "", image: item.album?.images?.[0]?.url || "", is_playing: !!c.json.is_playing,
    progress_ms: c.json.progress_ms || 0, duration_ms: item.duration_ms || 0, device: c.json.device?.name || "",
  };
}
