// Janus Lock — custom Lovelace card. Served + auto-loaded by the januslock integration.
// Usage:  type: custom:janus-lock-card   (entity is auto-detected; or set entity:/node:)
const DAYS = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"];

class JanusLockCard extends HTMLElement {
  setConfig(config) {
    this._config = config || {};
    this._node = this._config.node || "januslock";
    this._built = false;
  }

  set hass(hass) {
    this._hass = hass;
    this._entity = this._config.entity || this._findEntity(hass);
    if (!this._built) this._build();
    this._update();
  }

  getCardSize() { return 8; }

  _findEntity(hass) {
    for (const id in hass.states) {
      if (id.startsWith("sensor.") && id.endsWith("_day_code") &&
          hass.states[id].attributes && "pins" in hass.states[id].attributes) {
        return id;
      }
    }
    return null;
  }

  _st() { return this._entity ? this._hass.states[this._entity] : null; }

  _build() {
    this._built = true;
    this.innerHTML = `
      <ha-card header="Janus Lock — PIN codes">
        <style>
          .jl { padding: 0 16px 16px; }
          .jl h4 { margin: 14px 0 6px; color: var(--primary-text-color); }
          .jl .muted { color: var(--secondary-text-color); font-size: 0.9em; }
          .jl .row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin: 6px 0; }
          .jl input[type=text], .jl input[type=time], .jl input[type=date] {
            padding: 8px; border: 1px solid var(--divider-color); border-radius: 8px;
            background: var(--card-background-color); color: var(--primary-text-color); font: inherit;
          }
          .jl input[type=text] { width: 8em; }
          .jl label { color: var(--primary-text-color); display: inline-flex; align-items: center; gap: 4px; }
          .jl .days button { border: 1px solid var(--divider-color); border-radius: 999px;
            padding: 4px 9px; background: var(--card-background-color); color: var(--primary-text-color); cursor: pointer; }
          .jl .days button.on { background: var(--primary-color); color: var(--text-primary-color, #fff); border-color: var(--primary-color); }
          .jl .btn { background: var(--primary-color); color: var(--text-primary-color, #fff); border: none;
            border-radius: 8px; padding: 8px 14px; cursor: pointer; font: inherit; }
          .jl .btn.sec { background: transparent; color: var(--primary-color); border: 1px solid var(--primary-color); }
          .jl .btn.danger { background: transparent; color: var(--error-color); border: 1px solid var(--error-color); padding: 4px 10px; }
          .jl ul { list-style: none; margin: 0; padding: 0; }
          .jl li { display: flex; justify-content: space-between; align-items: center; gap: 8px;
            padding: 7px 0; border-bottom: 1px solid var(--divider-color); }
          .jl li:last-child { border-bottom: none; }
          .jl .tag { font-size: 0.8em; color: var(--secondary-text-color); }
          .jl .status { margin-top: 8px; min-height: 1.2em; font-size: 0.9em; }
          .jl .status.err { color: var(--error-color); }
          .jl .status.ok { color: var(--success-color, #43a047); }
          .jl details summary { cursor: pointer; color: var(--secondary-text-color); margin: 8px 0; }
          .jl .mono { font-family: var(--code-font-family, monospace); }
        </style>
        <div class="jl">
          <div class="warn muted"></div>
          <h4>Add a code</h4>
          <div class="row">
            <input type="text" class="pin" inputmode="numeric" placeholder="PIN e.g. 1234">
            <label><input type="checkbox" class="once"> One-time</label>
          </div>
          <details>
            <summary>Time / date / weekday limits (optional)</summary>
            <div class="row">
              <span class="muted">Daily:</span>
              <input type="time" class="tf"> <span class="muted">–</span> <input type="time" class="tt">
            </div>
            <div class="row">
              <span class="muted">Dates:</span>
              <input type="date" class="df"> <span class="muted">–</span> <input type="date" class="dt">
            </div>
            <div class="row days"></div>
          </details>
          <div class="row"><button class="btn add">Add code</button></div>
          <div class="status add-status"></div>

          <h4>Codes</h4>
          <ul class="pins"></ul>
          <h4>Fingerprints</h4>
          <ul class="fps"></ul>

          <h4>Recent unlocks
            <button class="btn sec sync" style="float:right;font-size:0.85em;padding:4px 10px;">Sync now</button>
          </h4>
          <ul class="hist"></ul>
          <div class="status sync-status"></div>
        </div>
      </ha-card>`;

    const q = (s) => this.querySelector(s);
    this._els = {
      warn: q(".warn"), pin: q(".pin"), once: q(".once"),
      tf: q(".tf"), tt: q(".tt"), df: q(".df"), dt: q(".dt"), days: q(".days"),
      addStatus: q(".add-status"), pins: q(".pins"), fps: q(".fps"),
      hist: q(".hist"), syncStatus: q(".sync-status"),
    };
    // weekday chips
    this._dayState = {};
    DAYS.forEach((d) => {
      const b = document.createElement("button");
      b.textContent = d.slice(0, 2).replace(/^\w/, (c) => c.toUpperCase());
      b.onclick = () => { this._dayState[d] = !this._dayState[d]; b.classList.toggle("on", this._dayState[d]); };
      this._els.days.appendChild(b);
    });
    q(".add").onclick = () => this._add();
    q(".sync").onclick = () => this._sync();
  }

