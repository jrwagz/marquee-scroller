// Empty SPA shell. Real pages land in subsequent PRs — this one just proves
// the asset pipeline (LittleFS upload, gzip serving, Vite/Preact toolchain).
export function App() {
  return (
    <main class="container">
      <h1>WagFam CalClock</h1>
      <p>SPA shell loaded. Pages coming soon.</p>
      <p class="muted">
        For now, the legacy UI lives at{" "}
        <a href="/">/</a> and{" "}
        <a href="/configure">/configure</a>.
      </p>
    </main>
  );
}
