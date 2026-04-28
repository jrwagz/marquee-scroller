# Firmware Binary Hosting

## Overview

Each tagged firmware release (`v*.*.*`) builds `firmware.bin` and publishes it both as a GitHub
Release asset and as a versioned file inside a Docker image served by an Azure App Service. The
Docker-based path provides stable `http://` URLs that the ESP8266 clocks can use directly for
OTA updates — GitHub Release URLs are HTTPS-only and are not reachable by ESPhttpUpdate.

---

## How It Works

### Trigger

Pushing a tag matching `v*.*.*` fires `.github/workflows/lint-test-build.yaml`. After the
existing lint → test → build → GitHub Release steps complete, three additional steps run:

1. **Log in to Docker Hub** — authenticates using repository secrets
2. **Publish firmware binary to simple-web-server image** — injects the new binary as a Docker
   layer on top of the current `jrwagz/simple-web-server:latest` image and pushes it back
3. **Reload Azure firmware web app** — instructs Azure App Service `files-jrwagz` to pull the
   updated image and restart

### Layer-Injection Model

Each release adds exactly one new file to the Docker image without touching any previous files.
The CI step generates and runs a minimal Dockerfile built for both `linux/amd64` and `linux/arm64`
via Docker Buildx:

```dockerfile
FROM jrwagz/simple-web-server:latest
COPY marquee-scroller-3.08.0-wagfam-cbb4daa.bin /usr/local/apache2/htdocs/marquee-scroller-3.08.0-wagfam-cbb4daa.bin
```

Because Docker images are layered, every prior binary remains in the image across releases.
The image grows by roughly the size of one firmware binary (~400 KB) per release. The firmware
binary itself is architecture-agnostic (ESP8266 target); the multi-arch build ensures the server
process that hosts it can run on either AMD64 (Azure App Service) or ARM64 (Raspberry Pi, etc.).

### Image Tags

The build step pushes **two tags** for every release:

- `jrwagz/simple-web-server:latest` — always points to the most recent release
- `jrwagz/simple-web-server:<VERSION>` — immutable tag for that specific release
  (e.g. `jrwagz/simple-web-server:3.08.0-wagfam-cbb4daa`)

The Azure App Service is configured to pull the versioned tag (not `:latest`). This is
intentional: `azure/webapps-deploy@v3` only triggers a real container restart when the
`DOCKER_CUSTOM_IMAGE_NAME` app setting changes. Using `:latest` every time means the setting
is identical across releases and Azure leaves the old container running — silently reporting
"Successfully deployed" while serving stale binaries. The versioned tag guarantees the setting
changes on every release and forces a pull + restart.

### File Naming

Binaries are named using the full CI build version string from `artifacts/VERSION.txt`:

```text
marquee-scroller-<VERSION>.bin
```

`build_version.py` (the PlatformIO pre-build script) sets VERSION to `<base>-wagfam-<git-hash>`
on CI builds, e.g.:

```text
marquee-scroller-3.08.0-wagfam-cbb4daa.bin
```

---

## Infrastructure

| Component | Details |
| --- | --- |
| Docker Hub image | `docker.io/jrwagz/simple-web-server:latest` (+ versioned tag per release) |
| Web server | Apache httpd; serves `/usr/local/apache2/htdocs/` |
| Azure App Service | `files-jrwagz` |
| Protocol | Plain HTTP — required by ESPhttpUpdate (no HTTPS support on ESP8266) |
| Build architecture | `linux/amd64` + `linux/arm64` (multi-arch via Docker Buildx) |

---

## Required GitHub Secrets

Three secrets must be present at Settings → Secrets and variables → Actions:

| Secret | How to obtain |
| --- | --- |
| `DOCKER_HUB_USERNAME` | Your Docker Hub username (`jrwagz`) |
| `DOCKER_HUB_TOKEN` | Docker Hub → Account Settings → Personal Access Tokens → New Token with **Read & Write** scope on `jrwagz/simple-web-server` |
| `AZURE_FIRMWARE_PUBLISH_PROFILE` | Azure Portal → App Service `files-jrwagz` → Overview → **Get publish profile** (downloads XML; paste the entire content as the secret value) |

### Azure prerequisite: enable basic auth

Azure disables basic auth on new App Services by default. Enable it before downloading the
publish profile:

App Service `files-jrwagz` → Settings → Configuration → General settings → Platform settings
→ **SCM Basic Auth Publishing Credentials** → **On** → Save.

This is a required one-time step when setting up the App Service.

---

## How Clocks Use the Hosted Binaries

The calendar server can trigger an automatic OTA update on any clock by
including `latestVersion` and `firmwareUrl` in its calendar JSON response:

```json
{
  "config": {
    "latestVersion": "3.08.0-wagfam-cbb4daa",
    "firmwareUrl": "http://files-jrwagz.azurewebsites.net/marquee-scroller-3.08.0-wagfam-cbb4daa.bin"
  }
}
```

`firmwareUrl` **must use `http://`** — ESPhttpUpdate on the ESP8266 does not support HTTPS.
The OTA rollback mechanism (described in `docs/OTA_STRATEGY.md`) applies to all updates
delivered this way.

---

## Verification

After a tag push, confirm the new binary is live:

```bash
VERSION=$(cat artifacts/VERSION.txt)
curl -I "http://files-jrwagz.azurewebsites.net/marquee-scroller-${VERSION}.bin"
# Expect: HTTP/1.1 200 OK
```

Confirm the previous release binary is still present (accumulation check):

```bash
curl -I "http://files-jrwagz.azurewebsites.net/marquee-scroller-<previous-VERSION>.bin"
# Expect: HTTP/1.1 200 OK
```
