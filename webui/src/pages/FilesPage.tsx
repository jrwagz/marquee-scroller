// Files tab — browse + preview + download anything on LittleFS.
//
// The motivation is iterating on the artifacts the discovery + ARP scans
// dump (e.g. /tasmota_discovered.json, /lan_arp.json) before deciding to
// ship them up to the server. Endpoints used:
//
//   GET /api/fs/list                 — list every file with size
//   GET /api/fs/raw?path=/X          — stream raw bytes for view + download
//   DELETE /api/fs/delete?path=/X    — remove (refuses protected paths)
//
// Download is a client-side Blob trick rather than a server-side
// Content-Disposition: keeps the firmware path simple and lets the SPA
// own the filename derivation.

import { useEffect } from "preact/hooks";
import { signal, computed } from "@preact/signals";

interface FileEntry {
  name: string;
  size: number;
}

const files = signal<FileEntry[]>([]);
const selectedPath = signal<string | null>(null);
const previewContent = signal<string>("");
const previewError = signal<string | null>(null);
const previewLoading = signal(false);
const listError = signal<string | null>(null);
const listLoading = signal(false);
const action = signal<string | null>(null);

const totalBytes = computed(() =>
  files.value.reduce((acc, f) => acc + f.size, 0),
);

function formatSize(n: number): string {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / 1024 / 1024).toFixed(2)} MB`;
}

function basename(path: string): string {
  const i = path.lastIndexOf("/");
  return i >= 0 ? path.slice(i + 1) : path;
}

function isLikelyJson(path: string, content: string): boolean {
  if (path.endsWith(".json")) return true;
  const t = content.trimStart();
  return t.startsWith("{") || t.startsWith("[");
}

function prettyJsonOrRaw(content: string, path: string): string {
  if (!isLikelyJson(path, content)) return content;
  try {
    return JSON.stringify(JSON.parse(content), null, 2);
  } catch {
    return content;
  }
}

async function loadList(): Promise<void> {
  listLoading.value = true;
  listError.value = null;
  try {
    const res = await fetch("/api/fs/list");
    if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
    const json = (await res.json()) as { files: FileEntry[] };
    // Sort by name for stable display.
    files.value = json.files.slice().sort((a, b) =>
      a.name.localeCompare(b.name),
    );
  } catch (e) {
    listError.value = e instanceof Error ? e.message : String(e);
  } finally {
    listLoading.value = false;
  }
}

async function loadPreview(path: string): Promise<void> {
  selectedPath.value = path;
  previewLoading.value = true;
  previewError.value = null;
  previewContent.value = "";
  try {
    const res = await fetch(
      `/api/fs/raw?path=${encodeURIComponent(path)}`,
    );
    if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
    previewContent.value = await res.text();
  } catch (e) {
    previewError.value = e instanceof Error ? e.message : String(e);
  } finally {
    previewLoading.value = false;
  }
}

async function downloadFile(path: string): Promise<void> {
  action.value = `Downloading ${path}…`;
  try {
    const res = await fetch(
      `/api/fs/raw?path=${encodeURIComponent(path)}`,
    );
    if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = basename(path);
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    action.value = `Downloaded ${basename(path)}`;
  } catch (e) {
    action.value = `Download failed: ${e instanceof Error ? e.message : String(e)}`;
  }
}

async function deleteFile(path: string): Promise<void> {
  if (!confirm(`Delete ${path} from LittleFS? This cannot be undone.`)) return;
  action.value = `Deleting ${path}…`;
  try {
    const res = await fetch(
      `/api/fs/delete?path=${encodeURIComponent(path)}`,
      { method: "DELETE" },
    );
    if (!res.ok) {
      const body = await res.text().catch(() => res.statusText);
      throw new Error(`${res.status}: ${body}`);
    }
    action.value = `Deleted ${path}`;
    if (selectedPath.value === path) {
      selectedPath.value = null;
      previewContent.value = "";
    }
    await loadList();
  } catch (e) {
    action.value = `Delete failed: ${e instanceof Error ? e.message : String(e)}`;
  }
}

export function FilesPage() {
  useEffect(() => {
    loadList();
  }, []);

  const sel = selectedPath.value;
  const selFile = sel ? files.value.find((f) => f.name === sel) : undefined;
  const displayContent = sel
    ? prettyJsonOrRaw(previewContent.value, sel)
    : "";

  return (
    <div class="files-page">
      <section class="card">
        <header class="card-header">
          <h2>Files on LittleFS</h2>
          <div class="card-actions">
            <button class="btn" onClick={() => loadList()}>
              Refresh
            </button>
          </div>
        </header>
        <p class="muted small">
          {files.value.length} file{files.value.length === 1 ? "" : "s"},{" "}
          {formatSize(totalBytes.value)} total
        </p>
        {listError.value && (
          <p class="muted" style={{ color: "var(--red)" }}>
            {listError.value}
          </p>
        )}
        {listLoading.value && <p class="muted">Loading…</p>}
        <table class="net-table">
          <thead>
            <tr>
              <th style={{ width: "60%" }}>Path</th>
              <th style={{ width: "20%" }}>Size</th>
              <th style={{ width: "20%" }}>Actions</th>
            </tr>
          </thead>
          <tbody>
            {files.value.map((f) => (
              <tr
                key={f.name}
                class={selectedPath.value === f.name ? "row-muted" : ""}
              >
                <td>
                  <button
                    class="btn"
                    style={{
                      padding: 0,
                      background: "transparent",
                      color: "var(--link, #4ea1ff)",
                      textDecoration: "underline",
                    }}
                    onClick={() => loadPreview(f.name)}
                  >
                    {f.name}
                  </button>
                </td>
                <td>{formatSize(f.size)}</td>
                <td style={{ display: "flex", gap: "0.4rem" }}>
                  <button
                    class="btn"
                    onClick={() => downloadFile(f.name)}
                    title="Download to your computer"
                  >
                    download
                  </button>
                  <button
                    class="btn btn-danger"
                    onClick={() => deleteFile(f.name)}
                  >
                    delete
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
        {action.value && <p class="muted small">{action.value}</p>}
      </section>

      {sel && (
        <section class="card">
          <header class="card-header">
            <h2>Preview: {sel}</h2>
            <div class="card-actions">
              <button class="btn" onClick={() => downloadFile(sel)}>
                Download
              </button>
              <button
                class="btn"
                onClick={() => {
                  selectedPath.value = null;
                  previewContent.value = "";
                }}
              >
                Close
              </button>
            </div>
          </header>
          {selFile && (
            <p class="muted small">{formatSize(selFile.size)}</p>
          )}
          {previewLoading.value && <p class="muted">Loading…</p>}
          {previewError.value && (
            <p class="muted" style={{ color: "var(--red)" }}>
              {previewError.value}
            </p>
          )}
          {!previewLoading.value && !previewError.value && (
            <pre class="files-preview">{displayContent}</pre>
          )}
        </section>
      )}
    </div>
  );
}
