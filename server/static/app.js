"use strict";

// Web app del control remoto: un unico WebSocket (/ws/ui) por el que llega
// el estado del dispositivo y salen los comandos (ver server/app.py).

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

const state = {
  presence: null,
  status: null,
  statusAt: null,
  serverOffset: 0, // server_time - Date.now()/1000, para "hace X" coherentes
  logs: [],
  logPaused: false,
  pending: new Map(), // rid (id de peticion generado aqui) -> boton
  serverDown: false,
};
let nextRid = 1;

const COLOR_NAMES = { red: "rojo", yellow: "ámbar", green: "verde", all: "todos", off: "apagado" };
const MODE_NAMES = {
  solid: "fijo", blink_slow: "parpadeo lento", blink_fast: "parpadeo rápido",
  rotate: "rotando", rotate_fast: "rotando rápido", off: "apagado",
};
const CMD_NAMES = {
  sign_in: "Fichar entrada", sign_out: "Fichar salida", refresh: "Consultar estado",
  led_set: "Color fijo", led_auto: "Automático", reboot: "Reiniciar",
  ota_update: "Actualizar firmware", status: "Pedir estado",
};

// ---------- utilidades ----------

function now() { return Date.now() / 1000 + state.serverOffset; }

function ago(t) {
  if (!t) return "nunca";
  const s = Math.max(0, Math.round(now() - t));
  if (s < 10) return "ahora mismo";
  if (s < 60) return `hace ${s} s`;
  if (s < 3600) return `hace ${Math.round(s / 60)} min`;
  if (s < 86400) return `hace ${Math.round(s / 3600)} h`;
  return `hace ${Math.round(s / 86400)} d`;
}

function clock(t) {
  return new Date(t * 1000).toLocaleTimeString("es-ES", { hour: "2-digit", minute: "2-digit" });
}

function duration(sec) {
  const d = Math.floor(sec / 86400), h = Math.floor((sec % 86400) / 3600), m = Math.floor((sec % 3600) / 60);
  if (d) return `${d} d ${h} h`;
  if (h) return `${h} h ${m} min`;
  return `${m} min`;
}

function toast(text, isError = false) {
  const el = document.createElement("div");
  el.className = "toast" + (isError ? " error" : "");
  el.textContent = text;
  $("#toasts").appendChild(el);
  setTimeout(() => el.remove(), isError ? 6000 : 3500);
}

// ---------- WebSocket ----------

let ws = null;
let retryMs = 1000;

function connect() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(`${proto}://${location.host}/ws/ui`);
  ws.onopen = () => { retryMs = 1000; state.serverDown = false; renderPresence(); };
  ws.onmessage = (e) => handle(JSON.parse(e.data));
  ws.onclose = (e) => {
    if (e.code === 1006 && !navigator.onLine) { /* sin red: reintenta igual */ }
    state.serverDown = true;
    renderPresence();
    state.pending.forEach((btn) => setBusy(btn, false));
    state.pending.clear();
    // 401 en el upgrade (sesion caducada) se ve como cierre inmediato: se
    // comprueba con una peticion normal si hay que volver al login.
    fetch("/", { redirect: "manual" }).then((r) => {
      if (r.type === "opaqueredirect" || r.status === 302) location.href = "/login";
    }).catch(() => {});
    setTimeout(connect, retryMs);
    retryMs = Math.min(retryMs * 2, 15000);
  };
}

function send(msg) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    toast("Sin conexión con el servidor", true);
    return false;
  }
  ws.send(JSON.stringify(msg));
  return true;
}

function handle(msg) {
  switch (msg.type) {
    case "snapshot":
      state.serverOffset = msg.server_time - Date.now() / 1000;
      $("#device-name").textContent = msg.name;
      document.title = `${msg.name} · Semáforo Woffu`;
      state.presence = msg.presence;
      state.status = msg.status;
      state.statusAt = msg.status_at;
      state.logs = msg.logs;
      renderEvents(msg.events, true);
      renderAll();
      renderConsole(true);
      break;
    case "presence":
      state.presence = msg;
      renderAll();
      break;
    case "status":
      state.status = msg.status;
      state.statusAt = msg.status_at;
      renderAll();
      break;
    case "event":
      renderEvents([msg.event], false);
      break;
    case "log":
      state.logs.push(...msg.lines);
      if (state.logs.length > 3000) state.logs.splice(0, state.logs.length - 3000);
      appendConsole(msg.lines);
      break;
    case "ack": {
      // Los acks se difunden a todos los navegadores del dispositivo; solo
      // el que lanzo el comando tiene su rid en `pending`.
      const btn = state.pending.get(msg.rid);
      if (!btn) break;
      state.pending.delete(msg.rid);
      setBusy(btn, false);
      const name = CMD_NAMES[msg.cmd] || msg.cmd;
      toast(`${name}: ${msg.msg || (msg.ok ? "ok" : "error")}`, !msg.ok);
      break;
    }
  }
}

// ---------- render ----------

