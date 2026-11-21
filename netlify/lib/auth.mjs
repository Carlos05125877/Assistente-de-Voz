// Autenticacao leve e sem estado: cookie do painel = HMAC(PANEL_PASSWORD) e token fixo do ESP32.
import { createHmac, timingSafeEqual } from "node:crypto";

const env = (k) => process.env[k] || globalThis.__JARVIS_ENV__?.[k] || "";
const safeEq = (a, b) => { a = Buffer.from(String(a)); b = Buffer.from(String(b)); return a.length === b.length && timingSafeEqual(a, b); };
const cookieValue = () => createHmac("sha256", env("PANEL_PASSWORD") || "sem-senha").update("jarvis-panel-v1").digest("base64url");

export function getCookie(req, name) {
  const m = (req.headers.get("cookie") || "").match(new RegExp(`(?:^|;\\s*)${name}=([^;]+)`));
  return m ? decodeURIComponent(m[1]) : "";
}
export const isPanel = (req) => !env("PANEL_PASSWORD") || safeEq(getCookie(req, "jarvis"), cookieValue());
export function isDevice(req) {
  const tok = env("DEVICE_TOKEN");
  const got = req.headers.get("x-device-token") || (req.headers.get("authorization") || "").replace(/^Bearer\s+/i, "");
  return !!tok && safeEq(got, tok);
}
export const checkPassword = (p) => !!env("PANEL_PASSWORD") && safeEq(p, env("PANEL_PASSWORD"));
export const loginCookie = () => `jarvis=${cookieValue()}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${60 * 60 * 24 * 60}`;
export const logoutCookie = () => "jarvis=; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=0";
export const configured = () => ({ panel: !!env("PANEL_PASSWORD"), device: !!env("DEVICE_TOKEN") });
