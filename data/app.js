"use strict";

// Vanilla, no build step, matching every other tool in this repo
// (tools/m4client.py is the same client in Python; this is its browser
// twin). Renders settings forms straight from GET /api/apps -- there is no
// per-app UI code here, and there must never be one: a new app or a new
// setting on an existing app needs zero changes on this side.
//
// Signing follows docs/security.md exactly:
//   Authorization: HMAC id=<hex>,ts=<unix seconds>,nonce=<hex>,sig=<hex>
//   sig = HMAC-SHA256(secret, METHOD "\n" TARGET "\n" TS "\n" NONCE "\n" SHA256HEX(body))
// TARGET is the request path plus query string, exactly as sent -- the same
// rule the firmware's canonical_request() follows.
//
// SHA-256 is hand-rolled rather than crypto.subtle: this board is
// deliberately plain HTTP on a LAN IP (docs/security.md, "Why not HTTPS"),
// and SubtleCrypto is only reachable from a secure context -- HTTPS, or
// HTTP on localhost -- neither of which this page is ever served from.
// crypto.getRandomValues() has no such restriction and is used as-is.

const STORAGE_KEY = "matrixfaces.credentials";

// ---------------------------------------------------------------------------
// Crypto helpers
// ---------------------------------------------------------------------------

function bytesToHex(bytes) {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

function hexToBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
  return out;
}

function randomHex(byteLen) {
  const bytes = new Uint8Array(byteLen);
  crypto.getRandomValues(bytes);
  return bytesToHex(bytes);
}

// FIPS 180-4 SHA-256 over a byte array. Verified against the empty-string,
// "abc", and RFC 4231 HMAC test vectors before this was wired into signing.
const SHA256_K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

function sha256(bytes) {
  const H = new Uint32Array([
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ]);

  const bitLen = bytes.length * 8;
  const padded = new Uint8Array(Math.ceil((bytes.length + 9) / 64) * 64);
  padded.set(bytes);
  padded[bytes.length] = 0x80;
  const view = new DataView(padded.buffer);
  view.setUint32(padded.length - 4, bitLen >>> 0, false);
  view.setUint32(padded.length - 8, Math.floor(bitLen / 0x100000000), false);

  const rotr = (x, n) => (x >>> n) | (x << (32 - n));
  const w = new Uint32Array(64);

  for (let offset = 0; offset < padded.length; offset += 64) {
    for (let i = 0; i < 16; i++) w[i] = view.getUint32(offset + i * 4, false);
    for (let i = 16; i < 64; i++) {
      const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
      const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
    }

    let [a, b, c, d, e, f, g, h] = H;
    for (let i = 0; i < 64; i++) {
      const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const ch = (e & f) ^ (~e & g);
      const temp1 = (h + S1 + ch + SHA256_K[i] + w[i]) | 0;
      const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const temp2 = (S0 + maj) | 0;
      h = g; g = f; f = e; e = (d + temp1) | 0;
      d = c; c = b; b = a; a = (temp1 + temp2) | 0;
    }
    H[0] = (H[0] + a) | 0; H[1] = (H[1] + b) | 0; H[2] = (H[2] + c) | 0; H[3] = (H[3] + d) | 0;
    H[4] = (H[4] + e) | 0; H[5] = (H[5] + f) | 0; H[6] = (H[6] + g) | 0; H[7] = (H[7] + h) | 0;
  }

  const out = new Uint8Array(32);
  const outView = new DataView(out.buffer);
  for (let i = 0; i < 8; i++) outView.setUint32(i * 4, H[i], false);
  return out;
}

function sha256Hex(text) {
  return bytesToHex(sha256(new TextEncoder().encode(text)));
}

// RFC 2104 HMAC, built on the sha256() above.
function hmacSha256Hex(secretHex, message) {
  const blockSize = 64;
  let key = hexToBytes(secretHex);
  if (key.length > blockSize) key = sha256(key);
  if (key.length < blockSize) {
    const padded = new Uint8Array(blockSize);
    padded.set(key);
    key = padded;
  }

  const oKeyPad = new Uint8Array(blockSize);
  const iKeyPad = new Uint8Array(blockSize);
  for (let i = 0; i < blockSize; i++) {
    oKeyPad[i] = key[i] ^ 0x5c;
    iKeyPad[i] = key[i] ^ 0x36;
  }

  const msgBytes = new TextEncoder().encode(message);
  const inner = sha256(new Uint8Array([...iKeyPad, ...msgBytes]));
  return bytesToHex(sha256(new Uint8Array([...oKeyPad, ...inner])));
}

// ---------------------------------------------------------------------------
// Credentials
// ---------------------------------------------------------------------------

function loadCredentials() {
  try {
    return JSON.parse(localStorage.getItem(STORAGE_KEY));
  } catch {
    return null;
  }
}

function saveCredentials(creds) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(creds));
}

function clearCredentials() {
  localStorage.removeItem(STORAGE_KEY);
}

