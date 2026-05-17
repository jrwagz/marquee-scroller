import { useEffect } from "preact/hooks";
import { signal } from "@preact/signals";
import { HomePage } from "./pages/HomePage";
import { StatusPage } from "./pages/StatusPage";
import { SettingsPage } from "./pages/SettingsPage";
import { ActionsPage } from "./pages/ActionsPage";
import { SchedulesPage } from "./pages/SchedulesPage";
import { Footer } from "./pages/Footer";
import { getStatus } from "./api";

type Tab = "home" | "status" | "settings" | "actions" | "schedules";

const activeTab = signal<Tab>("home");
const familyDisplay = signal<string>("");

const TABS: { id: Tab; label: string }[] = [
  { id: "home", label: "Home" },
  { id: "status", label: "Status" },
  { id: "settings", label: "Settings" },
  { id: "actions", label: "Actions" },
  { id: "schedules", label: "Schedules" },
];

export function App() {
  useEffect(() => {
    // One-shot fetch on mount — the header label only changes when the
    // server re-tags the clock, which is rare. StatusPage already polls
    // /api/status every 30 s for everything else; the header just reads
    // the field from a single boot-time fetch.
    getStatus()
      .then((s) => {
        familyDisplay.value = s.family_display || "";
      })
      .catch(() => {
        // Untagged device or unreachable — fall back to the generic title.
      });
  }, []);
  const title = familyDisplay.value
    ? `${familyDisplay.value} Family CalClock`
    : "WagFam CalClock";
  return (
    <main class="container">
      <h1>{title}</h1>
      <nav class="tab-bar">
        {TABS.map((t) => (
          <button
            key={t.id}
            class={`tab${activeTab.value === t.id ? " active" : ""}`}
            onClick={() => {
              activeTab.value = t.id;
            }}
          >
            {t.label}
          </button>
        ))}
      </nav>
      {activeTab.value === "home" && <HomePage />}
      {activeTab.value === "status" && <StatusPage />}
      {activeTab.value === "settings" && <SettingsPage />}
      {activeTab.value === "actions" && <ActionsPage />}
      {activeTab.value === "schedules" && <SchedulesPage />}
      <Footer />
    </main>
  );
}
