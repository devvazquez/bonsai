/* Bonsai web flasher.
 *
 * Downloads a firmware release from the repository's GitHub releases and writes
 * it to a board over the Web Serial API, using esptool-js. Everything happens in
 * the browser: the binaries go from GitHub to the serial port and nowhere else.
 */

import { ESPLoader, Transport } from "./vendor/esptool-js/bundle.js";

/* --------------------------------------------------------------------------
 * Which repository to read releases from.
 *
 * Derived from the URL so a fork serves its own releases without editing this
 * file: user.github.io/repo/ gives both halves. Anything else (a local file, a
 * custom domain) falls back to the upstream repository.
 * ---------------------------------------------------------------------- */

const FALLBACK_REPO = { owner: "devvazquez", repo: "bonsai" };

function detectRepo() {
  const host = location.hostname;
  const seg = location.pathname.split("/").filter(Boolean);
  if (host.endsWith(".github.io") && seg.length > 0) {
    const owner = host.slice(0, -".github.io".length);
    // user.github.io/user.github.io is the user site: its repo is the host.
    return { owner, repo: seg[0] };
  }
  return FALLBACK_REPO;
}

const REPO = detectRepo();
const API = `https://api.github.com/repos/${REPO.owner}/${REPO.repo}`;

/* --------------------------------------------------------------------------
 * Flash layout.
 *
 * A release can say where its parts go by shipping a manifest.json; when it
 * does not, the file names are matched against the standard ESP-IDF layout
 * that PlatformIO produces for this board.
 * ---------------------------------------------------------------------- */

const PART_RULES = [
  { re: /bootloader.*\.bin$/i,                    offset: 0x0000 },
  { re: /(partitions?|partition[-_]table).*\.bin$/i, offset: 0x8000 },
  { re: /boot_app0.*\.bin$/i,                     offset: 0xe000 },
  { re: /(firmware|bonsai|app)[^/]*\.bin$/i,      offset: 0x10000 },
];

// A single image that already contains bootloader + table + app goes at 0.
const MERGED_RE = /(merged|combined|factory|full)[^/]*\.bin$/i;

const USB_NAMES = {
  "303a": { name: "Espressif", devices: { "1001": "ESP32-S3 (native USB)", "0002": "ESP32-S2" } },
  "10c4": { name: "Silicon Labs", devices: { ea60: "CP2102 USB bridge" } },
  "1a86": { name: "WCH", devices: { "7523": "CH340 USB bridge", "55d4": "CH9102 USB bridge" } },
  "0403": { name: "FTDI", devices: {} },
  "2886": { name: "Seeed Studio", devices: {} },
};

/* --------------------------------------------------------------------------
 * Small DOM helpers.
 * ---------------------------------------------------------------------- */

const $ = (id) => document.getElementById(id);
const consoleEl = $("console");

function log(text, cls) {
  const line = document.createElement("span");
  if (cls) line.className = cls;
  line.textContent = text + "\n";
  consoleEl.appendChild(line);
  consoleEl.scrollTop = consoleEl.scrollHeight;
}

function clearConsole(first) {
  consoleEl.textContent = "";
  if (first) log(first, "accent");
}

function setField(id, value, cls) {
  const el = $(id);
  el.textContent = value;
  el.className = cls || "";
}

const hex = (n) => "0x" + n.toString(16);

function humanSize(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / 1024 / 1024).toFixed(2) + " MB";
}

/* --------------------------------------------------------------------------
 * Releases.
 * ---------------------------------------------------------------------- */

let releases = [];      // as returned by the API, newest first
let currentPlan = null; // { release, parts: [{name, offset, url, size}] }

