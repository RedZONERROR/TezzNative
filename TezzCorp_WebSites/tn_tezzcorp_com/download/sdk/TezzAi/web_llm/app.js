const state = { token: "", user: "", sessionId: "", sessions: [], lastReply: "" };
const chatEl = document.getElementById("chat");
const sessEl = document.getElementById("sessions");
const metaEl = document.getElementById("meta");
const authEl = document.getElementById("auth-state");
const key = "tezzai.llm.sessions.v1";

function load() {
  try {
    const raw = localStorage.getItem(key) || "{}";
    const d = JSON.parse(raw);
    state.token = d.token || "";
    state.user = d.user || "";
    state.sessionId = d.sessionId || "";
    state.sessions = Array.isArray(d.sessions) ? d.sessions : [];
  } catch (_) {
    state.token = "";
    state.user = "";
    state.sessionId = "";
    state.sessions = [];
  }
}

function save() {
  localStorage.setItem(key, JSON.stringify({
    token: state.token,
    user: state.user,
    sessionId: state.sessionId,
    sessions: state.sessions
  }));
}

function stamp() {
  return new Date().toLocaleTimeString();
}

function push(role, text) {
  const box = document.createElement("div");
  box.className = "msg " + (role === "user" ? "user" : "assistant");
  box.innerHTML = '<div class="head"><span>' + role + '</span><span>' + stamp() + "</span></div>";
  const body = document.createElement("div");
  body.textContent = text || "";
  box.appendChild(body);
  chatEl.appendChild(box);
  chatEl.scrollTop = chatEl.scrollHeight;
}

function renderSessions() {
  sessEl.innerHTML = "";
  for (const s of state.sessions) {
    const d = document.createElement("div");
    d.className = "sess" + (s.id === state.sessionId ? " active" : "");
    d.textContent = s.id;
    d.onclick = () => {
      state.sessionId = s.id;
      save();
      renderSessions();
      loadHistory();
    };
    sessEl.appendChild(d);
  }
  authEl.textContent = state.user ? ("signed in as " + state.user) : "not signed in";
}

async function req(url, body) {
  const r = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {})
  });
  return await r.json();
}

async function register() {
  const u = document.getElementById("username").value.trim();
  const p = document.getElementById("password").value;
  const j = await req("/auth/register", { username: u, password: p });
  if (!j.ok) {
    metaEl.textContent = j.error || "register failed";
    return;
  }
  state.user = j.user;
  state.token = j.token;
  metaEl.textContent = "registered " + j.user;
  save();
  renderSessions();
}

async function login() {
  const u = document.getElementById("username").value.trim();
  const p = document.getElementById("password").value;
  const j = await req("/auth/login", { username: u, password: p });
  if (!j.ok) {
    metaEl.textContent = j.error || "login failed";
    return;
  }
  state.user = j.user;
  state.token = j.token;
  metaEl.textContent = "login ok";
  save();
  renderSessions();
}

async function newSession() {
  if (!state.token) {
    metaEl.textContent = "login required";
    return;
  }
  const j = await req("/chat/session/new", { token: state.token });
  if (!j.ok) {
    metaEl.textContent = j.error || "session failed";
    return;
  }
  state.sessionId = j.session_id;
  if (!state.sessions.find(x => x.id === j.session_id)) {
    state.sessions.unshift({ id: j.session_id });
  }
  metaEl.textContent = "session " + j.session_id;
  save();
  renderSessions();
  chatEl.innerHTML = "";
}

async function loadHistory() {
  if (!state.token || !state.sessionId) {
    chatEl.innerHTML = "";
    return;
  }
  const u = "/chat/history?token=" + encodeURIComponent(state.token) + "&session_id=" + encodeURIComponent(state.sessionId);
  const r = await fetch(u, { cache: "no-store" });
  const j = await r.json();
  chatEl.innerHTML = "";
  if (!j.ok || !Array.isArray(j.history)) {
    return;
  }
  for (const m of j.history) {
    push("user", m.prompt || "");
    push("assistant", m.reply || "");
  }
}

async function send() {
  const p = document.getElementById("prompt").value.trim();
  if (!p) return;
  if (!state.token) {
    metaEl.textContent = "login required";
    return;
  }
  if (!state.sessionId) {
    await newSession();
    if (!state.sessionId) return;
  }

  push("user", p);
  document.getElementById("prompt").value = "";

  const j = await req("/llm/respond", {
    token: state.token,
    session_id: state.sessionId,
    prompt: p,
    max_new: 96
  });

  if (!j.ok) {
    metaEl.textContent = j.error || "request failed";
    return;
  }

  state.sessionId = j.session_id || state.sessionId;
  if (!state.sessions.find(x => x.id === state.sessionId)) {
    state.sessions.unshift({ id: state.sessionId });
  }
  save();
  renderSessions();

  const txt = j.reply || "";
  let out = "";
  const box = document.createElement("div");
  box.className = "msg assistant";
  box.innerHTML = '<div class="head"><span>assistant</span><span>' + stamp() + "</span></div>";
  const body = document.createElement("div");
  box.appendChild(body);
  chatEl.appendChild(box);

  let i = 0;
  const step = () => {
    if (i >= txt.length) {
      state.lastReply = txt;
      return;
    }
    i += 3;
    if (i > txt.length) i = txt.length;
    out = txt.slice(0, i);
    body.textContent = out;
    chatEl.scrollTop = chatEl.scrollHeight;
    setTimeout(step, 8);
  };
  step();
  metaEl.textContent = "session " + state.sessionId;
}

document.getElementById("register").onclick = register;
document.getElementById("login").onclick = login;
document.getElementById("new-session").onclick = newSession;
document.getElementById("send").onclick = send;
document.getElementById("copy").onclick = () => {
  if (state.lastReply) navigator.clipboard.writeText(state.lastReply);
};

load();
renderSessions();
loadHistory();
