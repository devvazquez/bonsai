/* Flash Bonsai.
 *
 * Reads the repository's GitHub releases, then writes the one you pick to a
 * board over the Web Serial API with esptool-js. The binaries travel from
 * GitHub to the serial port inside the browser; no server sees them.
 */

import { ESPLoader, Transport } from "./vendor/esptool-js/bundle.js";

/* Which repository the releases come from. Derived from the URL so a fork
 * serves its own builds; anything that is not a project page falls back. */
function detectRepo() {
  const host = location.hostname;
  const seg = location.pathname.split("/").filter(Boolean);
  if (host.endsWith(".github.io") && seg.length > 0) {
    return { owner: host.slice(0, -".github.io".length), repo: seg[0] };
  }
  return { owner: "devvazquez", repo: "bonsai" };
}

const REPO = detectRepo();
const API = `https://api.github.com/repos/${REPO.owner}/${REPO.repo}`;

/* Where each image goes when a release ships no manifest.json. These are the
 * names PlatformIO produces, at the offsets the ESP32-S3 expects. */
const PART_RULES = [
  { re: /bootloader.*\.bin$/i, offset: 0x0 },
  { re: /(partitions?|partition[-_]table).*\.bin$/i, offset: 0x8000 },
  { re: /boot_app0.*\.bin$/i, offset: 0xe000 },
  { re: /(firmware|bonsai|app)[^/]*\.bin$/i, offset: 0x10000 },
];
const MERGED_RE = /(merged|combined|factory|full)[^/]*\.bin$/i;

const USB_NAMES = {
  "303a": { name: "Espressif", devices: { "1001": "native USB", "0002": "ESP32-S2" } },
  "10c4": { name: "Silicon Labs", devices: { ea60: "CP2102 adapter" } },
  "1a86": { name: "WCH", devices: { "7523": "CH340 adapter", "55d4": "CH9102 adapter" } },
  "0403": { name: "FTDI", devices: {} },
  "2886": { name: "Seeed Studio", devices: {} },
};

const $ = (id) => document.getElementById(id);
const hex = (n) => "0x" + n.toString(16);

function size(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(0) + " KB";
  return (bytes / 1024 / 1024).toFixed(2) + " MB";
}

function log(text) {
  const pre = $("log");
  pre.textContent += text + "\n";
  pre.scrollTop = pre.scrollHeight;
}

/* --------------------------------------------------------------------------
 * Steps.
 * ---------------------------------------------------------------------- */

const STEP_NAMES = ["Board", "Version", "Write"];
let step = 0;

function goStep(n) {
  step = n;
  document.querySelectorAll(".step").forEach((el) => {
    el.classList.toggle("active", Number(el.dataset.step) === n);
  });
  document.querySelectorAll(".rail li").forEach((li) => {
    const i = Number(li.dataset.rail);
    li.classList.toggle("current", i === n);
    li.classList.toggle("done", i < n);
  });
  document.querySelectorAll(".mprog-track i").forEach((bar, i) => {
    bar.classList.toggle("on", i <= n);
  });
  $("mprog-name").textContent = STEP_NAMES[n];
  $("mprog-cur").textContent = String(n + 1);
  window.scrollTo({ top: 0, behavior: "instant" });
}

/* --------------------------------------------------------------------------
 * Releases.
 * ---------------------------------------------------------------------- */

let releases = [];
let selected = -1;
let plan = null;

async function loadReleases() {
  const sub = $("releases-sub");
  const list = $("releases");
  selected = -1;
  plan = null;
  $("plan").hidden = true;
  $("plan-title").hidden = true;
  $("to-write").disabled = true;
  list.innerHTML = "";
  sub.textContent = "Reading the published releases.";

  let data;
  try {
    const res = await fetch(`${API}/releases?per_page=30`, {
      headers: { Accept: "application/vnd.github+json" },
    });
    if (!res.ok) throw new Error(`GitHub answered ${res.status}`);
    data = await res.json();
  } catch (err) {
    sub.textContent = `The release list could not be read: ${err.message}`;
    return;
  }

  releases = data.filter((r) => !r.draft);
  if (releases.length === 0) {
    sub.innerHTML =
      `<b>${REPO.owner}/${REPO.repo}</b> has published no releases yet, so there ` +
      `is nothing to write. Tag a commit and the build workflow will publish one.`;
    return;
  }

  sub.textContent = "The newest build is picked for you. Older ones still work.";
  releases.forEach((r, i) => {
    const date = r.published_at
      ? new Date(r.published_at).toLocaleDateString(undefined, {
          year: "numeric", month: "short", day: "numeric",
        })
      : "not published";
    const card = document.createElement("button");
    card.type = "button";
    card.className = "opt";
    card.dataset.index = String(i);
    card.innerHTML =
      `<span class="radio"></span>` +
      `<span class="opt-body">` +
      `<span class="opt-name">${r.tag_name}</span>` +
      `<span class="opt-meta">${date}</span>` +
      `</span>` +
      (r.prerelease
        ? `<span class="tag tag-blue">pre-release</span>`
        : i === 0
        ? `<span class="tag tag-green">newest</span>`
        : "");
    card.addEventListener("click", () => select(i));
    list.appendChild(card);
  });

  const firstStable = releases.findIndex((r) => !r.prerelease);
  select(firstStable === -1 ? 0 : firstStable);
}