function renderAll() {
  renderPresence();
  renderHero();
  renderOverride();
  renderFacts();
  $("#raw-status").textContent = state.status ? JSON.stringify(state.status, null, 2) : "—";
}

function renderPresence() {
  const pill = $("#presence");
  const label = pill.querySelector("span");
  if (state.serverDown) {
    pill.className = "pill offline";
    label.textContent = "Sin conexión con el servidor";
    return;
  }
  const p = state.presence;
  if (p && p.online) {
    pill.className = "pill online";
    label.textContent = "En línea";
  } else {
    pill.className = "pill offline";
    label.textContent = p && p.last_seen ? `Desconectado · visto ${ago(p.last_seen)}` : "Desconectado";
  }
}

function renderHero() {
  const s = state.status;
  const online = state.presence && state.presence.online;
  const light = $("#light");
  const lamps = { red: light.querySelector(".red"), yellow: light.querySelector(".yellow"), green: light.querySelector(".green") };
  light.className = "light";
  Object.values(lamps).forEach((l) => { l.className = `lamp ${l.classList[1]}`; });

  if (s && s.led) {
    const { color, mode } = s.led;
    if (mode === "rotate" || mode === "rotate_fast") {
      light.classList.add("rotate", mode);
    } else if (mode !== "off") {
      const on = color === "all" ? ["red", "yellow", "green"] : [color];
      on.forEach((c) => lamps[c] && lamps[c].classList.add("on", mode));
    }
  }
  if (!online) light.classList.add("unknown");

  const st = $("#woffu-state");
  const woffu = s && s.woffu ? s.woffu.status : null;
  st.className = "state " + (woffu || "unknown");
  st.textContent = woffu === "in" ? "Dentro" : woffu === "out" ? "Fuera" : "Desconocido";

  const sub = [];
  if (s && s.woffu) {
    if (s.woffu.credentials_invalid) sub.push("Woffu rechaza el usuario/password");
    else if (s.woffu.last_poll_epoch) sub.push(`Consultado ${ago(s.woffu.last_poll_epoch)}`);
  }
  if (!online && state.statusAt) sub.push(`dato de ${ago(state.statusAt)}`);
  $("#woffu-sub").textContent = sub.join(" · ");

  let ledText = "";
  if (s) {
    if (s.mode === "off") ledText = `Semáforo apagado: ${s.mode_reason || "fuera de horario"}`;
    else if (s.led) ledText = `Semáforo: ${COLOR_NAMES[s.led.color] || s.led.color}, ${MODE_NAMES[s.led.mode] || s.led.mode}`;
  }
  $("#led-sub").textContent = ledText;
}

function renderOverride() {
  const s = state.status;
  const ov = s ? s.override : null;
  $("#override-banner").hidden = !ov;
  if (ov) {
    let text = `El semáforo muestra ${COLOR_NAMES[ov.color] || ov.color}`;
    if (ov.mode !== "solid" && ov.mode !== "off") text += ` (${MODE_NAMES[ov.mode]})`;
    text += " en vez de tu estado real.";
    if (s.mode === "off") text += " Fuera de horario sigue apagado.";
    $("#override-text").textContent = text;
  }
  $$(".swatch").forEach((b) => {
    const v = b.dataset.led;
    b.classList.toggle("selected", s ? (ov ? v === ov.color : v === "auto") : false);
  });
  if (ov && ov.mode && ov.mode.startsWith("blink")) $("#led-blink").checked = true;
}

function renderFacts() {
  const s = state.status;
  const p = state.presence || {};
  const rows = [];
  const add = (k, v) => { if (v !== undefined && v !== null && v !== "") rows.push([k, v]); };
  add("Firmware", (s && s.fw) || (p.hello && p.hello.fw));
  if (s) {
    add("Encendido", duration(s.uptime_s));
    add("Horario", s.window ? `${s.window.start} – ${s.window.end}` : null);
    add("Modo", s.mode === "active" ? "activo (consulta cada minuto)" : `apagado (${s.mode_reason || "fuera de horario"})`);
    if (s.workday && s.workday.valid) add("Hoy", s.workday.holiday ? "festivo" : s.workday.weekend ? "fin de semana" : "laborable");
    if (s.auto_sign) add("Fichaje automático", s.auto_sign.enabled ? `sí${s.auto_sign.today_start ? ` (hoy ${s.auto_sign.today_start} – ${s.auto_sign.today_end})` : ""}` : "no");
    if (s.nfc) add("NFC", !s.nfc.reader ? "sin lector" : s.nfc.card ? "tarjeta aprendida" : "sin tarjeta");
    if (s.wifi) add("WiFi", `${s.wifi.ssid} · ${s.wifi.rssi} dBm · ${s.wifi.ip}`);
    if (s.heap) add("Memoria libre", `${Math.round(s.heap.free / 1024)} KB (mín. ${Math.round(s.heap.min_free / 1024)} KB, bloque ${Math.round(s.heap.max_alloc / 1024)} KB)`);
  }
  if (p.hello && p.hello.reset_reason) add("Último reinicio", p.hello.reset_reason);
  if (p.connected_at && p.online) add("Conectado", ago(p.connected_at));
  if (state.statusAt) add("Último estado", ago(state.statusAt));

  const dl = $("#facts");
  dl.replaceChildren();
  for (const [k, v] of rows) {
    const dt = document.createElement("dt"); dt.textContent = k;
    const dd = document.createElement("dd"); dd.textContent = v;
    dl.append(dt, dd);
  }
}