// ---------------------------------------------------------------------------
// Signed requests
// ---------------------------------------------------------------------------

// `path` already includes any query string; body is a JS object or
// undefined. Throws on a transport failure; returns {status, json} for
// anything the board answered, including a non-2xx status, so callers can
// show *why* a request was refused rather than treating every failure alike.
async function apiFetch(method, path, body) {
  const creds = loadCredentials();
  if (!creds) throw new Error("not paired");

  const bodyText = body !== undefined ? JSON.stringify(body) : "";
  const ts = Math.floor(Date.now() / 1000);
  const nonce = randomHex(8);
  const bodyHash = await sha256Hex(bodyText);
  const canonical = [method, path, ts, nonce, bodyHash].join("\n");
  const sig = await hmacSha256Hex(creds.secret, canonical);

  const headers = {
    Authorization: `HMAC id=${creds.client_id},ts=${ts},nonce=${nonce},sig=${sig}`,
  };
  if (bodyText) headers["Content-Type"] = "application/json";

  const response = await fetch(path, { method, headers, body: bodyText || undefined });
  const text = await response.text();
  let json = null;
  try {
    json = text ? JSON.parse(text) : null;
  } catch {
    // Not every response is JSON (there isn't one here today, but a caller
    // should not throw over it).
  }
  return { status: response.status, json };
}

// ---------------------------------------------------------------------------
// Pairing
// ---------------------------------------------------------------------------