async function select(index) {
  selected = index;
  document.querySelectorAll(".opt").forEach((el) => {
    el.classList.toggle("sel", Number(el.dataset.index) === index);
  });

  const release = releases[index];
  const table = $("plan");
  $("plan-title").hidden = false;
  table.hidden = false;
  table.innerHTML = `<div class="srow"><span class="k">Reading</span>` +
    `<span class="v">${release.tag_name}</span></div>`;
  $("to-write").disabled = true;

  try {
    plan = { release, parts: await buildPlan(release) };
  } catch (err) {
    plan = null;
    table.innerHTML =
      `<div class="srow"><span class="k">Problem</span><span class="v">${err.message}</span></div>`;
    return;
  }

  table.innerHTML = plan.parts
    .map(
      (p) =>
        `<div class="srow"><span class="k mono">${hex(p.offset)}</span>` +
        `<span class="v mono">${p.name}</span>` +
        `<span class="size">${p.size ? size(p.size) : ""}</span></div>`
    )
    .join("");
  $("to-write").disabled = false;
}

// Works out what goes where, preferring what the release states over guesswork.
async function buildPlan(release) {
  const assets = release.assets || [];
  const manifest = assets.find((a) => /^manifest\.json$/i.test(a.name));

  if (manifest) {
    const res = await fetch(manifest.browser_download_url);
    if (!res.ok) throw new Error(`manifest.json could not be read (${res.status}).`);
    const doc = await res.json();
    const builds = doc.builds || [];
    const build = builds.find((b) => /esp32-?s3/i.test(b.chipFamily || "")) || builds[0];
    if (!build || !(build.parts || []).length) throw new Error("manifest.json lists no images.");
    return build.parts
      .map((p) => {
        const url = new URL(p.path, manifest.browser_download_url).href;
        const asset = assets.find((a) => url.endsWith("/" + a.name));
        return {
          name: p.path.split("/").pop(),
          offset: p.offset || 0,
          url,
          size: asset ? asset.size : 0,
          chipFamily: build.chipFamily || null,
        };
      })
      .sort((a, b) => a.offset - b.offset);
  }

  const bins = assets.filter((a) => /\.bin$/i.test(a.name));
  const merged = bins.find((a) => MERGED_RE.test(a.name));
  if (merged) {
    return [{ name: merged.name, offset: 0x0, url: merged.browser_download_url, size: merged.size }];
  }

  const parts = [];
  for (const asset of bins) {
    const rule = PART_RULES.find((r) => r.re.test(asset.name));
    if (rule) {
      parts.push({
        name: asset.name,
        offset: rule.offset,
        url: asset.browser_download_url,
        size: asset.size,
      });
    }
  }
  if (parts.length === 0) {
    throw new Error(`${release.tag_name} has no firmware images attached to it.`);
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
  clean: () => { $("log").textContent = ""; },
  writeLine: (data) => log(String(data)),
  write: (data) => {
    const pre = $("log");
    pre.textContent += String(data);
    pre.scrollTop = pre.scrollHeight;
  },
};

function describePort(port) {
  const info = port.getInfo ? port.getInfo() : {};
  if (info.usbVendorId === undefined) return "serial port";
  const vid = info.usbVendorId.toString(16).padStart(4, "0");
  const pid = (info.usbProductId ?? 0).toString(16).padStart(4, "0");
  const vendor = USB_NAMES[vid];
  if (!vendor) return `USB serial device ${vid}:${pid}`;
  const device = vendor.devices[pid];
  return device ? `${vendor.name}, ${device}` : vendor.name;
}

async function connect() {
  if (busy) return;
  busy = true;
  $("connect").disabled = true;
  $("connect").textContent = "Connecting";

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

    const description = await esploader.main();

    $("k-chip").textContent = description;
    $("k-mac").textContent = await esploader.chip.readMac(esploader);
    $("k-flash").textContent = await esploader.detectFlashSize();
    $("k-port").textContent = describePort(device);
    $("board-summary").hidden = false;
    $("w-board").textContent = description;

    $("connect").hidden = true;
    $("to-versions").hidden = false;
    $("tip").hidden = true;
  } catch (err) {
    if (!err || err.name !== "NotFoundError") {
      showError(
        "The board did not answer: " + (err && err.message ? err.message : err) +
          ". Hold BOOT, tap RESET, release BOOT and try again."
      );
    }
    await release();
  } finally {
    busy = false;
    $("connect").disabled = false;
    $("connect").textContent = "Connect a board";
  }
}