  _add() {
    const e = this._els;
    const code = (e.pin.value || "").trim();
    if (!/^\d+$/.test(code)) { this._say(e.addStatus, "Enter a numeric PIN.", true); return; }
    const data = { passcode: code, one_time: e.once.checked, esphome_node: this._node };
    if (e.tf.value && e.tt.value) { data.time_from = e.tf.value; data.time_to = e.tt.value; }
    if (e.df.value && e.dt.value) { data.date_from = e.df.value; data.date_to = e.dt.value; }
    const days = DAYS.filter((d) => this._dayState[d]);
    if (days.length) data.weekdays = days;
    this._say(e.addStatus, "Adding…");
    this._hass.callService("januslock", "add_pin", data)
      .then(() => { this._say(e.addStatus, `Added code ${code}.`, false, true); e.pin.value = ""; })
      .catch((err) => this._say(e.addStatus, "Failed: " + (err.message || err), true));
  }

  _remove(tokenId, code) {
    if (!confirm(`Remove code ${code} (token ${tokenId})?`)) return;
    this._say(this._els.syncStatus, `Removing ${code}…`);
    this._hass.callService("januslock", "remove_pin", { token_id: tokenId, esphome_node: this._node })
      .then(() => this._say(this._els.syncStatus, `Removed ${code}.`, false, true))
      .catch((err) => this._say(this._els.syncStatus, "Failed: " + (err.message || err), true));
  }

  _sync() {
    this._say(this._els.syncStatus, "Reading the lock's history…");
    this._hass.callService("januslock", "sync_history", { esphome_node: this._node })
      .then(() => this._say(this._els.syncStatus, "History syncing; used one-time codes clean up shortly.", false, true))
      .catch((err) => this._say(this._els.syncStatus, "Failed: " + (err.message || err), true));
  }

  _say(el, msg, err, ok) {
    el.textContent = msg;
    el.className = "status" + (err ? " err" : ok ? " ok" : "");
  }

  _update() {
    if (!this._els) return;
    const st = this._st();
    if (!st) { this._els.warn.textContent = "No Janus Lock day-code sensor found."; return; }
    this._els.warn.textContent = "";
    const a = st.attributes || {};
    const pins = a.pins || [], fps = a.fingerprints || [], hist = a.recent_unlocks || [];

    this._els.pins.innerHTML = pins.length ? "" : "<li class='muted'>No PIN codes.</li>";
    pins.forEach((p) => {
      const bits = [p.type];
      if (p.time) bits.push(p.time);
      if (p.date) bits.push(p.date);
      if (p.weekdays) bits.push(p.weekdays.map((d) => d.slice(0, 2)).join(","));
      const li = document.createElement("li");
      li.innerHTML = `<span><b class="mono">${p.code}</b> <span class="tag">${bits.join(" · ")}</span></span>`;
      const btn = document.createElement("button");
      btn.className = "btn danger"; btn.textContent = "Remove";
      btn.onclick = () => this._remove(p.token_id, p.code);
      li.appendChild(btn);
      this._els.pins.appendChild(li);
    });

    this._els.fps.innerHTML = fps.length ? "" : "<li class='muted'>No fingerprints.</li>";
    fps.forEach((f) => {
      const li = document.createElement("li");
      li.innerHTML = `<span>${f.name || "(unnamed)"}</span><span class="tag">token ${f.token_id}</span>`;
      this._els.fps.appendChild(li);
    });

    this._els.hist.innerHTML = hist.length ? "" : "<li class='muted'>No recent unlocks synced. Use “Sync now”.</li>";
    hist.slice().reverse().forEach((h) => {
      const li = document.createElement("li");
      li.innerHTML = `<span class="mono">${h.date || ""}</span><span class="tag">token ${h.tokenId}</span>`;
      this._els.hist.appendChild(li);
    });
  }
}

if (!customElements.get("janus-lock-card")) {
  customElements.define("janus-lock-card", JanusLockCard);
  window.customCards = window.customCards || [];
  window.customCards.push({
    type: "janus-lock-card",
    name: "Janus Lock Card",
    description: "Manage Janus lock PIN codes, fingerprints and unlock history.",
  });
  console.info("%c JANUS-LOCK-CARD %c loaded ", "color:#fff;background:#3949ab", "");
}
