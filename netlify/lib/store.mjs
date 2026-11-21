// Armazenamento persistente gratuito: Netlify Blobs (chave/valor JSON).
// Nos testes, defina globalThis.__JARVIS_STORE__ = { get(key,{type}), setJSON(key,v), delete(key) }.
let _store;

async function store() {
  if (globalThis.__JARVIS_STORE__) return globalThis.__JARVIS_STORE__;
  if (!_store) {
    const { getStore } = await import("@netlify/blobs");
    _store = getStore({ name: "jarvis", consistency: "strong" });
  }
  return _store;
}

const clone = (v) => (v === undefined ? undefined : JSON.parse(JSON.stringify(v)));

export async function getJSON(key, def = null) {
  const s = await store();
  const v = await s.get(key, { type: "json" });
  return v ?? clone(def);
}

export async function setJSON(key, value) {
  const s = await store();
  await s.setJSON(key, value);
  return value;
}

export async function del(key) {
  const s = await store();
  await s.delete(key);
}

/** Le, deixa `fn` alterar o objeto e grava. Retorna o que `fn` retornar. */
export async function update(key, def, fn) {
  const v = await getJSON(key, def);
  const out = await fn(v);
  await setJSON(key, v);
  return out;
}

// ---------------------------------------------------------------- log de eventos e conversas (painel)
export async function logEvent(kind, msg, level = "info") {
  try {
    await update("events", { seq: 0, items: [] }, (ev) => {
      ev.seq += 1;
      ev.items.push({ id: ev.seq, ts: Date.now(), kind, level, msg: String(msg).slice(0, 400) });
      ev.items = ev.items.slice(-150);
    });
  } catch (e) {
    console.error("logEvent", e);
  }
  console.log(`[${kind}] ${msg}`);
}