async function release() {
  if (transport) {
    try { await transport.disconnect(); } catch { /* the port is already gone */ }
  }
  transport = null;
  esploader = null;
}

function showError(text) {
  const box = $("unsupported");
  box.textContent = text;
  box.classList.add("show");
}

/* --------------------------------------------------------------------------
 * Writing.
 * ---------------------------------------------------------------------- */

async function flash() {
  if (busy || !esploader || !plan) return;

  const family = plan.parts.find((p) => p.chipFamily)?.chipFamily;
  const chipName = esploader.chip.CHIP_NAME;
  const same = (a, b) => a.replace(/-/g, "").toLowerCase() === b.replace(/-/g, "").toLowerCase();
  if (family && !same(family, chipName)) {
    const go = confirm(
      `This build is for ${family} and the connected board is a ${chipName}. ` +
        `Writing it will leave the board unusable until it is flashed again. Continue?`
    );
    if (!go) return;
  }

  busy = true;
  $("flash").disabled = true;
  $("back-version").disabled = true;
  $("outcome").hidden = true;
  $("progress").hidden = false;
  $("bar").style.width = "0%";
  $("logbox").open = false;
  $("log").textContent = "";

  try {
    const fileArray = [];
    let done = 0;
    const offsets = [];
    for (const part of plan.parts) {
      $("progress-label").textContent = `Downloading ${part.name}`;
      const res = await fetch(part.url);
      if (!res.ok) throw new Error(`${part.name} could not be downloaded (${res.status})`);
      const data = new Uint8Array(await res.arrayBuffer());
      log(`${part.name}: ${size(data.length)} for ${hex(part.offset)}`);
      offsets.push(done);
      done += data.length;
      fileArray.push({ data, address: part.offset });
    }
    const total = done;

    await esploader.writeFlash({
      fileArray,
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex, written, fileTotal) => {
        const overall = (offsets[fileIndex] + written) / total;
        $("bar").style.width = (overall * 100).toFixed(1) + "%";
        $("progress-label").textContent =
          `Writing ${plan.parts[fileIndex].name}, ` +
          `${Math.round((written / fileTotal) * 100)} percent of it, ` +
          `${Math.round(overall * 100)} percent overall`;
      },
    });

    $("bar").style.width = "100%";
    $("progress-label").textContent = "Written.";

    // The XIAO answers over its own USB port, which has no RTS line to pulse,
    // so esptool-js has to reset it the USB-OTG way instead.
    const info = transport.device.getInfo ? transport.device.getInfo() : {};
    await esploader.after("hard_reset", info.usbVendorId === 0x303a);

    // The flasher stub is gone after the reset, so the session ends with it.
    await release();
    outcome(
      true,
      `${plan.release.tag_name} is on the board`,
      "The board has restarted. Open its setup page to give it a network, or " +
        "hold the button if it is already configured."
    );
    $("flash").hidden = true;
    $("back-version").hidden = true;
  } catch (err) {
    $("progress-label").textContent = "Stopped.";
    outcome(
      false,
      "The write did not finish",
      (err && err.message ? err.message : String(err)) +
        ". The board keeps the firmware it had. Open Details for the full log."
    );
    $("logbox").open = true;
  } finally {
    busy = false;
    $("flash").disabled = false;
    $("back-version").disabled = false;
  }
}

function outcome(ok, title, text) {
  const badge = $("outcome-badge");
  badge.className = "badge " + (ok ? "ok" : "err");
  badge.innerHTML = ok
    ? `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
         stroke-linecap="round" stroke-linejoin="round"><path d="M20 6 9 17l-5-5"></path></svg>`
    : `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
         stroke-linecap="round" stroke-linejoin="round"><path d="M18 6 6 18M6 6l12 12"></path></svg>`;
  $("outcome-title").textContent = title;
  $("outcome-text").textContent = text;
  $("outcome").hidden = false;
}

/* --------------------------------------------------------------------------
 * Start-up.
 * ---------------------------------------------------------------------- */

$("repo-link").href = `https://github.com/${REPO.owner}/${REPO.repo}`;

if (!navigator.serial) {
  $("unsupported").classList.add("show");
  $("connect").disabled = true;
}

goStep(0);
loadReleases();

$("connect").addEventListener("click", connect);
$("to-versions").addEventListener("click", () => goStep(1));
$("back-board").addEventListener("click", () => goStep(0));
$("to-write").addEventListener("click", () => {
  $("w-version").textContent = plan.release.tag_name;
  $("w-images").textContent =
    plan.parts.length === 1
      ? `one image at ${hex(plan.parts[0].offset)}`
      : `${plan.parts.length} images, ${size(plan.parts.reduce((s, p) => s + p.size, 0))} in total`;
  goStep(2);
});
$("back-version").addEventListener("click", () => goStep(1));
$("reload").addEventListener("click", loadReleases);
$("flash").addEventListener("click", flash);
