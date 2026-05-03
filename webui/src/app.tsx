import { signal } from "@preact/signals";
import { StatusPage } from "./pages/StatusPage";
import { SettingsPage } from "./pages/SettingsPage";
import { ActionsPage } from "./pages/ActionsPage";

type Tab = "status" | "settings" | "actions";

const activeTab = signal<Tab>("status");

const TABS: { id: Tab; label: string }[] = [
  { id: "status", label: "Status" },
  { id: "settings", label: "Settings" },
  { id: "actions", label: "Actions" },
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
      {activeTab.value === "status" && <StatusPage />}
      {activeTab.value === "settings" && <SettingsPage />}
      {activeTab.value === "actions" && <ActionsPage />}
    </main>
  );
}
