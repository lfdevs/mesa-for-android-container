#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "tornado>=6.5.7",
# ]
# ///
"""Live per-render-pass bin (tile) timing viewer for the turnip mesa-debug-socket patch.

The patched tu_autotune sends one datagram per processed render pass to a unix
socket on the device:

    pid=[pid] [<hash>] gmem ts=<us> dur_us=<us> draws=<n> grid=<W>x<H> div=<d> max_tile_us=<us> tiles=[a,b,c,]
    pid=[pid] [<hash>] sysmem ts=<us> dur_us=<us> draws=<n>

tiles= is in tile_idx order (y * W + x), so it maps onto framebuffer positions.

This script SSHes to the device, runs a tiny listener that binds that socket and
streams the datagrams back as lines, and serves a live web GUI on localhost:
  - Bins:     per-bin bar chart of the latest sample (+ average overlay)
  - Heatmap:  spatial WxH heatmap of where GPU time goes across the framebuffer,
              with load-imbalance / division-efficiency stats
  - Timeline: cost of the top render passes over GPU time (what is expensive *when*)
  - Divisor:  per-tile_size_divisor stats for one RP — is dividing tiles working?

Basic Usage:
    python3 bin_timing_viewer.py
then open http://localhost:8765

Options: --ssh-host hmd --port 8765 --socket-path /tmp/mesa_tu_log.sock
"""

import argparse
import collections
import json
import re
import subprocess
import threading
import time

import tornado.ioloop
import tornado.web
import tornado.websocket

# ---------------------------------------------------------------------------
# Remote listener: bind the unix datagram socket on the device, echo datagrams
# as lines on stdout. Shipped to the device over ssh stdin (python3 -u -).
# ---------------------------------------------------------------------------

REMOTE_LISTENER = r"""
import socket, os, sys
path = sys.argv[1]
try:
    os.unlink(path)
except OSError:
    pass
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.bind(path)
os.chmod(path, 0o666)
sys.stderr.write("listening on %s\n" % path)
sys.stderr.flush()
while True:
    d = s.recv(65536)
    sys.stdout.write(d.decode("utf-8", errors="replace").rstrip("\n") + "\n")
    sys.stdout.flush()
"""

LINE_RE = re.compile(
    r"pid=(\d+)\s+"
    r"\[(\d+)\]\s+(gmem|sysmem)\s+ts=(\d+)\s+dur_us=(\d+)\s+draws=(\d+)"
    r"(?:\s+grid=(\d+)x(\d+)\s+div=(\d+)"
    r"(?:\s+fb=(\d+)x(\d+)\s+tile0=(\d+)x(\d+)\s+layers=(\d+))?"
    r"\s+max_tile_us=(\d+)\s+tiles=\[([0-9, ]*)\]?)?"
)

TILING_RE = re.compile(r"^pid=\d+\s+tiling\b")

MAX_SCALARS = 4096      # per-RP history of scalar metrics (timeline/divisor)
MAX_BIN_SAMPLES = 32    # per-RP history of full bin arrays (heatmap average)
AVG_WINDOW = 64         # samples for the list's rolling averages
TOP_N_TIMELINE = 8