async function pair() {
  const response = await fetch("/pair", { method: "POST" });
  const json = await response.json();
  if (response.status !== 200) {
    throw new Error(json && json.error ? json.error : `pairing failed (${response.status})`);
  }
  saveCredentials({ client_id: json.client_id, secret: json.secret });
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

const appsPanel = document.getElementById("apps-panel");
const pairingPanel = document.getElementById("pairing-panel");
const pairingError = document.getElementById("pairing-error");
const connStatus = document.getElementById("conn-status");
const boardInfo = document.getElementById("board-info");
const unpairButton = document.getElementById("unpair-button");

// Debounced per (appIndex, key) so a slider dragged across a colour picker
// posts once it settles rather than once per "input" event -- the same
// reasoning as the firmware's own settingsQuiet, just client-side.
const pendingWrites = new Map();
function debouncedApplySetting(appIndex, key, value) {
  const mapKey = `${appIndex}:${key}`;
  clearTimeout(pendingWrites.get(mapKey));
  pendingWrites.set(
    mapKey,
    setTimeout(() => applySetting(appIndex, key, value), 150));
}

async function applySetting(appIndex, key, value) {
  const { status, json } = await apiFetch("POST", `/api/apps/${appIndex}/settings`, { [key]: value });
  if (status !== 200) {
    console.error("setting rejected", key, json);
  }
}

async function switchApp(index) {
  await apiFetch("POST", "/api/app", { index });
}

function colorIntToHex(value) {
  return "#" + (value >>> 0).toString(16).padStart(6, "0");
}

function colorHexToInt(hex) {
  return parseInt(hex.slice(1), 16);
}

// One input element for one setting descriptor, wired to post its own
// changes. Exactly four cases because SettingType has exactly four
// members -- a fifth would fail loudly here rather than render as nothing.
function buildSettingField(appIndex, descriptor, value) {
  const wrapper = document.createElement("label");
  wrapper.className = "field";
  wrapper.dataset.key = descriptor.key;

  const labelText = document.createElement("span");
  labelText.textContent = descriptor.label;
  wrapper.appendChild(labelText);

  let input;
  switch (descriptor.type) {
    case "bool":
      input = document.createElement("input");
      input.type = "checkbox";
      input.checked = Boolean(value);
      input.addEventListener("change", () => applySetting(appIndex, descriptor.key, input.checked));
      break;

    case "int":
      input = document.createElement("input");
      input.type = "number";
      input.min = descriptor.min;
      input.max = descriptor.max;
      input.value = value;
      input.addEventListener("input", () =>
        debouncedApplySetting(appIndex, descriptor.key, Number(input.value)));
      break;

    case "color":
      input = document.createElement("input");
      input.type = "color";
      input.value = colorIntToHex(value);
      input.addEventListener("input", () =>
        debouncedApplySetting(appIndex, descriptor.key, colorHexToInt(input.value)));
      break;

    case "string":
      input = document.createElement("input");
      input.type = "text";
      input.maxLength = descriptor.max_len - 1;  // room for the NUL the firmware caps at
      input.value = value;
      input.addEventListener("input", () =>
        debouncedApplySetting(appIndex, descriptor.key, input.value));
      break;

    default:
      input = document.createElement("em");
      input.textContent = `(unsupported setting type "${descriptor.type}")`;
  }

  wrapper.appendChild(input);
  return wrapper;
}

// Rebuilds the whole apps panel from a fresh GET /api/apps + one settings
// fetch per app. Simple rather than incremental: this board runs at most a
// handful of apps with a handful of settings each, so re-rendering
// everything on every full refresh costs nothing a user would notice, and
// it is the one code path that can never drift from what the schema says.
async function renderApps(schema) {
  appsPanel.innerHTML = "";
  appsPanel.hidden = false;

  for (const app of schema.apps) {
    const section = document.createElement("section");
    section.className = "app";
    section.dataset.appIndex = app.index;
    if (app.index === schema.active_index) section.classList.add("active");

    const heading = document.createElement("h2");
    heading.textContent = app.name;
    section.appendChild(heading);

    const switchButton = document.createElement("button");
    switchButton.textContent =
      app.index === schema.active_index ? "Showing now" : "Switch to this app";
    switchButton.disabled = app.index === schema.active_index;
    switchButton.addEventListener("click", () => switchApp(app.index));
    section.appendChild(switchButton);

    if (app.settings.length > 0) {
      const { json: values } = await apiFetch("GET", `/api/apps/${app.index}/settings`);
      const form = document.createElement("div");
      form.className = "settings";
      for (const descriptor of app.settings) {
        form.appendChild(buildSettingField(app.index, descriptor, values ? values[descriptor.key] : undefined));
      }
      section.appendChild(form);
    }

    appsPanel.appendChild(section);
  }
}

async function refreshApps() {
  const { json: schema } = await apiFetch("GET", "/api/apps");
  if (schema) await renderApps(schema);
}

// Applies one live event without a full refetch, for the common cases;
// falls back to refreshApps() for anything it does not specifically know
// how to patch in place, so an event this code has not been taught about
// still ends up correct a moment later rather than silently ignored.
function applyEvent(event) {
  if (event.event === "app_switched") {
    for (const section of appsPanel.querySelectorAll(".app")) {
      const isActive = Number(section.dataset.appIndex) === event.app;
      section.classList.toggle("active", isActive);
      const button = section.querySelector("button");
      button.disabled = isActive;
      button.textContent = isActive ? "Showing now" : "Switch to this app";
    }
    return;
  }

  if (event.event === "setting_changed") {
    const section = appsPanel.querySelector(`.app[data-app-index="${event.app}"]`);
    const field = section && section.querySelector(`.field[data-key="${CSS.escape(event.key)}"] input`);
    if (!field) {
      refreshApps();
      return;
    }
    // Skip the field the user is actively editing, so an event echoing this
    // tab's own change does not fight the cursor mid-edit.
    if (document.activeElement === field) return;

    if (field.type === "checkbox") field.checked = Boolean(event.value);
    else if (field.type === "color") field.value = colorIntToHex(event.value);
    else field.value = event.value;
  }
}

// ---------------------------------------------------------------------------
// Live sync
// ---------------------------------------------------------------------------

let ws = null;
let wsRetryMs = 1000;

function setConnStatus(state, text) {
  connStatus.className = `pill pill-${state}`;
  connStatus.textContent = text;
}

async function connectWs() {
  try {
    const { status, json } = await apiFetch("POST", "/api/ws-ticket");
    if (status !== 200) throw new Error("could not get a ticket");

    const url = new URL("/api/ws", location.href);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    url.search = `?ticket=${json.ticket}`;

    ws = new WebSocket(url);
    ws.addEventListener("open", () => {
      wsRetryMs = 1000;
      setConnStatus("ok", "live");
    });
    ws.addEventListener("message", (msg) => {
      try {
        applyEvent(JSON.parse(msg.data));
      } catch (err) {
        console.error("bad event", err);
      }
    });
    ws.addEventListener("close", scheduleWsRetry);
    ws.addEventListener("error", () => ws.close());
  } catch (err) {
    console.error("ws-ticket failed", err);
    scheduleWsRetry();
  }
}

function scheduleWsRetry() {
  setConnStatus("warn", "reconnecting\u2026");
  setTimeout(connectWs, wsRetryMs);
  wsRetryMs = Math.min(wsRetryMs * 2, 30000);
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

async function showBoardInfo() {
  const response = await fetch("/");
  const info = await response.json();
  boardInfo.textContent = `${info.device} ${info.firmware_version}`;
  return info;
}

async function boot() {
  const info = await showBoardInfo();

  if (!loadCredentials()) {
    pairingPanel.hidden = false;
    setConnStatus("warn", "not paired");
    return;
  }

  unpairButton.hidden = false;
  try {
    await refreshApps();
    setConnStatus("warn", "connecting\u2026");
    connectWs();
  } catch (err) {
    if (String(err).includes("401")) {
      clearCredentials();
      location.reload();
      return;
    }
    console.error(err);
    setConnStatus("bad", "error");
  }

  void info;
}

document.getElementById("pair-button").addEventListener("click", async () => {
  pairingError.hidden = true;
  try {
    await pair();
    location.reload();
  } catch (err) {
    pairingError.textContent = err.message;
    pairingError.hidden = false;
  }
});

unpairButton.addEventListener("click", () => {
  clearCredentials();
  location.reload();
});

boot();
