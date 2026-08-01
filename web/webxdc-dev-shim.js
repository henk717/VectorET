/*
 * Development-only stand-in for the WebXDC host API.
 *
 * Copyright (C) 2026 VectorET contributors
 * Licensed GPLv3.
 *
 * Backs joinRealtimeChannel with a BroadcastChannel so two ordinary browser
 * tabs behave like two peers in a chat. This makes the whole election and
 * netcode path testable without packaging an .xdc and loading it in Vector.
 *
 * Inert when a real webxdc host is present, so it is safe to ship - though
 * stage-xdc.sh drops it from release builds anyway.
 */

'use strict';

(function (global) {
  if (global.webxdc) {
    return; // real host - do nothing
  }

  console.warn('[et-dev] no WebXDC host; using a BroadcastChannel stand-in');

  // Identity must differ per peer but survive reloads. ?peer=<name> pins it,
  // which is what dev-2p.html uses: two iframes share one tab's sessionStorage
  // and would otherwise collide on a single identity - and two peers must be
  // in one visible document, because a hidden tab gets no requestAnimationFrame
  // and its engine would never run a frame.
  const pinned = new URLSearchParams(location.search).get('peer');
  let addr;
  if (pinned) {
    addr = 'dev-' + pinned;
  } else {
    addr = sessionStorage.getItem('et_dev_addr');
    if (!addr) {
      addr = 'dev-' + Math.random().toString(36).slice(2, 10);
      sessionStorage.setItem('et_dev_addr', addr);
    }
  }

  global.webxdc = {
    selfAddr: addr,
    selfName: 'Dev ' + addr.slice(4, 8),

    joinRealtimeChannel() {
      const bc = new BroadcastChannel('vectoret-realtime');
      let listener = null;

      bc.onmessage = (ev) => {
        if (listener) {
          listener(new Uint8Array(ev.data));
        }
      };

      return {
        send(data) {
          // BroadcastChannel does not echo to the sender, matching webxdc
          bc.postMessage(data.buffer.slice(
            data.byteOffset, data.byteOffset + data.byteLength));
        },
        setListener(fn) { listener = fn; },
        leave() { bc.close(); },
      };
    },

    setUpdateListener: async () => {},
    sendUpdate: () => {},
  };
})(window);