class RpData:
    def __init__(self, rphash):
        self.rphash = rphash
        # (ts_us, dur_us, max_us, total_us, imb, div, draws, sysmem)
        self.scalars = collections.deque(maxlen=MAX_SCALARS)
        self.bin_samples = collections.deque(maxlen=MAX_BIN_SAMPLES)  # (gw, gh, bins)
        self.grid = (0, 0)
        self.fb = (0, 0)
        self.tile0 = (0, 0)
        self.layers = 0
        self.total_samples = 0
        self.gmem_samples = 0
        self.last_seen = time.time()

    def add(self, kind, ts, dur, draws, gw, gh, div, max_us, bins,
            fb=(0, 0), tile0=(0, 0), layers=0):
        sysmem = kind == "sysmem"
        if bins:
            imb = max(bins) / (sum(bins) / len(bins)) if sum(bins) else 1.0
            total = sum(bins)
            self.bin_samples.append((gw, gh, bins))
            self.grid = (gw, gh)
            self.fb = fb
            self.tile0 = tile0
            self.layers = layers
        else:
            imb, total = 0.0, 0
        self.scalars.append((ts, dur, max_us, total, round(imb, 3), div, draws, sysmem))
        self.total_samples += 1
        if not sysmem:
            self.gmem_samples += 1
        self.last_seen = time.time()

    def summary(self):
        last = self.scalars[-1]
        recent = list(self.scalars)[-AVG_WINDOW:]
        n = len(recent)
        gmem_recent = [s for s in recent if not s[7]]
        if self.gmem_samples == 0:
            kind = "sysmem"
        elif self.gmem_samples == self.total_samples:
            kind = "gmem"
        else:
            kind = "mixed"
        return {
            "hash": "%016x" % self.rphash,
            "kind": kind,
            "grid": "%dx%d" % self.grid if self.grid[0] else "-",
            "tile0": "%dx%d" % self.tile0 if self.tile0[0] else "-",
            "fb": "%dx%d" % self.fb if self.fb[0] else "-",
            "layers": self.layers,
            "div": last[5],
            "draws": last[6],
            "last_dur_us": last[1],
            "avg_dur_us": round(sum(s[1] for s in recent) / n, 1),
            "last_max_us": last[2],
            "avg_max_us": round(sum(s[2] for s in gmem_recent) / len(gmem_recent), 1)
                          if gmem_recent else 0,
            "imb": last[4],
            "samples": self.total_samples,
            "age": round(time.time() - self.last_seen, 1),
        }

    def detail(self):
        """Everything the per-RP tabs need: latest bins, same-grid mean bins,
        downsampled scalar series, and per-divisor aggregate stats."""
        gw, gh, bins = self.bin_samples[-1] if self.bin_samples else (0, 0, [])
        fb_w, fb_h = self.fb
        tile0_w, tile0_h = self.tile0
        same = [b for w, h, b in self.bin_samples if (w, h) == (gw, gh) and len(b) == len(bins)]
        avg_bins = [round(sum(col) / len(same), 1) for col in zip(*same)] if bins else []

        scalars = list(self.scalars)
        stride = max(1, len(scalars) // 800)
        series = scalars[::stride]
        if scalars and series[-1] is not scalars[-1]:
            series.append(scalars[-1])

        div_stats = {}
        for ts, dur, max_us, total, imb, div, draws, sysmem in scalars:
            if sysmem or not max_us:
                continue
            d = div_stats.setdefault(div, [0, 0.0, 0.0, 0.0, 0.0])
            d[0] += 1
            d[1] += max_us
            d[2] += total
            d[3] += dur
            d[4] += imb
        div_rows = [
            {"div": div, "n": n, "avg_max_us": round(smax / n, 1),
             "avg_total_us": round(stot / n, 1), "avg_dur_us": round(sdur / n, 1),
             "avg_imb": round(simb / n, 2),
             "eff_pct": round(100.0 * n / simb, 1) if simb else 0}
            for div, (n, smax, stot, sdur, simb) in sorted(div_stats.items())
        ]
        return {
            "hash": "%016x" % self.rphash,
            "grid_w": gw, "grid_h": gh,
            "fb_w": fb_w, "fb_h": fb_h,
            "tile0_w": tile0_w, "tile0_h": tile0_h,
            "layers": self.layers,
            "bins": bins, "avg_bins": avg_bins, "avg_n": len(same),
            "series": [[s[0], s[1], s[2], s[3], s[4], s[5]] for s in series],
            "div_stats": div_rows,
            "samples": self.total_samples,
        }


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.rps = {}            # rphash -> RpData
        self.lines = 0
        self.parse_errors = 0
        self.latest_ts = 0
        self.misc_log = collections.deque(maxlen=400)
        self.ssh_status = "starting"

    def feed(self, line):
        m = LINE_RE.search(line)
        with self.lock:
            self.lines += 1
            if not m:
                # non-RP messages (e.g. "pid=%d tiling ..." divisor decisions) go to the log
                self.misc_log.append("%s %s" % (time.strftime("%H:%M:%S"), line.strip()))
                if not TILING_RE.search(line):
                    self.parse_errors += 1
                return
            rphash = int(m.group(1))
            kind = m.group(2)
            ts, dur, draws = int(m.group(3)), int(m.group(4)), int(m.group(5))
            fb = tile0 = (0, 0)
            layers = 0
            if m.group(6) is not None:
                gw, gh, div, max_us = (int(m.group(i)) for i in (6, 7, 8, 14))
                if m.group(9) is not None:
                    fb = (int(m.group(9)), int(m.group(10)))
                    tile0 = (int(m.group(11)), int(m.group(12)))
                    layers = int(m.group(13))
                bins = [int(t) for t in m.group(15).split(",") if t.strip()]
            else:
                gw = gh = div = max_us = 0
                bins = []
            rp = self.rps.get(rphash)
            if rp is None:
                rp = self.rps[rphash] = RpData(rphash)
            rp.add(kind, ts, dur, draws, gw, gh, div, max_us, bins,
                   fb=fb, tile0=tile0, layers=layers)
            self.latest_ts = max(self.latest_ts, ts)

    def clear(self):
        with self.lock:
            self.rps.clear()
            self.lines = 0
            self.parse_errors = 0
            self.latest_ts = 0
            self.misc_log.clear()

    def overview(self):
        """Downsampled (ts, dur, max, total) series for the top-N RPs by recent
        total duration — drives the all-RPs timeline."""
        def recent_cost(rp):
            return sum(s[1] for s in list(rp.scalars)[-AVG_WINDOW:])
        top = sorted(self.rps.values(), key=recent_cost, reverse=True)[:TOP_N_TIMELINE]
        out = []
        for rp in top:
            scalars = list(rp.scalars)
            stride = max(1, len(scalars) // 400)
            series = scalars[::stride]
            if scalars and series[-1] is not scalars[-1]:
                series.append(scalars[-1])
            out.append({"hash": "%016x" % rp.rphash,
                        "series": [[s[0], s[1], s[2], s[3]] for s in series]})
        return out

    def snapshot(self, selected_hash):
        with self.lock:
            detail = None
            if selected_hash is not None:
                rp = self.rps.get(selected_hash)
                if rp is not None and rp.scalars:
                    detail = rp.detail()
            return {
                "type": "update",
                "ssh": self.ssh_status,
                "lines": self.lines,
                "parse_errors": self.parse_errors,
                "latest_ts": self.latest_ts,
                "rps": [rp.summary() for rp in self.rps.values()],
                "overview": self.overview(),
                "detail": detail,
                "log": list(self.misc_log),
            }


STATE = State()


def ssh_reader(args):
    """Run the remote listener over ssh and feed its stdout into STATE. Retries."""
    while True:
        cmd = ["ssh", args.ssh_host, "python3", "-u", "-", args.socket_path]
        try:
            proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
        except OSError as e:
            STATE.ssh_status = "ssh failed: %s" % e
            time.sleep(3)
            continue
        proc.stdin.write(REMOTE_LISTENER.encode())
        proc.stdin.close()
        STATE.ssh_status = "connected (%s)" % args.ssh_host
        for raw in proc.stdout:
            STATE.feed(raw.decode("utf-8", errors="replace"))
        rc = proc.wait()
        STATE.ssh_status = "disconnected (exit %s), retrying..." % rc
        time.sleep(3)


# ---------------------------------------------------------------------------
# Web GUI
# ---------------------------------------------------------------------------

HTML = r"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>turnip bin timings</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; font: 13px/1.4 -apple-system, "Segoe UI", sans-serif;
         background: #14161a; color: #d8dce2; display: flex; flex-direction: column; height: 100vh; }
  #topbar { padding: 6px 12px; background: #1d2026; border-bottom: 1px solid #2c313a;
            display: flex; gap: 14px; align-items: center; flex: none; flex-wrap: wrap; }
  .stat { color: #8b93a1; }
  .stat b { color: #d8dce2; font-weight: 600; }
  button { background: #2c313a; color: #d8dce2; border: 1px solid #3c424e;
           border-radius: 4px; padding: 3px 10px; cursor: pointer; font: inherit; }
  button:hover { background: #3c424e; }
  button.active { background: #3a6ea5; border-color: #3a6ea5; }
  #main { display: flex; flex: 1; min-height: 0; }
  #listpane { width: 540px; flex: none; overflow-y: auto; border-right: 1px solid #2c313a; }
  table { width: 100%; border-collapse: collapse; }
  th { position: sticky; top: 0; background: #1d2026; text-align: right; padding: 5px 6px;
       cursor: pointer; user-select: none; color: #8b93a1; font-weight: 600; white-space: nowrap; z-index: 1; }
  th:first-child { text-align: left; }
  th.sorted { color: #6ab0f3; }
  td { padding: 3px 6px; text-align: right; border-bottom: 1px solid #1d2026;
       font-variant-numeric: tabular-nums; white-space: nowrap; }
  td:first-child { text-align: left; font-family: ui-monospace, monospace; color: #9fb3c8; }
  tr.row { cursor: pointer; }
  tr.row:hover { background: #22262d; }
  tr.row.sel { background: #2a3b50; }
  tr.row.stale td { opacity: 0.45; }
  td.sysmem-kind { color: #c678dd; }
  #rightpane { flex: 1; display: flex; flex-direction: column; min-width: 0; }
  #tabs { display: flex; gap: 6px; padding: 8px 14px 0 14px; flex: none; }
  #viewinfo { padding: 8px 14px; color: #8b93a1; flex: none; min-height: 20px; }
  #viewinfo b { color: #d8dce2; }
  #viewinfo .warn { color: #e5c07b; }
  #chartwrap { flex: 1; min-height: 0; position: relative; }
  canvas { position: absolute; inset: 0; width: 100%; height: 100%; }
  #tooltip { position: absolute; pointer-events: none; background: #1d2026; border: 1px solid #3c424e;
             border-radius: 4px; padding: 4px 8px; display: none; font-variant-numeric: tabular-nums; z-index: 2; }
  #divtable-wrap { display: none; padding: 0 14px; flex: none; max-height: 40%; overflow-y: auto; }
  #divtable-wrap table { width: auto; min-width: 70%; }
  #divtable-wrap th { position: static; cursor: default; }
  #subctl { display: none; gap: 6px; padding: 0 14px 6px 14px; flex: none; align-items: center; }
  #logwrap { display: none; flex: 1; min-height: 0; overflow-y: auto; padding: 0 14px 10px 14px; }
  #logwrap pre { margin: 0; font: 12px ui-monospace, monospace; color: #9fb3c8; white-space: pre-wrap; }
  #logwrap pre .revert { color: #e06c75; }
  #logwrap pre .kept { color: #98c379; }
</style>
</head>
<body>
<div id="topbar">
  <span class="stat">ssh: <b id="ssh">-</b></span>
  <span class="stat">RPs: <b id="nrps">0</b></span>
  <span class="stat">msgs: <b id="nlines">0</b></span>
  <span class="stat" id="errwrap" style="display:none">parse errors: <b id="nerrs">0</b></span>
  <button id="pause">Pause</button>
  <button id="clear">Clear</button>
  <span class="stat" id="conn">connecting...</span>
</div>
<div id="main">
  <div id="listpane">
    <table>
      <thead><tr id="hdr">
        <th data-k="hash">render pass</th>
        <th data-k="kind">mode</th>
        <th data-k="grid">grid</th>
        <th data-k="tile0">tile0</th>
        <th data-k="div">÷</th>
        <th data-k="avg_dur_us">avg&nbsp;dur</th>
        <th data-k="last_max_us">max&nbsp;µs</th>
        <th data-k="avg_max_us">avg&nbsp;max</th>
        <th data-k="imb">imb</th>
        <th data-k="samples">#</th>
      </tr></thead>
      <tbody id="rows"></tbody>
    </table>
  </div>
  <div id="rightpane">
    <div id="tabs">
      <button data-tab="bins" class="active">Bins</button>
      <button data-tab="heatmap">Heatmap</button>
      <button data-tab="timeline">Timeline</button>
      <button data-tab="divisor">Divisor</button>
      <button data-tab="log">Log</button>
    </div>
    <div id="viewinfo">select a render pass</div>
    <div id="subctl"></div>
    <div id="divtable-wrap"></div>
    <div id="logwrap"><pre id="logpre"></pre></div>
    <div id="chartwrap">
      <canvas id="chart"></canvas>
      <div id="tooltip"></div>
    </div>
  </div>
</div>
<script>
"use strict";
let ws = null, paused = false, selected = null, tab = "bins";
let sortKey = "avg_dur_us", sortDesc = true;
let lastMsg = null, detail = null;
let heatMode = "latest";                  // latest | mean
let tlMetric = 1, tlWindow = 30;          // series idx: 1=dur 2=max 3=total; window seconds (0=all)
const TL_METRICS = [null, "RP dur µs", "max tile µs", "Σ tiles µs"];
const PALETTE = ["#6ab0f3","#98c379","#e06c75","#e5c07b","#c678dd","#56b6c2","#d19a66","#7f8ea3"];

function connect() {
  ws = new WebSocket("ws://" + location.host + "/ws");
  ws.onopen = () => { document.getElementById("conn").textContent = "";
                      if (selected) ws.send(JSON.stringify({select: selected})); };
  ws.onclose = () => { document.getElementById("conn").textContent = "ws disconnected, retrying...";
                       setTimeout(connect, 1500); };
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (paused) return;
    lastMsg = msg;
    if (msg.detail) detail = msg.detail;
    render();
  };
}

function fmtUs(v) {
  if (v >= 10000) return (v / 1000).toFixed(1) + " ms";
  return Math.round(v) + " µs";
}
function shortHash(h) { return h.slice(0, 12); }

function render() {
  if (!lastMsg) return;
  document.getElementById("ssh").textContent = lastMsg.ssh;
  document.getElementById("nrps").textContent = lastMsg.rps.length;
  document.getElementById("nlines").textContent = lastMsg.lines;
  if (lastMsg.parse_errors > 0) {
    document.getElementById("errwrap").style.display = "";
    document.getElementById("nerrs").textContent = lastMsg.parse_errors;
  }
  const rps = lastMsg.rps.slice().sort((a, b) => {
    const va = a[sortKey], vb = b[sortKey];
    return (va < vb ? -1 : va > vb ? 1 : 0) * (sortDesc ? -1 : 1);
  });
  const tb = document.getElementById("rows");
  tb.innerHTML = "";
  for (const rp of rps) {
    const tr = document.createElement("tr");
    tr.className = "row" + (rp.hash === selected ? " sel" : "") + (rp.age > 5 ? " stale" : "");
    tr.innerHTML = "<td>" + shortHash(rp.hash) + "</td><td class='" +
      (rp.kind === "sysmem" ? "sysmem-kind" : "") + "'>" + rp.kind + "</td><td>" + rp.grid +
      "</td><td>" + rp.tile0 + "</td><td>" + (rp.div || "-") + "</td><td>" + rp.avg_dur_us +
      "</td><td>" + rp.last_max_us + "</td><td>" + rp.avg_max_us +
      "</td><td>" + (rp.imb ? rp.imb.toFixed(2) : "-") + "</td><td>" + rp.samples + "</td>";
    tr.onclick = () => {
      selected = rp.hash;
      detail = null;
      if (ws && ws.readyState === 1) ws.send(JSON.stringify({select: selected}));
      render();
    };
    tb.appendChild(tr);
  }
  for (const th of document.querySelectorAll("#hdr th"))
    th.classList.toggle("sorted", th.dataset.k === sortKey);
  drawView();
}

// ---------------------------------------------------------------- canvas core
const canvas = document.getElementById("chart");
const tooltip = document.getElementById("tooltip");
let hitRegions = [];   // [x, y, w, h, text]

function setupCanvas() {
  const wrap = document.getElementById("chartwrap");
  const dpr = window.devicePixelRatio || 1;
  const W = wrap.clientWidth, H = wrap.clientHeight;
  canvas.width = W * dpr; canvas.height = H * dpr;
  const ctx = canvas.getContext("2d");
  ctx.scale(dpr, dpr);
  ctx.clearRect(0, 0, W, H);
  hitRegions = [];
  return [ctx, W, H];
}

function yAxis(ctx, maxV, padL, padR, padT, ch, W, fmt) {
  ctx.font = "11px sans-serif";
  ctx.fillStyle = "#8b93a1"; ctx.strokeStyle = "#22262d";
  for (let i = 0; i <= 5; i++) {
    const v = maxV * i / 5, y = padT + ch - ch * i / 5;
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(W - padR, y); ctx.stroke();
    ctx.textAlign = "right";
    ctx.fillText(fmt(v), padL - 6, y + 4);
  }
}

function setInfo(html) { document.getElementById("viewinfo").innerHTML = html; }

function drawView() {
  document.getElementById("divtable-wrap").style.display = tab === "divisor" ? "block" : "none";
  document.getElementById("logwrap").style.display = tab === "log" ? "block" : "none";
  document.getElementById("chartwrap").style.display = tab === "log" ? "none" : "block";
  renderSubctl();
  if (tab === "bins") drawBins();
  else if (tab === "heatmap") drawHeatmap();
  else if (tab === "timeline") drawTimeline();
  else if (tab === "divisor") drawDivisor();
  else if (tab === "log") drawLog();
}

function drawLog() {
  setInfo("driver messages (tile-divisor decisions log once per framebuffer — restart the app to recapture)");
  const esc = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;");
  document.getElementById("logpre").innerHTML = ((lastMsg && lastMsg.log) || []).map(l =>
    esc(l).replace(/verdict=revert/, '<span class="revert">verdict=revert</span>')
          .replace(/verdict=kept/, '<span class="kept">verdict=kept</span>')).join("\n")
    || "(nothing yet)";
}

function needDetail() {
  if (!selected) { setInfo("select a render pass"); setupCanvas(); return null; }
  if (!detail || detail.hash !== selected) {
    setInfo("waiting for data for " + shortHash(selected) + "...");
    setupCanvas();
    return null;
  }
  return detail;
}

// ---------------------------------------------------------------- subcontrols
function renderSubctl() {
  const sc = document.getElementById("subctl");
  sc.innerHTML = "";
  if (tab === "heatmap") {
    sc.style.display = "flex";
    for (const m of ["latest", "mean"]) {
      const b = document.createElement("button");
      b.textContent = m;
      b.className = heatMode === m ? "active" : "";
      b.onclick = () => { heatMode = m; drawView(); };
      sc.appendChild(b);
    }
  } else if (tab === "timeline") {
    sc.style.display = "flex";
    for (let m = 1; m <= 3; m++) {
      const b = document.createElement("button");
      b.textContent = TL_METRICS[m];
      b.className = tlMetric === m ? "active" : "";
      b.onclick = () => { tlMetric = m; drawView(); };
      sc.appendChild(b);
    }
    const sep = document.createElement("span");
    sep.style.width = "20px";
    sc.appendChild(sep);
    for (const w of [10, 30, 120, 0]) {
      const b = document.createElement("button");
      b.textContent = w ? w + "s" : "all";
      b.className = tlWindow === w ? "active" : "";
      b.onclick = () => { tlWindow = w; drawView(); };
      sc.appendChild(b);
    }
  } else {
    sc.style.display = "none";
  }
}

// ---------------------------------------------------------------- bins tab
function drawBins() {
  const d = needDetail();
  if (!d) return;
  const [ctx, W, H] = setupCanvas();
  const bins = d.bins;
  if (!bins.length) { setInfo("<b>" + shortHash(d.hash) + "</b> has no tile data (sysmem-only?)"); return; }
  const expect = d.grid_w * d.grid_h;
  const truncated = bins.length < expect
      ? " <span class='warn'>— showing " + bins.length + " of " + expect + " bins (datagram truncated)</span>" : "";
  setInfo("<b>" + shortHash(d.hash) + "</b> &nbsp; " + d.grid_w + "×" + d.grid_h +
      " bins, max " + fmtUs(Math.max(...bins)) + ", avg overlay over last " + d.avg_n + " samples" + truncated);

  const padL = 56, padR = 14, padT = 14, padB = 30;
  const cw = W - padL - padR, ch = H - padT - padB;
  const maxV = Math.max(...bins, ...(d.avg_bins.length ? d.avg_bins : [0]), 1);
  const maxIdx = bins.indexOf(Math.max(...bins));
  yAxis(ctx, maxV, padL, padR, padT, ch, W, fmtUs);

  const bw = cw / bins.length;
  for (let i = 0; i < bins.length; i++) {
    const h = ch * bins[i] / maxV;
    const x = padL + i * bw, y = padT + ch - h;
    ctx.fillStyle = i === maxIdx ? "#e06c75" : "#3a6ea5";
    const w = Math.max(bw - Math.min(2, bw * 0.2), 1);
    ctx.fillRect(x, y, w, h);
    let t = "bin " + i + " (" + (i % d.grid_w) + "," + Math.floor(i / d.grid_w) + "): " + bins[i] + " µs";
    if (d.avg_bins.length > i) t += " (avg " + d.avg_bins[i] + ")";
    hitRegions.push([x, padT, w, ch, t]);
  }
  if (d.avg_bins.length === bins.length && d.avg_n > 1) {
    ctx.strokeStyle = "#e5c07b"; ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let i = 0; i < bins.length; i++) {
      const y = padT + ch - ch * d.avg_bins[i] / maxV;
      const x = padL + i * bw + bw / 2;
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    }
    ctx.stroke();
    ctx.lineWidth = 1;
  }
  ctx.fillStyle = "#8b93a1"; ctx.textAlign = "center";
  const lblStep = Math.max(1, Math.ceil(bins.length / Math.floor(cw / 40)));
  for (let i = 0; i < bins.length; i += lblStep)
    ctx.fillText(i, padL + i * bw + bw / 2, H - padB + 16);
}

// ---------------------------------------------------------------- heatmap tab
function heatColor(t) {
  // simple dark-blue -> cyan -> yellow -> red ramp
  const stops = [[0,[20,30,60]],[0.33,[30,110,165]],[0.66,[229,192,123]],[1,[224,108,117]]];
  for (let i = 1; i < stops.length; i++) {
    if (t <= stops[i][0]) {
      const [t0, c0] = stops[i-1], [t1, c1] = stops[i];
      const f = (t - t0) / (t1 - t0);
      return "rgb(" + c0.map((c, j) => Math.round(c + (c1[j] - c) * f)).join(",") + ")";
    }
  }
  return "rgb(224,108,117)";
}

function drawHeatmap() {
  const d = needDetail();
  if (!d) return;
  const [ctx, W, H] = setupCanvas();
  const useMean = heatMode === "mean" && d.avg_bins.length === d.bins.length && d.avg_n > 1;
  const bins = useMean ? d.avg_bins : d.bins;
  const gw = d.grid_w, gh = d.grid_h;
  if (!bins.length || !gw || !gh) {
    setInfo("<b>" + shortHash(d.hash) + "</b> has no tile data (sysmem-only?)");
    return;
  }
  const maxV = Math.max(...bins), sum = bins.reduce((a, b) => a + b, 0);
  const mean = sum / bins.length;
  const cv = Math.sqrt(bins.reduce((a, b) => a + (b - mean) * (b - mean), 0) / bins.length) / (mean || 1);
  const eff = mean / (maxV || 1);
  const truncated = bins.length < gw * gh
      ? " <span class='warn'>— only " + bins.length + " of " + (gw * gh) + " bins (truncated)</span>" : "";
  const layout = d.fb_w ? " &nbsp;·&nbsp; fb " + d.fb_w + "×" + d.fb_h + " px, tile " +
      d.tile0_w + "×" + d.tile0_h + " px" + (d.layers > 1 ? ", " + d.layers + " layers" : "") : "";
  setInfo("<b>" + shortHash(d.hash) + "</b> &nbsp; " + gw + "×" + gh +
      (useMean ? " (mean of " + d.avg_n + " samples)" : " (latest sample)") + layout +
      " &nbsp;·&nbsp; max <b>" + fmtUs(maxV) + "</b>, mean <b>" + fmtUs(mean) +
      "</b>, imbalance <b>" + (maxV / (mean || 1)).toFixed(2) + "×</b>, CV <b>" + cv.toFixed(2) +
      "</b> &nbsp;·&nbsp; division efficiency <b>" + Math.round(eff * 100) +
      "%</b> (perfectly uniform load would put the max at the mean)" + truncated);

  const padL = 20, padR = 70, padT = 16, padB = 16;
  const cell = Math.min((W - padL - padR) / gw, (H - padT - padB) / gh);
  const ox = padL + ((W - padL - padR) - cell * gw) / 2;
  const oy = padT + ((H - padT - padB) - cell * gh) / 2;
  for (let i = 0; i < bins.length; i++) {
    const x = i % gw, y = Math.floor(i / gw);
    const px = ox + x * cell, py = oy + y * cell;
    ctx.fillStyle = heatColor(maxV ? bins[i] / maxV : 0);
    ctx.fillRect(px, py, Math.max(cell - 1, 1), Math.max(cell - 1, 1));
    hitRegions.push([px, py, cell, cell,
      "tile (" + x + "," + y + "): " + Math.round(bins[i]) + " µs (" +
      (maxV ? Math.round(100 * bins[i] / maxV) : 0) + "% of max)"]);
  }
  // color scale
  const sx = W - padR + 18, sh = Math.min(220, H - 60), sy = (H - sh) / 2;
  for (let i = 0; i < sh; i++) {
    ctx.fillStyle = heatColor(1 - i / sh);
    ctx.fillRect(sx, sy + i, 14, 1);
  }
  ctx.fillStyle = "#8b93a1"; ctx.font = "11px sans-serif"; ctx.textAlign = "left";
  ctx.fillText(fmtUs(maxV), sx + 18, sy + 8);
  ctx.fillText("0", sx + 18, sy + sh);
}

// ---------------------------------------------------------------- timeline tab
function drawTimeline() {
  const [ctx, W, H] = setupCanvas();
  if (!lastMsg || !lastMsg.overview.length) { setInfo("no data yet"); return; }
  const latest = lastMsg.latest_ts;
  const lines = lastMsg.overview.slice();
  if (selected && detail && detail.hash === selected &&
      !lines.some(l => l.hash === selected))
    lines.push({hash: selected, series: detail.series});

  const t0 = tlWindow ? latest - tlWindow * 1e6 : Math.min(
      ...lines.map(l => l.series.length ? l.series[0][0] : latest));
  let maxV = 1;
  for (const l of lines)
    for (const p of l.series)
      if (p[0] >= t0) maxV = Math.max(maxV, p[tlMetric]);

  setInfo("top " + lines.length + " render passes by recent cost — <b>" + TL_METRICS[tlMetric] +
          "</b> over GPU time (click a legend entry to select)");

  const padL = 60, padR = 14, padT = 30, padB = 30;
  const cw = W - padL - padR, ch = H - padT - padB;
  yAxis(ctx, maxV, padL, padR, padT, ch, W, fmtUs);
  const span = Math.max(latest - t0, 1);
  const X = ts => padL + cw * (ts - t0) / span;
  const Y = v => padT + ch - ch * v / maxV;

  // x labels (seconds ago)
  ctx.fillStyle = "#8b93a1"; ctx.textAlign = "center";
  for (let i = 0; i <= 6; i++) {
    const ts = t0 + span * i / 6;
    ctx.fillText("-" + ((latest - ts) / 1e6).toFixed(1) + "s", padL + cw * i / 6, H - padB + 16);
  }

  let lx = padL;
  lines.forEach((l, li) => {
    const col = l.hash === selected ? "#ffffff" : PALETTE[li % PALETTE.length];
    ctx.strokeStyle = col;
    ctx.lineWidth = l.hash === selected ? 2.2 : 1.2;
    ctx.beginPath();
    let started = false;
    for (const p of l.series) {
      if (p[0] < t0) continue;
      const x = X(p[0]), y = Y(p[tlMetric]);
      started ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      started = true;
    }
    ctx.stroke();
    // legend
    ctx.fillStyle = col; ctx.fillRect(lx, 12, 10, 10);
    ctx.fillStyle = "#d8dce2"; ctx.textAlign = "left"; ctx.font = "11px ui-monospace, monospace";
    ctx.fillText(shortHash(l.hash), lx + 14, 21);
    hitRegions.push([lx, 8, 100, 16, "\x01select:" + l.hash]);
    lx += 110;
  });
  ctx.lineWidth = 1;

  // divisor change markers for the selected RP (series idx 5 = div)
  if (selected && detail && detail.hash === selected) {
    let prev = null;
    ctx.strokeStyle = "#e5c07b"; ctx.fillStyle = "#e5c07b"; ctx.setLineDash([4, 4]);
    ctx.font = "11px sans-serif"; ctx.textAlign = "center";
    for (const p of detail.series) {
      if (prev !== null && p[5] !== prev && p[0] >= t0) {
        const x = X(p[0]);
        ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, padT + ch); ctx.stroke();
        ctx.fillText("÷" + p[5], x, padT - 4);
      }
      prev = p[5];
    }
    ctx.setLineDash([]);
  }
}

// ---------------------------------------------------------------- divisor tab
function drawDivisor() {
  const wrap = document.getElementById("divtable-wrap");
  const d = needDetail();
  if (!d) { wrap.innerHTML = ""; return; }
  if (!d.div_stats.length) {
    setInfo("<b>" + shortHash(d.hash) + "</b> has no gmem tile samples yet");
    wrap.innerHTML = "";
    setupCanvas();
    return;
  }
  setInfo("<b>" + shortHash(d.hash) + "</b> — per tile_size_divisor stats. If load were uniform, " +
          "avg max would scale ~1/÷ and efficiency would stay near 100%. Low efficiency at high ÷ " +
          "means the hot spot doesn't subdivide away.");
  let h = "<table><thead><tr><th>÷</th><th>samples</th><th>avg max tile</th>" +
          "<th>avg Σ tiles</th><th>avg RP dur</th><th>avg imbalance</th><th>efficiency</th></tr></thead><tbody>";
  for (const r of d.div_stats) {
    h += "<tr><td>" + r.div + "</td><td>" + r.n + "</td><td>" + fmtUs(r.avg_max_us) +
         "</td><td>" + fmtUs(r.avg_total_us) + "</td><td>" + fmtUs(r.avg_dur_us) +
         "</td><td>" + r.avg_imb.toFixed(2) + "×</td><td>" + r.eff_pct + "%</td></tr>";
  }
  wrap.innerHTML = h + "</tbody></table>";

  // scatter: max tile µs over time, colored by divisor
  const [ctx, W, H] = setupCanvas();
  const pts = detail.series.filter(p => p[2] > 0);
  if (!pts.length) return;
  const latest = lastMsg.latest_ts;
  const t0 = pts[0][0];
  const maxV = Math.max(...pts.map(p => p[2]), 1);
  const padL = 60, padR = 14, padT = 14, padB = 30;
  const cw = W - padL - padR, ch = H - padT - padB;
  yAxis(ctx, maxV, padL, padR, padT, ch, W, fmtUs);
  const span = Math.max(latest - t0, 1);
  const divsSeen = [...new Set(pts.map(p => p[5]))].sort((a, b) => a - b);
  for (const p of pts) {
    const x = padL + cw * (p[0] - t0) / span;
    const y = padT + ch - ch * p[2] / maxV;
    ctx.fillStyle = PALETTE[divsSeen.indexOf(p[5]) % PALETTE.length];
    ctx.fillRect(x - 1.5, y - 1.5, 3, 3);
    hitRegions.push([x - 3, y - 3, 6, 6, "max " + p[2] + " µs, ÷" + p[5] + ", imb " + p[4] + "×"]);
  }
  let lx = padL;
  ctx.font = "11px sans-serif";
  for (const dv of divsSeen) {
    ctx.fillStyle = PALETTE[divsSeen.indexOf(dv) % PALETTE.length];
    ctx.fillRect(lx, padT - 4, 10, 10);
    ctx.fillStyle = "#d8dce2"; ctx.textAlign = "left";
    ctx.fillText("÷" + dv, lx + 14, padT + 5);
    lx += 50;
  }
  ctx.fillStyle = "#8b93a1"; ctx.textAlign = "center";
  for (let i = 0; i <= 6; i++)
    ctx.fillText("-" + ((latest - (t0 + span * i / 6)) / 1e6).toFixed(1) + "s",
                 padL + cw * i / 6, H - padB + 16);
}

// ---------------------------------------------------------------- events
canvas.addEventListener("mousemove", (ev) => {
  const r = canvas.getBoundingClientRect();
  const mx = ev.clientX - r.left, my = ev.clientY - r.top;
  let text = null;
  for (const [x, y, w, h, t] of hitRegions)
    if (mx >= x && mx <= x + w && my >= y && my <= y + h) { text = t; }
  if (text && !text.startsWith("\x01")) {
    tooltip.style.display = "block";
    tooltip.style.left = Math.min(mx + 12, r.width - 220) + "px";
    tooltip.style.top = (my + 12) + "px";
    tooltip.textContent = text;
  } else tooltip.style.display = "none";
  canvas.style.cursor = text && text.startsWith("\x01") ? "pointer" : "default";
});
canvas.addEventListener("click", (ev) => {
  const r = canvas.getBoundingClientRect();
  const mx = ev.clientX - r.left, my = ev.clientY - r.top;
  for (const [x, y, w, h, t] of hitRegions)
    if (mx >= x && mx <= x + w && my >= y && my <= y + h && t.startsWith("\x01select:")) {
      selected = t.slice(8);
      detail = null;
      if (ws && ws.readyState === 1) ws.send(JSON.stringify({select: selected}));
      render();
      return;
    }
});
canvas.addEventListener("mouseleave", () => tooltip.style.display = "none");

document.getElementById("pause").onclick = (ev) => {
  paused = !paused;
  ev.target.classList.toggle("active", paused);
  ev.target.textContent = paused ? "Resume" : "Pause";
};
document.getElementById("clear").onclick = () => fetch("/clear", {method: "POST"});
document.getElementById("hdr").onclick = (ev) => {
  const k = ev.target.dataset.k;
  if (!k) return;
  if (sortKey === k) sortDesc = !sortDesc; else { sortKey = k; sortDesc = true; }
  render();
};
for (const b of document.querySelectorAll("#tabs button"))
  b.onclick = () => {
    tab = b.dataset.tab;
    for (const o of document.querySelectorAll("#tabs button"))
      o.classList.toggle("active", o === b);
    drawView();
  };
window.addEventListener("resize", drawView);
connect();
</script>
</body>
</html>
"""


class IndexHandler(tornado.web.RequestHandler):
    def get(self):
        self.set_header("Content-Type", "text/html; charset=utf-8")
        self.write(HTML)


class ClearHandler(tornado.web.RequestHandler):
    def post(self):
        STATE.clear()


class WsHandler(tornado.websocket.WebSocketHandler):
    clients = set()

    def open(self):
        self.selected = None
        WsHandler.clients.add(self)

    def on_message(self, message):
        try:
            msg = json.loads(message)
        except ValueError:
            return
        if "select" in msg:
            try:
                self.selected = int(msg["select"], 16)
            except (ValueError, TypeError):
                self.selected = None

    def on_close(self):
        WsHandler.clients.discard(self)


def broadcast():
    if not WsHandler.clients:
        return
    for client in list(WsHandler.clients):
        try:
            client.write_message(json.dumps(STATE.snapshot(client.selected)))
        except tornado.websocket.WebSocketClosedError:
            WsHandler.clients.discard(client)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ssh-host", default="hmd")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--socket-path", default="/tmp/mesa_tu_log.sock",
                    help="unix socket path on the device, make sure to set TU_DEBUG=logsocket in your env for the running app")
    args = ap.parse_args()

    threading.Thread(target=ssh_reader, args=(args,), daemon=True).start()

    app = tornado.web.Application([
        (r"/", IndexHandler),
        (r"/clear", ClearHandler),
        (r"/ws", WsHandler),
    ])
    app.listen(args.port, address="127.0.0.1")
    print("open http://localhost:%d" % args.port)
    tornado.ioloop.PeriodicCallback(broadcast, 500).start()
    try:
        tornado.ioloop.IOLoop.current().start()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
