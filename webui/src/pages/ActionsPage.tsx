import { signal } from "@preact/signals";
import { postRefresh, postRestart } from "../api";

type ActionState = "idle" | "loading" | "ok" | "error";

const refreshState = signal<ActionState>("idle");
const refreshMsg = signal("");
const restartState = signal<ActionState>("idle");
const restartCountdown = signal(0);

async function doRefresh() {
  refreshState.value = "loading";
  refreshMsg.value = "";
  try {
    await postRefresh();
    refreshState.value = "ok";
    refreshMsg.value = "Refresh queued — data will update shortly.";
    setTimeout(() => {
      refreshState.value = "idle";
      refreshMsg.value = "";
    }, 4000);
  } catch (e) {
    refreshState.value = "error";
    refreshMsg.value = String(e);
    setTimeout(() => {
      refreshState.value = "idle";
    }, 5000);
  }
}

async function doRestart() {
  restartState.value = "loading";
  try {
    await postRestart();
    restartState.value = "ok";
    restartCountdown.value = 10;
    const t = setInterval(() => {
      restartCountdown.value -= 1;
      if (restartCountdown.value <= 0) {
        clearInterval(t);
        restartState.value = "idle";
      }
    }, 1000);
  } catch (e) {
    restartState.value = "error";
    setTimeout(() => {
      restartState.value = "idle";
    }, 5000);
  }
}

export function ActionsPage() {
  return (
    <div>
      <div class="action-card">
        <div class="action-info">
          <strong>Force Refresh</strong>
          <p>Pull fresh weather and calendar data immediately.</p>
        </div>
        <div class="action-controls">
          <button
            class="btn"
            disabled={refreshState.value === "loading"}
            onClick={doRefresh}
          >
            {refreshState.value === "loading" ? (
              <><span class="spinner" /> Refreshing…</>
            ) : (
              "Refresh Now"
            )}
          </button>
          {refreshState.value === "ok" && (
            <span class="save-ok">{refreshMsg.value}</span>
          )}
          {refreshState.value === "error" && (
            <span class="error-msg">{refreshMsg.value}</span>
          )}
        </div>
      </div>

      <div class="action-card">
        <div class="action-info">
          <strong>Restart Device</strong>
          <p>Reboot the clock. It will reconnect to WiFi automatically.</p>
        </div>
        <div class="action-controls">
          {restartState.value === "idle" && (
            <button class="btn btn-danger" onClick={doRestart}>
              Restart
            </button>
          )}
          {restartState.value === "loading" && (
            <button class="btn btn-danger" disabled>
              <span class="spinner" /> Sending…
            </button>
          )}
          {restartState.value === "ok" && (
            <span class="muted">
              Restarting… reconnect in ~{restartCountdown.value} s
            </span>
          )}
          {restartState.value === "error" && (
            <span class="error-msg">Restart failed — check device connection.</span>
          )}
        </div>
      </div>

      <p class="muted" style={{ marginTop: "1.5rem", fontSize: "0.8rem" }}>
        Legacy web interface:{" "}
        <a href="/">Home</a> · <a href="/configure">Configure</a>
      </p>
    </div>
  );
}
