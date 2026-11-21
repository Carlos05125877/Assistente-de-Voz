// SmartLife / Tuya Cloud (assinatura HMAC-SHA256) + webhook opcional (Home Assistant etc.).
import { createHash, createHmac } from "node:crypto";
import { getJSON, setJSON, logEvent } from "./store.mjs";
import { fetchJSON, norm } from "./util.mjs";

const COLORS = [["vermelh", 0, 1000], ["laranja", 30, 1000], ["amarel", 60, 1000], ["verde", 120, 1000], ["ciano", 180, 1000],
  ["azul", 240, 1000], ["rox", 275, 1000], ["rosa", 320, 1000], ["branc", 0, 0], ["quente", 35, 350]];

async function creds() {
  const s = await getJSON("secrets", {});
  return { id: s.tuya_access_id || process.env.TUYA_ACCESS_ID || "", secret: s.tuya_access_secret || process.env.TUYA_ACCESS_SECRET || "",
    host: s.tuya_host || process.env.TUYA_HOST || "openapi.tuyaus.com", hook: s.smart_webhook_url || "" };
}

async function tuyaRequest(c, method, path, body = "") {
  let token = "";
  if (!path.startsWith("/v1.0/token")) {
    const cache = await getJSON("tuya_token", {});
    if (cache.token && Date.now() < cache.exp) token = cache.token;
    else {
      const r = await tuyaRequest(c, "GET", "/v1.0/token?grant_type=1");
      if (!r?.success) return r;
      token = r.result.access_token;
      await setJSON("tuya_token", { token, exp: Date.now() + (r.result.expire_time - 120) * 1000 });
    }
  }
  const t = String(Date.now());
  const toSign = `${method}\n${createHash("sha256").update(body).digest("hex")}\n\n${path}`;
  const sign = createHmac("sha256", c.secret).update(c.id + token + t + toSign).digest("hex").toUpperCase();
  const headers = { client_id: c.id, sign, t, sign_method: "HMAC-SHA256", ...(token ? { access_token: token } : {}), ...(body ? { "Content-Type": "application/json" } : {}) };
  const r = await fetchJSON(`https://${c.host}${path}`, { method, headers, body: body || undefined });
  return r.json || { success: false, msg: `HTTP ${r.status}` };
}

export async function smartDevices() {
  return getJSON("smart_devices", []);
}

function findDevice(devs, name) {
  const n = norm(name);
  return devs.find((d) => [d.name, ...String(d.aliases || "").split(",")].map(norm).filter(Boolean).some((x) => x === n || x.includes(n) || n.includes(x)));
}

export async function smartControl(deviceName, state = "on", brightness = -1, color = "") {
  const devs = await smartDevices();
  const dev = findDevice(devs, deviceName);
  const label = dev?.name || deviceName;
  const on = !["off", "desligar", "desliga", "false", "0"].includes(norm(state));
  const c = await creds();
  const verb = on ? "liguei" : "desliguei";
  if (c.hook) {
    const r = await fetchJSON(c.hook, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ device: label, state: on ? "on" : "off", brightness, color }) }, 6000);
    if (r.status >= 200 && r.status < 300) return `Pronto, ${verb} ${label}.`;
  }
  if (!dev) return `Não encontrei o dispositivo ${deviceName}. Dispositivos: ${devs.map((d) => d.name).join(", ") || "nenhum cadastrado"}.`;
  if (!c.id || !c.secret) return `Simulação: eu ${on ? "ligaria" : "desligaria"} ${label}. Configure as chaves da Tuya no painel.`;
  const cmds = [{ code: dev.switch_code || "switch_led", value: on }];
  if (on && dev.rgb && +brightness > 0) cmds.push({ code: "bright_value_v2", value: Math.round(10 + (Math.min(100, +brightness) - 1) * 990 / 99) });
  if (on && dev.rgb && color) {
    const hit = COLORS.find(([w]) => norm(color).includes(w));
    if (hit) cmds.push(...(hit[2] === 0 ? [{ code: "work_mode", value: "white" }] : [{ code: "work_mode", value: "colour" }, { code: "colour_data_v2", value: JSON.stringify({ h: hit[1], s: hit[2], v: 1000 }) }]));
  }
  const r = await tuyaRequest(c, "POST", `/v1.0/iot-03/devices/${dev.device_id}/commands`, JSON.stringify({ commands: cmds }));
  await logEvent("casa", `${label}: ${r?.success ? "OK" : r?.msg}`, r?.success ? "info" : "error");
  return r?.success ? `Pronto, ${verb} ${label}.` : `A nuvem da Tuya recusou o comando para ${label}.`;
}