async function loadReleases() {
  const select = $("release");
  const hint = $("releases-hint");

  select.disabled = true;
  select.innerHTML = "<option>Loading releases…</option>";
  hint.className = "hint";
  hint.textContent = `Reading ${REPO.owner}/${REPO.repo}…`;

  let data;
  try {
    const res = await fetch(`${API}/releases?per_page=30`, {
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!res.ok) throw new Error(`GitHub answered ${res.status}`);
    data = await res.json();
  } catch (err) {
    select.innerHTML = "<option>Release list unavailable</option>";
    hint.className = "hint err";
    hint.textContent = `Could not read the release list: ${err.message}.`;
    updateFlashButton();
    return;
  }

  releases = data.filter((r) => !r.draft);

  if (releases.length === 0) {
    select.innerHTML = "<option>No releases published yet</option>";
    hint.className = "hint warn";
    hint.innerHTML =
      `<b>${REPO.owner}/${REPO.repo}</b> has no published releases, so there is ` +
      `nothing to flash yet. A release needs a <code>manifest.json</code>, a ` +
      `merged image, or the usual <code>bootloader.bin</code> / ` +
      `<code>partitions.bin</code> / <code>firmware.bin</code> set attached to it.`;
    $("release-detail").hidden = true;
    updateFlashButton();
    return;
  }

  select.innerHTML = "";
  releases.forEach((r, i) => {
    const opt = document.createElement("option");
    const date = r.published_at ? r.published_at.slice(0, 10) : "unpublished";
    const marks = [];
    if (i === 0 && !r.prerelease) marks.push("latest");
    if (r.prerelease) marks.push("pre-release");
    opt.value = String(i);
    opt.textContent =
      `${r.tag_name} — ${date}${marks.length ? "  [" + marks.join(", ") + "]" : ""}`;
    select.appendChild(opt);
  });
  select.disabled = false;

  // The newest stable build is what almost everybody wants.
  const firstStable = releases.findIndex((r) => !r.prerelease);
  select.value = String(firstStable === -1 ? 0 : firstStable);

  hint.className = "hint";
  hint.textContent = `${releases.length} release${releases.length === 1 ? "" : "s"} found.`;
  await selectRelease();
}

async function selectRelease() {
  const release = releases[Number($("release").value)];
  const detail = $("release-detail");
  const hint = $("releases-hint");
  currentPlan = null;

  if (!release) {
    detail.hidden = true;
    updateFlashButton();
    return;
  }

  detail.hidden = false;
  setField("r-tag", release.tag_name);
  setField(
    "r-date",
    release.published_at ? new Date(release.published_at).toLocaleString() : "—"
  );
  setField(
    "r-channel",
    release.prerelease ? "pre-release" : "stable",
    release.prerelease ? "warn" : "ok"
  );
  $("r-notes").innerHTML = "";
  const link = document.createElement("a");
  link.href = release.html_url;
  link.textContent = "release page";
  link.target = "_blank";
  link.rel = "noreferrer";
  $("r-notes").appendChild(link);

  const plan = $("plan");
  plan.innerHTML = "<tr><td>…</td><td>reading assets</td><td></td></tr>";

  let parts;
  try {
    parts = await buildPlan(release);
  } catch (err) {
    plan.innerHTML = "";
    hint.className = "hint err";
    hint.textContent = err.message;
    updateFlashButton();
    return;
  }

  currentPlan = { release, parts };
  plan.innerHTML = "";
  for (const part of parts) {
    const tr = document.createElement("tr");
    tr.innerHTML =
      `<td>${hex(part.offset)}</td><td>${part.name}</td>` +
      `<td>${part.size ? humanSize(part.size) : ""}</td>`;
    plan.appendChild(tr);
  }

  hint.className = "hint";
  hint.textContent =
    parts.length === 1
      ? "One image, written at " + hex(parts[0].offset) + "."
      : parts.length + " images to write.";
  updateFlashButton();
}

// Works out what goes where for a release, preferring an explicit manifest.
async function buildPlan(release) {
  const assets = release.assets || [];
  const manifest = assets.find((a) => /^manifest\.json$/i.test(a.name));

  if (manifest) {
    const res = await fetch(manifest.browser_download_url);
    if (!res.ok) throw new Error(`Could not read manifest.json (${res.status}).`);
    const doc = await res.json();
    const builds = doc.builds || [];
    if (builds.length === 0) throw new Error("manifest.json lists no builds.");
    // Keep every build; the right one is picked once the chip is known.
    const build =
      builds.find((b) => /esp32-?s3/i.test(b.chipFamily || "")) || builds[0];
    const parts = (build.parts || []).map((p) => {
      const url = new URL(p.path, manifest.browser_download_url).href;
      const asset = assets.find((a) => url.endsWith("/" + a.name));
      return {
        name: p.path.split("/").pop(),
        offset: p.offset || 0,
        url,
        size: asset ? asset.size : 0,
        chipFamily: build.chipFamily || null,
      };
    });
    if (parts.length === 0) throw new Error("manifest.json lists no parts.");
    return parts.sort((a, b) => a.offset - b.offset);
  }

  const bins = assets.filter((a) => /\.bin$/i.test(a.name));
  if (bins.length === 0) {
    throw new Error(
      `${release.tag_name} has no .bin assets and no manifest.json, so there is nothing to flash.`
    );
  }

  const merged = bins.find((a) => MERGED_RE.test(a.name));
  if (merged) {
    return [
      { name: merged.name, offset: 0x0, url: merged.browser_download_url, size: merged.size },
    ];
  }

  const parts = [];
  for (const asset of bins) {
    const rule = PART_RULES.find((r) => r.re.test(asset.name));
    if (!rule) continue;
    parts.push({
      name: asset.name,
      offset: rule.offset,
      url: asset.browser_download_url,
      size: asset.size,
    });
  }
  if (parts.length === 0) {
    throw new Error(
      `Could not tell where the assets of ${release.tag_name} belong. Attach a ` +
        `manifest.json, or name them bootloader.bin / partitions.bin / firmware.bin.`
    );
  }
  return parts.sort((a, b) => a.offset - b.offset);
}

/* --------------------------------------------------------------------------
 * The board.
 * ---------------------------------------------------------------------- */

let transport = null;
let esploader = null;
let busy = false;

const terminal = {
  clean: () => clearConsole(),
  writeLine: (data) => log(String(data)),
  write: (data) => {
    // esptool-js writes progress dots without newlines.
    const text = String(data);
    if (consoleEl.lastChild && !consoleEl.lastChild.textContent.endsWith("\n")) {
      consoleEl.lastChild.textContent += text;
    } else {
      const span = document.createElement("span");
      span.textContent = text;
      consoleEl.appendChild(span);
    }
    consoleEl.scrollTop = consoleEl.scrollHeight;
  },
};

function describePort(port) {
  const info = port.getInfo ? port.getInfo() : {};
  if (info.usbVendorId === undefined) return "serial port";
  const vid = info.usbVendorId.toString(16).padStart(4, "0");
  const pid = (info.usbProductId ?? 0).toString(16).padStart(4, "0");
  const vendor = USB_NAMES[vid];
  const name = vendor ? vendor.devices[pid] || vendor.name : "USB serial device";
  return `${name} (${vid}:${pid})`;
}

async function refreshPorts() {
  const list = $("ports");
  if (!navigator.serial) return;
  const ports = await navigator.serial.getPorts();
  list.innerHTML = "";
  if (ports.length === 0) {
    list.innerHTML = '<li class="empty">No ports authorised for this site yet.</li>';
    return;
  }
  ports.forEach((port, i) => {
    const li = document.createElement("li");
    const active = transport && transport.device === port;
    li.innerHTML =
      `<span class="tag">${active ? "*" : String(i)}</span>${describePort(port)}` +
      (active ? " — in use" : "");
    list.appendChild(li);
  });
}

async function connect() {
  if (busy) return;
  busy = true;
  $("connect").disabled = true;

  try {
    const device = await navigator.serial.requestPort();
    transport = new Transport(device, true);
    esploader = new ESPLoader({
      transport,
      baudrate: 921600,
      romBaudrate: 115200,
      terminal,
      debugLogging: false,
    });

    clearConsole("Connecting to the board…");
    setField("k-status", "connecting", "warn");

    const description = await esploader.main();

    setField("k-status", "connected", "ok");
    setField("k-chip", description);
    setField("k-rev", esploader.chip.getChipRevision
      ? String(await esploader.chip.getChipRevision(esploader))
      : "—");
    setField("k-features", (await esploader.chip.getChipFeatures(esploader)).join(", "));
    setField("k-crystal", (await esploader.chip.getCrystalFreq(esploader)) + " MHz");
    setField("k-mac", await esploader.chip.readMac(esploader));
    setField("k-flash", await esploader.detectFlashSize());
    setField("k-port", describePort(transport.device));

    $("connect").hidden = true;
    $("disconnect").hidden = false;
    log("Board ready.", "ok");
  } catch (err) {
    if (err && err.name === "NotFoundError") {
      log("No port was picked.", "warn");
    } else {
      log("Connection failed: " + (err && err.message ? err.message : err), "err");
      log(
        "Hold BOOT, tap RESET, release BOOT to force the board into download mode, then connect again.",
        "warn"
      );
      setField("k-status", "not connected", "err");
    }
    await cleanupTransport();
  } finally {
    busy = false;
    $("connect").disabled = !navigator.serial;
    await refreshPorts();
    updateFlashButton();
  }
}

async function cleanupTransport() {
  if (transport) {
    try { await transport.disconnect(); } catch { /* already gone */ }
  }
  transport = null;
  esploader = null;
  $("connect").hidden = false;
  $("connect").disabled = !navigator.serial;
  $("disconnect").hidden = true;
  ["k-chip", "k-rev", "k-features", "k-crystal", "k-mac", "k-flash", "k-port"]
    .forEach((id) => setField(id, "—", "dim"));
  updateFlashButton();
}

async function disconnect() {
  await cleanupTransport();
  setField("k-status", "not connected", "dim");
  log("Disconnected.", "warn");
  await refreshPorts();
}

/* --------------------------------------------------------------------------
 * Flashing.
 * ---------------------------------------------------------------------- */

function updateFlashButton() {
  $("flash").disabled = busy || !esploader || !currentPlan;
}

async function download(part, index, total) {
  log(`Downloading ${part.name} (${index + 1}/${total})…`);
  const res = await fetch(part.url);
  if (!res.ok) throw new Error(`${part.name}: download failed (${res.status})`);
  const data = new Uint8Array(await res.arrayBuffer());
  log(`  ${part.name}: ${humanSize(data.length)} to ${hex(part.offset)}`);
  return { data, address: part.offset };
}

async function flash() {
  if (busy || !esploader || !currentPlan) return;

  const { release, parts } = currentPlan;
  const family = parts.find((p) => p.chipFamily)?.chipFamily;
  const chipName = esploader.chip.CHIP_NAME;
  if (family && family.replace(/-/g, "").toLowerCase() !== chipName.replace(/-/g, "").toLowerCase()) {
    const go = confirm(
      `This release is built for ${family} but the connected board is a ${chipName}.\n\n` +
        `Flashing it will almost certainly brick the board until it is reflashed. Continue anyway?`
    );
    if (!go) return;
  }

  busy = true;
  updateFlashButton();
  $("connect").disabled = true;
  $("disconnect").disabled = true;
  $("release").disabled = true;

  const progress = $("progress");
  const bar = $("bar");
  progress.hidden = false;
  bar.style.width = "0%";

  try {
    log(`--- flashing ${release.tag_name} ---`, "accent");

    const fileArray = [];
    for (let i = 0; i < parts.length; i++) {
      fileArray.push(await download(parts[i], i, parts.length));
    }

    const totalBytes = fileArray.reduce((sum, f) => sum + f.data.length, 0);
    const doneBefore = [];
    let running = 0;
    for (const f of fileArray) {
      doneBefore.push(running);
      running += f.data.length;
    }

    if ($("erase").checked) log("Erasing the whole flash first…", "warn");

    await esploader.writeFlash({
      fileArray,
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: $("erase").checked,
      compress: true,
      reportProgress: (fileIndex, written, total) => {
        const overall = (doneBefore[fileIndex] + written) / totalBytes;
        bar.style.width = (overall * 100).toFixed(1) + "%";
        $("progress-label").textContent =
          `${parts[fileIndex].name}: ${Math.round((written / total) * 100)}%` +
          `  ·  ${Math.round(overall * 100)}% overall`;
      },
    });

    bar.style.width = "100%";
    $("progress-label").textContent = "Done.";
    log(`--- ${release.tag_name} written ---`, "ok");

    // The XIAO's native USB port has no RTS line to pulse: esptool-js needs to
    // be told so it resets the chip the USB-OTG way instead.
    const info = transport.device.getInfo ? transport.device.getInfo() : {};
    await esploader.after("hard_reset", info.usbVendorId === 0x303a);
    log("Board reset. Open a serial monitor at 115200 baud and type \"help\".", "ok");

    // The stub is gone after the reset, so the session has to be re-opened.
    await cleanupTransport();
    setField("k-status", "flashed, disconnected", "ok");
    await refreshPorts();
  } catch (err) {
    log("Flashing failed: " + (err && err.message ? err.message : err), "err");
    $("progress-label").textContent = "Failed.";
  } finally {
    busy = false;
    $("connect").disabled = !navigator.serial;
    $("disconnect").disabled = false;
    $("release").disabled = releases.length === 0;
    updateFlashButton();
  }
}

/* --------------------------------------------------------------------------
 * Start-up.
 * ---------------------------------------------------------------------- */

function checkSupport() {
  const line = $("support-line");
  if (navigator.serial) {
    line.className = "hint";
    line.innerHTML =
      "Web Serial is available in this browser. Nothing to install.";
    $("connect").disabled = false;
    return true;
  }
  line.className = "hint err";
  line.innerHTML =
    "This browser has no Web Serial API, so it cannot talk to the board. " +
    "Use Chrome, Edge or Opera on a desktop &mdash; Firefox, Safari and mobile browsers do not support it.";
  $("connect").disabled = true;
  $("devices-hint").hidden = true;
  return false;
}

function wireLinks() {
  const base = `https://github.com/${REPO.owner}/${REPO.repo}`;
  $("repo-link").href = base;
  $("releases-link").href = base + "/releases";
}

wireLinks();
const supported = checkSupport();
clearConsole(supported ? "Ready. Connect a board to begin." : "Web Serial is not available here.");
loadReleases();
if (supported) {
  refreshPorts();
  navigator.serial.addEventListener("connect", refreshPorts);
  navigator.serial.addEventListener("disconnect", refreshPorts);
}

$("connect").addEventListener("click", connect);
$("disconnect").addEventListener("click", disconnect);
$("reload").addEventListener("click", loadReleases);
$("release").addEventListener("change", selectRelease);
$("flash").addEventListener("click", flash);
