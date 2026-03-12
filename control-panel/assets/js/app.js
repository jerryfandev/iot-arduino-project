(() => {
  const storageKey = "esp-control-panel:v1";

  /** @type {{ lampOn: boolean }} */
  let state = { lampOn: false };

  const el = {
    badge: /** @type {HTMLSpanElement | null} */ (document.getElementById("connectionBadge")),
    toggle: /** @type {HTMLInputElement | null} */ (document.getElementById("lampToggle")),
    button: /** @type {HTMLButtonElement | null} */ (document.getElementById("lampButton")),
    statusText: /** @type {HTMLElement | null} */ (document.getElementById("lampStatusText")),
    debug: /** @type {HTMLElement | null} */ (document.getElementById("debugState")),
    reset: /** @type {HTMLButtonElement | null} */ (document.getElementById("resetUiBtn")),
  };

  function loadState() {
    try {
      const raw = localStorage.getItem(storageKey);
      if (!raw) return;
      const parsed = JSON.parse(raw);
      if (typeof parsed?.lampOn === "boolean") state.lampOn = parsed.lampOn;
    } catch {
      // ignore
    }
  }

  function saveState() {
    try {
      localStorage.setItem(storageKey, JSON.stringify(state));
    } catch {
      // ignore
    }
  }

  function setBadgeOffline() {
    if (!el.badge) return;
    el.badge.classList.remove("text-bg-success");
    el.badge.classList.add("text-bg-secondary");
    el.badge.textContent = "Offline";
  }

  function render() {
    if (el.toggle) el.toggle.checked = state.lampOn;

    if (el.button) {
      el.button.textContent = state.lampOn ? "Tắt" : "Bật";
      el.button.classList.toggle("btn-danger", state.lampOn);
      el.button.classList.toggle("btn-primary", !state.lampOn);
    }

    if (el.statusText) {
      el.statusText.textContent = state.lampOn ? "Đang bật" : "Đang tắt";
    }

    if (el.debug) {
      el.debug.textContent = `lamp=${state.lampOn ? "on" : "off"}`;
    }

    setBadgeOffline();
  }

  async function sendLampState(nextLampOn) {
    // Placeholder: gắn API/WebSocket về ESP ở đây sau này.
    // Ví dụ: await fetch(`/api/lamp`, { method: "POST", body: JSON.stringify({ on: nextLampOn }) })
    await new Promise((r) => setTimeout(r, 80));
    return { ok: true };
  }

  async function setLamp(nextLampOn) {
    const prev = state.lampOn;
    state.lampOn = nextLampOn;
    render();

    const res = await sendLampState(nextLampOn);
    if (!res?.ok) {
      state.lampOn = prev;
      render();
      return;
    }

    saveState();
  }

  function wireEvents() {
    el.toggle?.addEventListener("change", () => {
      void setLamp(Boolean(el.toggle?.checked));
    });

    el.button?.addEventListener("click", () => {
      void setLamp(!state.lampOn);
    });

    el.reset?.addEventListener("click", () => {
      state = { lampOn: false };
      saveState();
      render();
    });
  }

  function init() {
    loadState();
    wireEvents();
    render();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init, { once: true });
  } else {
    init();
  }
})();

