/*
 * Boot shim for VectorET - Wolfenstein: Enemy Territory as a WebXDC app.
 *
 * Copyright (C) 2026 VectorET contributors
 * Licensed GPLv3, matching the engine it loads.
 *
 * Differences from a normal web build, all forced by the WebXDC sandbox:
 *
 *   - No network. Paks ship inside the .xdc and are read from relative URLs,
 *     never downloaded.
 *   - Pure MEMFS. The paks are already durable inside the app bundle, so
 *     copying them into IndexedDB would double the storage for nothing -
 *     and VectorQuake hit IDBFS corruption doing exactly that.
 *   - No server browser. One peer is elected host and runs the listen server;
 *     everyone else connects to its fake address. See webxdc-net.js.
 */

'use strict';

const statusEl = document.getElementById('status');
const roleEl = document.getElementById('role');
const barEl = document.getElementById('bar');
const logEl = document.getElementById('log');

// Objective, on a single map. The stock default is Campaign, which walks a
// six-map rotation - the trimmed pak only carries a couple of maps, so a
// campaign would dead-end on the first map it cannot find.
const GAMETYPE = '2';
const DEFAULT_MAP = 'radar';

// A `connect` tears down the client and restarts the UI, so retrying too
// eagerly aborts the handshake it is meant to rescue. The engine already
// retries the challenge on its own; this only covers the host being slow to
// finish loading its map.
const CONNECT_RETRY_MS = 15000;
const CONNECT_MAX_TRIES = 8;

function setStatus(msg, isErr) {
  statusEl.textContent = msg;
  statusEl.classList.toggle('err', !!isErr);
}

function setRole(msg) {
  roleEl.textContent = msg;
}

function setProgress(frac) {
  barEl.style.width = (Math.max(0, Math.min(1, frac)) * 100).toFixed(1) + '%';
}

function logLine(msg, isErr) {
  const div = document.createElement('div');
  if (isErr) div.className = 'err';
  div.textContent = msg;
  logEl.appendChild(div);
  while (logEl.childNodes.length > 400) logEl.removeChild(logEl.firstChild);
  logEl.scrollTop = logEl.scrollHeight;
}

statusEl.addEventListener('click', () => document.body.classList.toggle('showlog'));

/* -------------------------------------------------- assets */

async function fetchPaks(onProgress) {
  const manifest = await (await fetch('manifest.json')).json();
  const total = manifest.files.reduce((n, f) => n + (f.size || 0), 0);

  const out = [];
  let done = 0;

  for (const f of manifest.files) {
    const resp = await fetch(f.url);
    if (!resp.ok) {
      throw new Error(`${f.url}: HTTP ${resp.status}`);
    }

    const reader = resp.body.getReader();
    const chunks = [];
    let got = 0;
    for (;;) {
      const { done: fin, value } = await reader.read();
      if (fin) break;
      chunks.push(value);
      got += value.length;
      onProgress((done + got) / (total || 1));
    }

    const buf = new Uint8Array(got);
    let off = 0;
    for (const c of chunks) {
      buf.set(c, off);
      off += c.length;
    }

    out.push({ path: f.path, data: buf });
    done += got;
  }

  return out;
}

/* -------------------------------------------------- engine */

let paks = null;
let net = null;

var Module = {
  canvas: document.getElementById('canvas'),
  arguments: [],
  print: (t) => { console.log(t); logLine(t, false); },
  printErr: (t) => { console.error(t); logLine(t, true); },

  preRun: [function () {
    for (const d of ['/et', '/et/etmain', '/et/legacy', '/et/home']) {
      try { FS.mkdir(d); } catch (e) { /* exists */ }
    }
    for (const p of paks) {
      FS.writeFile('/et/' + p.path, p.data);
    }
    paks = null; // the bytes now live in the wasm heap; drop the JS copies
  }],

  onRuntimeInitialized: function () {
    net.attach(Module);
    document.body.classList.add('playing');
    Module.canvas.focus();

    if (!net.isHost) {
      startConnectRetry();
    }

    // reachable from the console as netdiag() while debugging the transport
    window.netdiag = () => ({
      role: net.isHost ? 'host' : 'client',
      myIp: VectorETNet.ipToString(net.myIp),
      hostIp: VectorETNet.ipToString(net.hostIp),
      js: { ...net.stats },
      wasm: {
        committed: Module._NET_WebxdcStat(0),
        delivered: Module._NET_WebxdcStat(1),
        pumpCalls: Module._NET_WebxdcStat(2),
        queued: Module._NET_WebxdcStat(3),
      },
    });
  },
};

