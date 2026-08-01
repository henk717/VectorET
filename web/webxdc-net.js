/*
 * WebXDC realtime transport for the ET: Legacy web client.
 *
 * Copyright (C) 2026 VectorET contributors
 * Licensed GPLv3, matching the engine it links against.
 *
 * A WebXDC realtime channel is an unordered broadcast: every peer in the chat
 * receives every message. The engine wants a UDP LAN. Bridging the two needs
 * three things, all of which live here:
 *
 *   1. Identity   - each peer hashes its webxdc address into a stable fake
 *                   IPv4 (10.x.y.z), so the engine has something to put in a
 *                   netadr_t and to `connect` to.
 *   2. Framing    - every datagram carries explicit source and destination, so
 *                   receivers can discard traffic that is not theirs and the
 *                   listen server can tell its clients apart.
 *   3. Election   - exactly one peer must run the server. Lowest id wins.
 *
 * Addressing is deliberately on this side of the wasm boundary: only JS knows
 * the peer identity, and keeping it here means net_webxdc.c stays a ring
 * buffer with no notion of who anyone is.
 */

'use strict';

(function (global) {
  /* -------------------------------------------------- wire format */

  const MSG_GAME_PACKET = 1;
  const MSG_HOST_ANNOUNCE = 2;
  const MSG_DISCOVER = 3;
  const MSG_CLAIM = 4;

  // [type][destIp 4][destPort 2][srcIp 4][srcPort 2]
  const HEADER_LEN = 13;

  // Every peer uses one port. Peers are already unique by IP, so routing is
  // by IP alone; the port is carried only so the engine's netadr_t comparisons
  // (clc.serverAddress vs the packet source) line up on both ends.
  const ENGINE_PORT = 27960;

  // joinRealtimeChannel() hands back a channel object straight away, but the
  // host only registers the channel after an async round trip (in Vector, a
  // Tauri invoke plus Iroh/Nostr peer setup). Anything sent before that lands
  // is rejected outright - "Realtime channel not active". So warm up first,
  // and treat every early send as unreliable rather than decisive.
  const CHANNEL_WARMUP_MS = 1500;
  const DISCOVER_TRIES = 4;      // repeated, since any one may be dropped
  const DISCOVER_EVERY_MS = 400;
  const ELECT_CLAIM_MS = 900;    // wait for rival claims after claiming
  const ELECT_SETTLE_MS = 600;   // let a fresh host's announce reach us
  const ANNOUNCE_EVERY_MS = 2000;

  /* -------------------------------------------------- identity */

  function hashToUint24(s) {
    let h = 5381;
    for (let i = 0; i < s.length; i++) {
      h = ((h << 5) + h + s.charCodeAt(i)) & 0xffffff;
    }
    return h;
  }

  // 10/8 is private and never routable, so a stray packet cannot escape
  // anywhere meaningful even if this ran outside a sandbox.
  function idToFakeIp(id) {
    return [10, (id >> 16) & 0xff, (id >> 8) & 0xff, id & 0xff];
  }

  function ipEquals(a, b) {
    return a[0] === b[0] && a[1] === b[1] && a[2] === b[2] && a[3] === b[3];
  }

  function ipToString(ip) {
    return ip.join('.');
  }

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  /* -------------------------------------------------- channel */

  // Delta Chat can refuse a second joinRealtimeChannel() after a reload, so
  // the channel is stashed on window.top and reused.
  // https://webxdc.org/docs/spec/joinRealtimeChannel.html
  function openChannel() {
    if (!global.webxdc || !global.webxdc.joinRealtimeChannel) {
      throw new Error(
        'webxdc.joinRealtimeChannel is unavailable - this build must run inside ' +
        'a WebXDC-compatible messenger.'
      );
    }
    try {
      return global.webxdc.joinRealtimeChannel();
    } catch (e) {
      try {
        global.top.__webxdcRealtimeChannel.leave();
      } catch (e2) {
        console.error('[et-net] could not leave a stale realtime channel', e2);
      }
      const ch = global.webxdc.joinRealtimeChannel();
      try {
        global.top.__webxdcRealtimeChannel = ch;
      } catch (e3) {
        /* cross-origin top; the reload hack just will not apply */
      }
      return ch;
    }
  }

  /* -------------------------------------------------- net */

  class WebxdcNet {
    constructor(channel, myId, myIp) {
      this.channel = channel;
      this.myId = myId;
      this.myIp = myIp;
      this.isHost = false;
      this.hostIp = null;

      // packets that land before the wasm runtime exists
      this.pending = [];
      this.module = null;

      this.stats = {
        sent: 0, recv: 0, recvFromHost: 0, dropped: 0, errors: 0,
        bytesSent: 0, bytesRecv: 0,
      };
    }

    /** Route inbound game traffic into the engine once it is running. */
    attach(module) {
      if (!module.HEAPU8) {
        throw new Error(
          'Module.HEAPU8 is missing - the engine must be linked with ' +
          'HEAPU8 in EXPORTED_RUNTIME_METHODS');
      }
      this.module = module;
      module.webxdcNet = this;
      for (const p of this.pending) {
        this._push(p.ip, p.port, p.data);
      }
      this.pending.length = 0;
    }

    /** Called from net_webxdc.c via EM_JS. */
    send(ip0, ip1, ip2, ip3, port, payload) {
      const frame = new Uint8Array(HEADER_LEN + payload.length);
      frame[0] = MSG_GAME_PACKET;
      frame[1] = ip0;
      frame[2] = ip1;
      frame[3] = ip2;
      frame[4] = ip3;
      frame[5] = port & 0xff;
      frame[6] = (port >> 8) & 0xff;
      frame[7] = this.myIp[0];
      frame[8] = this.myIp[1];
      frame[9] = this.myIp[2];
      frame[10] = this.myIp[3];
      frame[11] = ENGINE_PORT & 0xff;
      frame[12] = (ENGINE_PORT >> 8) & 0xff;
      frame.set(payload, HEADER_LEN);

      this.channel.send(frame);
      this.stats.sent++;
      this.stats.bytesSent += frame.length;
    }

    /**
     * Single channel listener: election chatter and game traffic share it.
     *
     * Never throws. This runs inside the host's message callback, where an
     * exception is swallowed by the messenger and silently kills networking
     * for the rest of the session.
     */
    onMessage(data) {
      try {
        this._onMessage(data);
      } catch (e) {
        this.stats.errors++;
        if (this.stats.errors <= 3) {
          console.error('[et-net] dropping packet after error', e);
        }
      }
    }

    _onMessage(data) {
      if (!data || !data.length) {
        return;
      }

      if (data[0] !== MSG_GAME_PACKET) {
        return; // election messages are handled by the elector's own listener
      }
      if (data.length < HEADER_LEN) {
        return;
      }

      const dest = [data[1], data[2], data[3], data[4]];
      const destPort = data[5] | (data[6] << 8);
      const src = [data[7], data[8], data[9], data[10]];
      const srcPort = data[11] | (data[12] << 8);

      // broadcast delivers to everyone; take only what is addressed to us,
      // and never our own echo
      if (!ipEquals(dest, this.myIp)) {
        return;
      }
      if (ipEquals(src, this.myIp)) {
        return;
      }

      const payload = data.subarray(HEADER_LEN);
      this.stats.recv++;
      this.stats.bytesRecv += data.length;

      // counted separately: "am I connected" means the host is answering,
      // which is not the same question as "is any traffic arriving"
      if (this.hostIp && ipEquals(src, this.hostIp)) {
        this.stats.recvFromHost++;
      }

      if (this.module) {
        this._push(src, srcPort, payload);
      } else {
        this.pending.push({ ip: src, port: srcPort, data: payload.slice() });
      }

      void destPort;
    }

    _push(ip, port, payload) {
      const m = this.module;
      const slot = m._NET_WebxdcRxReserve();
      if (!slot) {
        this.stats.dropped++;
        return;
      }
      m.HEAPU8.set(payload, slot);
      m._NET_WebxdcRxCommit(payload.length, ip[0], ip[1], ip[2], ip[3], port);
    }
  }

  /* -------------------------------------------------- election */

  /**
   * Decide who runs the listen server.
   *
   * Announce-first: a peer joining a match already in progress hears the
   * running host and joins it rather than starting a rival server. Only when
   * nobody answers do peers claim, and the lowest id wins so every peer
   * reaches the same verdict without a tiebreak round.
   */
  async function elect() {
    const selfAddr = (global.webxdc && global.webxdc.selfAddr) ||
      Math.random().toString(36);
    const myId = hashToUint24(selfAddr);
    const myIp = idToFakeIp(myId);

    const channel = openChannel();
    const net = new WebxdcNet(channel, myId, myIp);

    let hostIp = null;
    let amHost = false;
    const claims = new Set([myId]);

    const announce = () => {
      const m = new Uint8Array(5);
      m[0] = MSG_HOST_ANNOUNCE;
      m.set(myIp, 1);
      channel.send(m);
    };

    const idOfIp = (ip) => (ip[1] << 16) | (ip[2] << 8) | ip[3];

    channel.setListener((data) => {
      if (!data || !data.length) {
        return;
      }
      switch (data[0]) {
        case MSG_HOST_ANNOUNCE:
          if (data.length >= 5) {
            const seen = [data[1], data[2], data[3], data[4]];
            // Two peers can both self-elect if their claims crossed while the
            // channel was still warming up. Whoever has the higher id stands
            // down on hearing the other, so a split heals instead of leaving
            // two separate games running side by side.
            if (amHost && idOfIp(seen) < myId) {
              amHost = false;
              net.isHost = false;
              net.hostIp = seen;
              console.warn('[et-net] standing down for lower host %s',
                           ipToString(seen));
              if (net.onDemoted) {
                net.onDemoted(seen);
              }
            }
            if (!amHost) {
              hostIp = seen;
            }
          }
          break;
        case MSG_DISCOVER:
          if (amHost) {
            announce();
          }
          break;
        case MSG_CLAIM:
          if (data.length >= 4) {
            claims.add(data[1] | (data[2] << 8) | (data[3] << 16));
          }
          break;
        default:
          net.onMessage(data);
      }
    });

    console.log('[et-net] id=%d ip=%s addr=%s', myId, ipToString(myIp), selfAddr);

    // Let the host finish registering the channel before anything we send can
    // count. Sends before this are silently discarded by the messenger.
    await sleep(CHANNEL_WARMUP_MS);

    // Repeat discovery: a single probe that lands during setup is lost, and
    // losing it means starting a rival server instead of joining the match.
    for (let i = 0; i < DISCOVER_TRIES && !hostIp; i++) {
      channel.send(new Uint8Array([MSG_DISCOVER]));
      await sleep(DISCOVER_EVERY_MS);
    }

    if (!hostIp) {
      // jitter so simultaneous starts do not collide on the same tick
      await sleep(Math.random() * 300);

      const claim = new Uint8Array(4);
      claim[0] = MSG_CLAIM;
      claim[1] = myId & 0xff;
      claim[2] = (myId >> 8) & 0xff;
      claim[3] = (myId >> 16) & 0xff;
      channel.send(claim);

      await sleep(ELECT_CLAIM_MS);

      if (!hostIp) {
        const winner = Math.min(...claims);
        if (winner === myId) {
          amHost = true;
          hostIp = myIp;
          announce();
          setInterval(announce, ANNOUNCE_EVERY_MS);
        } else {
          hostIp = idToFakeIp(winner);
          await sleep(ELECT_SETTLE_MS);
        }
      }
    }

    net.isHost = amHost;
    net.hostIp = hostIp;

    console.log('[et-net] elected: isHost=%s host=%s', amHost, ipToString(hostIp));
    return net;
  }

  global.VectorETNet = {
    elect,
    ENGINE_PORT,
    ipToString,
    idToFakeIp,
    hashToUint24,
  };
})(typeof window !== 'undefined' ? window : globalThis);
