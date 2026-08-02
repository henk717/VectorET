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

/* -------------------------------------------------- input availability */

/**
 * Report what the player can actually steer with.
 *
 * Some webviews refuse Pointer Lock outright. Without it there is no mouse
 * look, and the old behaviour was to leave the player stuck staring straight
 * ahead with nothing said. A pad needs no pointer lock at all, so say so
 * rather than treating a missing mouse as the end of it.
 */
const hintEl = document.getElementById('inputhint');
const input = { pointerLock: null, pads: 0 };
let hintTimer = null;

// Long enough to read and act on, short enough not to sit over the game. The
// pointer-lock notice is only raised once per session anyway, so it will not
// come back and nag.
const HINT_HOLD_MS = 20000;

function showHint(html, holdMs) {
  hintEl.innerHTML = html;
  hintEl.classList.add('show');
  clearTimeout(hintTimer);
  if (holdMs) {
    hintTimer = setTimeout(() => hintEl.classList.remove('show'), holdMs);
  }
}

function refreshInputHint() {
  if (input.pads > 0) {
    showHint('<b>Controller ready.</b> Left stick moves, right stick looks, ' +
             'right trigger fires, <b>Start</b> for the menu.', 6000);
    return;
  }
  if (input.pointerLock === false) {
    showHint('<b>Mouse look unavailable</b> — this webview blocks pointer lock. ' +
             'Connect a controller and press any button on it, or use the ' +
             '<b>arrow keys</b> to turn.', HINT_HOLD_MS);
    return;
  }
  hintEl.classList.remove('show');
}

function pointerLockDenied() {
  if (input.pointerLock === false) {
    return; // SDL retries on every click; report it once
  }
  input.pointerLock = false;
  // one cursor, not two: see the .nolock rule in index.html
  document.body.classList.add('nolock');
  logLine('[input] pointer lock denied by this webview - use a controller');
  refreshInputHint();
}

// Two routes: the event fires when the request is rejected asynchronously,
// while a nested or sandboxed document throws a SecurityError synchronously
// out of requestPointerLock, which only the global error trap sees.
addEventListener('pointerlockerror', pointerLockDenied);
window.__etOnPointerLockDenied = pointerLockDenied;
addEventListener('pointerlockchange', () => {
  if (document.pointerLockElement) {
    input.pointerLock = true;
    document.body.classList.remove('nolock');
    refreshInputHint();
  }
});

// Browsers hide pads until a button is pressed, so this usually fires well
// after the engine has started - which is why the engine also had to learn
// to pick up controllers hot rather than only enumerating them at boot.
addEventListener('gamepadconnected', (e) => {
  input.pads++;
  logLine(`[input] gamepad connected: ${e.gamepad.id} ` +
          `(${e.gamepad.buttons.length} buttons, ${e.gamepad.axes.length} axes, ` +
          `mapping "${e.gamepad.mapping || 'none'}")`);
  refreshInputHint();
});
addEventListener('gamepaddisconnected', () => {
  input.pads = Math.max(0, input.pads - 1);
  refreshInputHint();
});

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
let inputCfg = null;

var Module = {
  canvas: document.getElementById('canvas'),
  arguments: [],
  print: (t) => { console.log(t); logLine(t, false); },
  printErr: (t) => { console.error(t); logLine(t, true); },

  preRun: [function () {
    // /et/home/legacy is where exec looks: the search path is
    // fs_homepath/fs_game before fs_basepath/fs_game, and fs_game is "legacy"
    for (const d of ['/et', '/et/etmain', '/et/legacy', '/et/home',
                     '/et/home/legacy']) {
      try { FS.mkdir(d); } catch (e) { /* exists */ }
    }
    for (const p of paks) {
      FS.writeFile('/et/' + p.path, p.data);
    }
    paks = null; // the bytes now live in the wasm heap; drop the JS copies

    if (inputCfg) {
      FS.writeFile('/et/home/legacy/input.cfg', inputCfg);
    }
  }],

  onRuntimeInitialized: function () {
    net.attach(Module);
    document.body.classList.add('playing');
    Module.canvas.focus();

    // from here on the global error trap only logs; a running game must not
    // be torn down by a stray non-fatal error
    window.__etRuntimeUp = true;

    if (!net.isHost) {
      startConnectRetry();
    }

    // If our claim crossed with a lower-id peer's, we may have started a
    // server nobody should be on. Shut it down and join theirs instead.
    net.onDemoted = (ip) => {
      const target = `${VectorETNet.ipToString(ip)}:${VectorETNet.ENGINE_PORT}`;
      setRole(`joining ${VectorETNet.ipToString(ip)}`);
      logLine(`[et-net] another host has priority, joining ${target}`);
      Module.ccall('VectorET_Exec', null, ['string'], ['killserver\n']);
      startConnectRetry();
    };

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
let connectTimer = null;

function startConnectRetry() {
  const target = `${VectorETNet.ipToString(net.hostIp)}:${VectorETNet.ENGINE_PORT}`;
  const baseline = net.stats.recvFromHost;
  let tries = 0;

  if (connectTimer) {
    clearInterval(connectTimer);
  }

  const attempt = () => {
    // measured against the host specifically: a demoted host may already have
    // traffic from its own former clients, which says nothing about us
    if (net.stats.recvFromHost > baseline) {
      clearInterval(connectTimer);
      connectTimer = null;
      setRole('connected');
      setStatus('in the field');
      return;
    }
    if (++tries > CONNECT_MAX_TRIES) {
      clearInterval(connectTimer);
      connectTimer = null;
      setStatus('host never answered - reload to retry', true);
      return;
    }
    logLine(`[et-net] connect attempt ${tries} -> ${target}`);
    Module.ccall('VectorET_Exec', null, ['string'], [`connect ${target}\n`]);
  };

  attempt();
  connectTimer = setInterval(attempt, CONNECT_RETRY_MS);
}

/* -------------------------------------------------- start */

(async function () {
  try {
    setStatus('joining the channel');

    // Election and asset loading are independent; the election is short and
    // the paks are large, so overlapping them hides the election entirely.
    const [electedNet, loadedPaks, cfg] = await Promise.all([
      VectorETNet.elect(),
      fetchPaks((frac) => {
        setStatus(`loading game data - ${(frac * 100).toFixed(0)}%`);
        setProgress(frac);
      }),
      fetch('input.cfg').then((r) => (r.ok ? r.text() : null)).catch(() => null),
    ]);
    inputCfg = cfg;

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
      // latched, so it has to be set before the input subsystem starts
      '+set', 'in_joystick', '1',
      '+exec', 'input.cfg',
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
