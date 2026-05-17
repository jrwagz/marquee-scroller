import { signal } from "@preact/signals";
import { HomePage } from "./pages/HomePage";
import { StatusPage } from "./pages/StatusPage";
import { SettingsPage } from "./pages/SettingsPage";
import { ActionsPage } from "./pages/ActionsPage";
import { SchedulesPage } from "./pages/SchedulesPage";
import { Footer } from "./pages/Footer";

type Tab = "home" | "status" | "settings" | "actions" | "schedules";

const activeTab = signal<Tab>("home");

const TABS: { id: Tab; label: string }[] = [
  { id: "home", label: "Home" },
  { id: "status", label: "Status" },
  { id: "settings", label: "Settings" },
  { id: "actions", label: "Actions" },
  { id: "schedules", label: "Schedules" },
];

export function App() {
  return (
    <main class="container">
      <h1>WagFam CalClock</h1>
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