function renderEvents(events, reset) {
  const list = $("#events");
  if (reset) list.replaceChildren();
  for (const ev of events) {
    const li = document.createElement("li");
    const t = document.createElement("time");
    t.textContent = clock(ev.t);
    t.title = new Date(ev.t * 1000).toLocaleString("es-ES");
    const m = document.createElement("span");
    m.className = `k-${ev.kind}`;
    m.textContent = ev.msg;
    li.append(t, m);
    list.prepend(li);
  }
  while (list.children.length > 200) list.lastChild.remove();
  $("#events-empty").hidden = list.children.length > 0;
}

// ---------- consola ----------

function lineClass(line) {
  if (/error|fallo|fallid|invalid|rechaz/i.test(line)) return "err";
  if (/registrad|conectado|correctamente/i.test(line)) return "ok";
  return "";
}

function filterMatch(line) {
  const f = $("#log-filter").value.trim().toLowerCase();
  return !f || line.toLowerCase().includes(f);
}

function lineNode(item) {
  const span = document.createElement("span");
  const cls = lineClass(item.line);
  if (cls) span.className = cls;
  span.textContent = item.line + "\n";
  return span;
}

function renderConsole() {
  const con = $("#console");
  con.replaceChildren(...state.logs.filter((l) => filterMatch(l.line)).map(lineNode));
  con.scrollTop = con.scrollHeight;
}

function appendConsole(lines) {
  if (state.logPaused) return;
  const con = $("#console");
  const atBottom = con.scrollHeight - con.scrollTop - con.clientHeight < 40;
  for (const l of lines) if (filterMatch(l.line)) con.appendChild(lineNode(l));
  while (con.childNodes.length > 3000) con.firstChild.remove();
  if (atBottom) con.scrollTop = con.scrollHeight;
}

// ---------- acciones ----------

function setBusy(btn, busy) {
  btn.disabled = busy;
  btn.classList.toggle("busy", busy);
}

function command(btn, payload) {
  if (btn.dataset.confirm && !confirm(btn.dataset.confirm)) return;
  const rid = String(nextRid++);
  if (!send({ type: "cmd", rid, ...payload })) return;
  setBusy(btn, true);
  state.pending.set(rid, btn);
  // Red de seguridad por si el ack no llega nunca (el servidor ya caduca a los 90s).
  setTimeout(() => { if (state.pending.delete(rid)) setBusy(btn, false); }, 100000);
}

document.addEventListener("click", (e) => {
  const cmdBtn = e.target.closest("[data-cmd]");
  if (cmdBtn) {
    command(cmdBtn, { cmd: cmdBtn.dataset.cmd });
    return;
  }
  const sw = e.target.closest("[data-led]");
  if (sw) {
    const color = sw.dataset.led;
    if (color === "auto") command(sw, { cmd: "led_auto" });
    else command(sw, { cmd: "led_set", color, mode: $("#led-blink").checked ? "blink_slow" : "solid" });
    return;
  }
  const tab = e.target.closest(".tab");
  if (tab) {
    $$(".tab").forEach((t) => t.classList.toggle("active", t === tab));
    $$(".tabpanel").forEach((p) => p.classList.toggle("active", p.id === `tab-${tab.dataset.tab}`));
    try { localStorage.setItem("tab", tab.dataset.tab); } catch (_) {}
    if (tab.dataset.tab === "debug") renderConsole();
  }
});

$("#led-blink").addEventListener("change", () => {
  // Cambiar el parpadeo con un color fijo ya puesto lo reaplica directamente.
  const ov = state.status && state.status.override;
  if (ov && ov.color !== "off") {
    const btn = $(`.swatch[data-led="${ov.color}"]`);
    command(btn, { cmd: "led_set", color: ov.color, mode: $("#led-blink").checked ? "blink_slow" : "solid" });
  }
});

$("#log-filter").addEventListener("input", renderConsole);
$("#log-pause").addEventListener("click", (e) => {
  state.logPaused = !state.logPaused;
  e.target.textContent = state.logPaused ? "Seguir" : "Pausar";
  if (!state.logPaused) renderConsole();
});
$("#log-copy").addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(state.logs.map((l) => l.line).join("\n"));
    toast("Log copiado");
  } catch (_) { toast("No se pudo copiar", true); }
});

try {
  const saved = localStorage.getItem("tab");
  if (saved) { const t = $(`.tab[data-tab="${saved}"]`); if (t) t.click(); }
} catch (_) {}

// refresca los "hace X" aunque no lleguen mensajes
setInterval(() => { if (state.presence) { renderPresence(); renderHero(); renderFacts(); } }, 15000);

connect();
