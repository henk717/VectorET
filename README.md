# VectorET

**Wolfenstein: Enemy Territory** as a WebXDC app — the real
[ET: Legacy](https://www.etlegacy.com) engine compiled to WebAssembly, playing
multiplayer over a WebXDC realtime channel. Drop the `.xdc` into a chat and
everyone in it can play the same match.

No server, no internet, no accounts. One peer is elected host and runs the
listen server inside its own browser tab; the rest connect to it over the
chat's realtime channel.

> Non-commercial fan project. Not affiliated with Splash Damage, id Software,
> ZeniMax or Microsoft. See [Licensing](#licensing).

## How the networking works

A WebXDC realtime channel is an unordered broadcast shared by everyone in the
chat. The engine wants a UDP LAN. Three pieces bridge them:

| Piece | Where | What |
|---|---|---|
| Identity | `web/webxdc-net.js` | each peer hashes `webxdc.selfAddr` into a stable fake IPv4 (`10.x.y.z`) |
| Framing | `web/webxdc-net.js` | 13-byte header `[type][destIP:4][destPort:2][srcIP:4][srcPort:2]`, so a broadcast behaves like a switched LAN |
| Transport | `src/src/sys/net_webxdc.c` | a ring buffer wired into the engine's two platform seams |

The engine hooks are the same two `net_ip.c` used for the WebSocket build:
`Sys_SendPacket` on the way out, `NET_Sleep` on the way in. Game code is
untouched — it thinks it is talking UDP to `10.26.61.23:27960`.

Host election is announce-first: a peer broadcasts DISCOVER, waits 900 ms, and
joins whoever answers. Only if nobody does do peers CLAIM, lowest id winning,
so a late joiner enters the running match instead of starting a rival server.

The addressing scheme is lifted from the Half-Life/Xash WebXDC port, which
solved the same problem first.

## Build

Needs the Emscripten SDK, CMake **≥ 4.3.3** (older CMake silently downgrades
Emscripten side modules to static libraries, and the mods then fail to
`dlopen`), and `vorbis-tools` for the audio step.

```sh
git clone -b webxdc https://github.com/JSKitty/etlegacy src
git clone -b etweb https://github.com/JSKitty/gl4es tools/gl4es
git -C src submodule update --init --depth 1
git clone https://github.com/emscripten-core/emsdk tools/emsdk
EMSDK_PYTHON=/opt/homebrew/bin/python3.11 tools/emsdk/emsdk install latest
EMSDK_PYTHON=/opt/homebrew/bin/python3.11 tools/emsdk/emsdk activate latest
```

Then:

```sh
source scripts/env.sh
./scripts/build-gl4es.sh
./scripts/build-engine.sh
```

Game data is **not** included. Fetch `mp_bin.pk3` and `pak0/1/2.pk3` from
`mirror.etlegacy.com/etmain/` into `assets/etmain/`, then:

```sh
./scripts/build-assets.sh              # 218 MB -> 75 MB
ASSET_DIR=assets-trimmed ./scripts/stage-web.sh
./scripts/package-xdc.sh               # -> VectorET.xdc, ~87 MB
```

## Assets

A `.xdc` has to travel through a chat message, so the whole archive is the
download. `scripts/trim-pak.py` cuts pak0 from 218 MB to 75 MB by:

- **Transcoding audio to Vorbis** — 101 MB of WAV becomes 13 MB of `.ogg`
  with *every sound kept*. This needed an engine fix: the codec picker only
  auto-detected when a name had no extension, so the thousands of hardcoded
  `.wav` references never fell back to `.ogg`. `S_CodecResolve()` now asks the
  codecs which file actually exists.
- **Keeping only the chosen maps** and walking their real dependencies:
  BSP shader lump → `scripts/*.shader` → images, plus MD3 surface shaders,
  entity models and `.skin` files, to a fixpoint.
- **Mining the mod binaries for asset strings.** cgame/qagame register plenty
  of assets from string literals in code (`models/mapobjects/supplystands/…`).
  No file format points at those, so the trimmer greps the wasm for
  asset-shaped strings — without this, things the mod spawns lose their skins.

Verified by booting each kept map and reading the console: **zero** missing
images or sounds. Two shader warnings remain (`gfx/2d/crosshairs_alt`,
`crosshairt_alt`); those files are absent from stock pak0 and warn identically
with the full 218 MB set.

## Controller support

Some webviews refuse Pointer Lock. Without it there is no mouse look, which
leaves the player staring straight ahead — so a gamepad is the fallback, and
ET: Legacy already has full `SDL_GameController` support to build on.

Two engine changes were needed to make it work in a browser:

- **SDL never recognises web gamepads as controllers.** SDL 2.30.9's
  Emscripten joystick driver returns `SDL_FALSE` from
  `GetGamepadMapping`, so a pad reported by the browser in the "standard"
  layout only ever appears as a raw joystick — and ET's analog stick look
  lives in the `SDL_CONTROLLERAXISMOTION` path, which then never runs.
  `IN_RegisterWebGamepadMappings()` registers the fixed W3C standard layout
  against each device's GUID so `SDL_GameControllerOpen` succeeds.
- **Pads arrive late.** Browsers hide gamepads until the user presses a
  button on one, and ET only enumerated joysticks once at startup. Handling
  `SDL_JOYDEVICEADDED`/`REMOVED` re-runs init, so a pad connected mid-game is
  picked up.

Bindings, sensitivity and a keyboard-look fallback live in [`web/input.cfg`](web/input.cfg), written
into `fs_homepath/legacy` at boot and exec'd. Left stick moves, right stick
looks, right trigger fires, Start opens the menu. `in_joystickUseAnalog` gates
*both* analog movement and stick look, so it has to be on.

Stock ET binds only WASD — there are no turn keys at all — so without a mouse
*or* a pad a player genuinely cannot look around. `input.cfg` therefore also
binds keyboard look on the arrow keys (`+left`/`+right`/`+lookup`/`+lookdown`,
`END` to centre), as a last resort that always works.

The shell reports what input is available rather than failing mute: it catches
a refused pointer lock and says so on screen, and announces a pad on
`gamepadconnected`. A refused lock arrives two ways — the `pointerlockerror`
event, and a `SecurityError` thrown synchronously out of
`requestPointerLock()` in a nested document — so both are handled, and both
are reported once rather than on every retry.

**A running game is never torn down by a non-fatal error.** The global error
trap only reports startup failures; once the runtime is up it logs and nothing
more. Getting this wrong once meant a refused pointer lock threw the player
back to the loading screen on their first click.

Verified end to end except the pad itself — config, bindings and cvars all
apply (`execing input.cfg`, `PAD0_A = "+moveup"`, `j_pitch 0.10`), and the
engine reports `Joystick initialization failed: no device available` with
nothing plugged in, which is the expected path. **Actual stick and button
input needs real hardware to confirm.** `\joystickInfo` in the console prints
what SDL sees.

## Testing

`web/dev-2p.html` runs two peers side by side, backed by a BroadcastChannel
stand-in for the WebXDC host (`web/webxdc-dev-shim.js`), so the whole election
and netcode path is testable without packaging anything.

```sh
node scripts/serve.mjs 8666      # then open /dev-2p.html
```

Both peers must be in **one visible document**. A hidden tab gets no
`requestAnimationFrame`, so its engine never runs a frame and the connection
stalls — which is why the harness uses iframes rather than two tabs.

`netdiag()` in either peer's console reports the transport end to end:

```js
{ role: 'host', myIp: '10.26.61.23',
  js:   { sent, recv, dropped, errors },      // JS side of the channel
  wasm: { committed, delivered, pumpCalls, queued } }  // engine side
```

Packets counted in `js.recv` but not in `wasm.committed` mean the bridge into
the engine is broken; `committed` without `delivered` means the engine is not
running frames.

## Changes to the engine

Against `harzzn/etlegacy@web`:

- `src/sys/net_webxdc.c` — the realtime-channel transport, replacing
  `net_web_tunnel.c`. The tunnel attributed every incoming packet to the last
  address sent to, which is fine for one dedicated server but collapses when a
  listen server has several clients; packets now carry real source addresses.
- `src/client/snd_codec.c`, `snd_mem.c` — `S_CodecResolve()`, so a sound can
  ship under a different codec than its name says.
- `libs/CMakeLists.txt` — forward `-fPIC` to bundled libs on Emscripten.
  `ETL_64BITS` is false for wasm32, so SDL2/freetype/jpegturbo built non-PIC
  and `wasm-ld` rejected them against the `MAIN_MODULE` client. Also pins
  `OGG_LIBRARY`/`OGG_INCLUDE_DIR` for vorbis, whose `FindOgg` cannot see
  outside the sysroot when cross-compiling.
- `cmake/ETLBuildClient.cmake` — export `HEAPU8`. Since Emscripten 6 the heap
  views are not on `Module` unless asked for, and the JS side of the transport
  writes packets straight into the ring buffer.

## Licensing

- **Engine** — ET: Legacy fork: **GPLv3**, from id Software's 2010 source
  release plus id's additional terms. Shipping `etl.wasm` inside a `.xdc`
  conveys a GPL binary, so the fork is the corresponding-source offer.
- **GL translation** — [gl4es](https://github.com/ptitSeb/gl4es) fork: MIT.
- **This repo** (shell, transport, tooling): GPLv3 to match.
- **Game data** is **not** included and **not** GPL — it remains the property
  of id Software / ZeniMax / Microsoft, distributed free of charge since 2003
  under the W:ET EULA.

## Credits

Built on [ET: Legacy](https://www.etlegacy.com), [gl4es](https://github.com/ptitSeb/gl4es),
and [harzzn/et-web](https://github.com/harzzn/et-web), which did the original
browser port this builds on.
