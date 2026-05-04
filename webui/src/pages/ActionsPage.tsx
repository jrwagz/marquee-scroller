import { signal } from "@preact/signals";
import {
  postRefresh,
  postRestart,
  postSystemReset,
  postForgetWifi,
} from "../api";

type ActionState = "idle" | "loading" | "ok" | "error";

const refreshState = signal<ActionState>("idle");
const refreshMsg = signal("");
const restartState = signal<ActionState>("idle");
const restartCountdown = signal(0);
const resetState = signal<ActionState>("idle");
const resetMsg = signal("");
const forgetState = signal<ActionState>("idle");
const forgetMsg = signal("");

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

async function doSystemReset() {
  if (
    !window.confirm(
      "Reset all settings to defaults? This deletes /conf.txt and reboots " +
        "the device. WiFi credentials are preserved.",
    )
  ) {
    return;
  }
  resetState.value = "loading";
  resetMsg.value = "";
  try {
    await postSystemReset();
    resetState.value = "ok";
    resetMsg.value = "Settings cleared — device is restarting.";
  } catch (e) {
    resetState.value = "error";
    resetMsg.value = String(e);
    setTimeout(() => {
      resetState.value = "idle";
    }, 5000);
  }
}

async function doForgetWifi() {
  if (
    !window.confirm(
      "Forget the saved WiFi network? The device will reboot into AP mode " +
        "(SSID: CLOCK-<chip-id>) so you can configure new credentials.",
    )
  ) {
    return;
  }
  forgetState.value = "loading";
  forgetMsg.value = "";
  try {
    await postForgetWifi();
    forgetState.value = "ok";
    forgetMsg.value = "WiFi cleared — device is restarting into AP mode.";
  } catch (e) {
    forgetState.value = "error";
    forgetMsg.value = String(e);
    setTimeout(() => {
      forgetState.value = "idle";
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
              <>
                <span class="spinner" /> Refreshing…
              </>
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
            <span class="error-msg">
              Restart failed — check device connection.
            </span>
          )}
        </div>
      </div>

      <div class="action-card">
        <div class="action-info">
          <strong>Reset Settings</strong>
          <p>
            Delete <code>/conf.txt</code> and reboot. All settings revert
            to compile-time defaults; WiFi credentials are preserved.
          </p>
        </div>
        <div class="action-controls">
          {resetState.value === "idle" && (
            <button class="btn btn-danger" onClick={doSystemReset}>
              Reset Settings
            </button>
          )}
          {resetState.value === "loading" && (
            <button class="btn btn-danger" disabled>
              <span class="spinner" /> Resetting…
            </button>
          )}
          {resetState.value === "ok" && (
            <span class="muted">{resetMsg.value}</span>
          )}
          {resetState.value === "error" && (
            <span class="error-msg">{resetMsg.value}</span>
          )}
        </div>
      </div>

      <div class="action-card">
        <div class="action-info">
          <strong>Forget WiFi</strong>
          <p>
            Clear saved WiFi credentials and reboot. The device comes up
            in AP mode (SSID <code>CLOCK-&lt;chip-id&gt;</code>) so you
            can configure a new network.
          </p>
        </div>
        <div class="action-controls">
          {forgetState.value === "idle" && (
            <button class="btn btn-danger" onClick={doForgetWifi}>
              Forget WiFi
            </button>
          )}
          {forgetState.value === "loading" && (
            <button class="btn btn-danger" disabled>
              <span class="spinner" /> Clearing…
            </button>
          )}
          {forgetState.value === "ok" && (
            <span class="muted">{forgetMsg.value}</span>
          )}
          {forgetState.value === "error" && (
            <span class="error-msg">{forgetMsg.value}</span>
          )}
        </div>
      </div>

      <div class="action-card">
        <div class="action-info">
          <strong>Firmware Update</strong>
          <p>
            Upload a firmware <code>.bin</code>, flash from a URL, or
            push the SPA bundle (<code>littlefs.bin</code>). All three
            pages live outside the SPA.
          </p>
        </div>
        <div class="action-controls">
          <a class="btn" href="/update">
            Upload .bin
          </a>
          <a class="btn" href="/updateFromUrl">
            From URL
          </a>
          <a class="btn" href="/updatefs">
            LittleFS
          </a>
        </div>
      </div>
    </div>
  );
}