/**
 * Keep asking to connect until the host answers.
 *
 * The host has to load a map before its server exists, which takes longer
 * than the engine's own connect timeout. Rather than guess how long, retry
 * until traffic actually arrives from the host.
 */
function startConnectRetry() {
  const target = `${VectorETNet.ipToString(net.hostIp)}:${VectorETNet.ENGINE_PORT}`;
  let tries = 0;

  const timer = setInterval(() => {
    if (net.stats.recv > 0) {
      clearInterval(timer);
      setRole('connected');
      return;
    }
    if (++tries > CONNECT_MAX_TRIES) {
      clearInterval(timer);
      setStatus('host never answered - reload to retry', true);
      return;
    }
    logLine(`[et-net] connect attempt ${tries} -> ${target}`);
    Module.ccall('VectorET_Exec', null, ['string'], [`connect ${target}\n`]);
  }, CONNECT_RETRY_MS);
}

/* -------------------------------------------------- start */

(async function () {
  try {
    setStatus('joining the channel');

    // Election and asset loading are independent; the election is short and
    // the paks are large, so overlapping them hides the election entirely.
    const [electedNet, loadedPaks] = await Promise.all([
      VectorETNet.elect(),
      fetchPaks((frac) => {
        setStatus(`loading game data - ${(frac * 100).toFixed(0)}%`);
        setProgress(frac);
      }),
    ]);

    net = electedNet;
    paks = loadedPaks;

    setProgress(1);

    const name = (window.webxdc && window.webxdc.selfName) || 'Soldier';
    const args = [
      '+set', 'fs_basepath', '/et',
      '+set', 'fs_homepath', '/et/home',
      '+set', 'r_fullscreen', '0',
      '+set', 'r_mode', '-1',
      '+set', 'r_customwidth', String(Math.min(1280, window.innerWidth || 1280)),
      '+set', 'r_customheight', String(Math.min(720, window.innerHeight || 720)),
      '+set', 'r_allowsoftwaregl', '1',
      '+set', 'r_ext_compiled_vertex_array', '0',
      '+set', 'r_fbo', '0',
      '+set', 'com_introplayed', '1',
      '+set', 'cl_motd', '0',
      '+set', 'cl_allowDownload', '0',
      '+set', 'sv_allowDownload', '0',
      // there is no internet here; without this the server heartbeats a
      // master list and those datagrams get broadcast to every peer
      '+set', 'sv_advertise', '0',
      '+set', 'sv_master1', '',
      '+set', 'sv_master2', '',
      '+set', 'sv_master3', '',
      '+set', 'sv_master4', '',
      '+set', 'sv_master5', '',
      '+set', 'name', name.slice(0, 30),
    ];

    if (net.isHost) {
      setRole('you are hosting');
      setStatus('starting the server');
      args.push(
        '+set', 'sv_pure', '0',
        '+set', 'g_gametype', GAMETYPE,
        '+set', 'sv_hostname', `${name}'s server`,
        '+map', DEFAULT_MAP
      );
    } else {
      const target = VectorETNet.ipToString(net.hostIp);
      setRole(`joining ${target}`);
      setStatus('connecting');
      args.push('+connect', `${target}:${VectorETNet.ENGINE_PORT}`);
    }

    Module.arguments = args;

    const s = document.createElement('script');
    s.src = 'etl.js';
    s.onerror = () => setStatus('failed to load the engine', true);
    document.body.appendChild(s);
  } catch (e) {
    setStatus('FAILED: ' + e.message, true);
    logLine(String(e.stack || e), true);
    document.body.classList.add('showlog');
  }
})();
