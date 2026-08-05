/*
 * ET: Legacy
 * Copyright (C) 2012-2026 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * @file net_webxdc.c
 * @brief Browser networking over a WebXDC realtime channel.
 *
 * A WebXDC app has no internet access and no UDP - only a broadcast message
 * channel shared by every peer in the chat. Each peer derives a stable fake
 * IPv4 (10.x.y.z) from its webxdc address, and webxdc-net.js frames every
 * datagram with an explicit source and destination, so the broadcast channel
 * behaves like a switched LAN. One peer is elected host and runs the listen
 * server; the others connect to its fake address.
 *
 * Addressing lives in JS because only JS knows the peer identity; this file
 * is just the ring buffer and the two net_ip.c seams - Sys_SendPacket on the
 * way out, NET_Sleep on the way in.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

void CL_PacketEvent(const netadr_t *from, msg_t *msg);

#define WEBXDC_MAX_PACKET 1500 // engine fragments to <= 1400 (MAX_PACKETLEN)
#define WEBXDC_QUEUE_LEN  256  // ring slots; excess is dropped, like UDP

typedef struct
{
	int  len;
	byte ip[4];
	int  port;
	byte data[WEBXDC_MAX_PACKET];
} webxdcPacket_t;

static webxdcPacket_t rxQueue[WEBXDC_QUEUE_LEN];
static volatile int   rxHead = 0; // written by the JS channel listener
static volatile int   rxTail = 0; // read by the pump

static int rxCommitted = 0; // packets accepted from JS
static int rxDelivered = 0; // packets handed to the engine
static int pumpCalls   = 0; // NET_Sleep visits

/**
 * @brief Counters for the shell's diagnostics overlay.
 *
 * Whether packets are arriving, and whether anything is draining them, are
 * different failures with the same symptom, so both ends are counted.
 */
EMSCRIPTEN_KEEPALIVE int NET_WebxdcStat(int which)
{
	switch (which)
	{
	case 0:  return rxCommitted;
	case 1:  return rxDelivered;
	case 2:  return pumpCalls;
	case 3:  return (rxHead - rxTail + WEBXDC_QUEUE_LEN) % WEBXDC_QUEUE_LEN;
	default: return -1;
	}
}

/**
 * @brief Reserve the next free ring slot for JS to write a payload into.
 *
 * Split from the commit so JS can copy straight into the ring rather than
 * malloc a bounce buffer per packet.
 *
 * @return writable buffer of WEBXDC_MAX_PACKET bytes, or NULL if the ring is full
 */
EMSCRIPTEN_KEEPALIVE byte *NET_WebxdcRxReserve(void)
{
	int next = (rxHead + 1) % WEBXDC_QUEUE_LEN;

	if (next == rxTail)
	{
		return NULL; // full - drop, like a kernel would
	}

	return rxQueue[rxHead].data;
}

/**
 * @brief Publish the slot handed out by NET_WebxdcRxReserve.
 * @param[in] len   bytes written into the reserved buffer
 * @param[in] ip0-3 source fake IP octets
 * @param[in] port  source port, host byte order
 */
EMSCRIPTEN_KEEPALIVE void NET_WebxdcRxCommit(int len, int ip0, int ip1, int ip2, int ip3, int port)
{
	int next = (rxHead + 1) % WEBXDC_QUEUE_LEN;

	if (next == rxTail || len <= 0 || len > WEBXDC_MAX_PACKET)
	{
		return;
	}

	rxQueue[rxHead].len   = len;
	rxQueue[rxHead].ip[0] = (byte)ip0;
	rxQueue[rxHead].ip[1] = (byte)ip1;
	rxQueue[rxHead].ip[2] = (byte)ip2;
	rxQueue[rxHead].ip[3] = (byte)ip3;
	rxQueue[rxHead].port  = port;

	rxHead = next;
	rxCommitted++;
}

/**
 * @brief Run a console command from JS.
 *
 * The shell retries `connect` until the host answers - a host has to finish
 * loading a map before its server exists, which outlasts the engine's own
 * connect timeout.
 */
EMSCRIPTEN_KEEPALIVE void VectorET_Exec(const char *text)
{
	if (text && *text)
	{
		Cbuf_AddText(text);
	}
}

EM_JS(int, Webxdc_JsSend, (int ip0, int ip1, int ip2, int ip3, int port, const void *data, int len), {
	var net = Module.webxdcNet;
	if (!net || !net.send)
	{
		return 0;
	}
	net.send(ip0, ip1, ip2, ip3, port, HEAPU8.subarray(data, data + len));
	return 1;
});

/**
 * @brief Send a datagram over the realtime channel.
 * @return qtrue if the packet was consumed here
 */
qboolean NET_WebxdcSend(int length, const void *data, const netadr_t *to)
{
	// loopback and LAN broadcast keep the engine's normal path: the local
	// client on a listen server never leaves the process, and there is no
	// LAN to discover - peers are found by election, not by broadcast
	if (to->type != NA_IP)
	{
		return qfalse;
	}

	if (length > 0 && length <= WEBXDC_MAX_PACKET)
	{
		Webxdc_JsSend(to->ip[0], to->ip[1], to->ip[2], to->ip[3],
		              BigShort(to->port), data, length);
	}
	else
	{
		// the engine fragments to MAX_PACKETLEN, so this should never fire;
		// say so rather than drop in silence if it ever does
		Com_DPrintf(S_COLOR_YELLOW "NET_WebxdcSend: dropping %d byte datagram\n",
		            length);
	}

	// claimed either way - an oversize datagram is a drop, not a fallthrough
	// to a socket path that cannot work in a browser
	return qtrue;
}

/**
 * @brief Dispatch queued packets as if they had arrived on a socket.
 *        Called once per frame from NET_Sleep.
 */
void NET_WebxdcPump(void)
{
	byte  bufData[MAX_MSGLEN + 1];
	msg_t netmsg;

	pumpCalls++;

	while (rxTail != rxHead)
	{
		webxdcPacket_t *pkt = &rxQueue[rxTail];
		netadr_t       from;

		Com_Memset(&from, 0, sizeof(from));
		from.type  = NA_IP;
		from.ip[0] = pkt->ip[0];
		from.ip[1] = pkt->ip[1];
		from.ip[2] = pkt->ip[2];
		from.ip[3] = pkt->ip[3];
		from.port  = BigShort((short)pkt->port);

		MSG_Init(&netmsg, bufData, sizeof(bufData));
		Com_Memcpy(netmsg.data, pkt->data, pkt->len);
		netmsg.cursize   = pkt->len;
		netmsg.readcount = 0;

		if (com_sv_running->integer)
		{
			Com_RunAndTimeServerPacket(&from, &netmsg);
		}
		else
		{
			CL_PacketEvent(&from, &netmsg);
		}

		rxTail = (rxTail + 1) % WEBXDC_QUEUE_LEN;
		rxDelivered++;
	}
}

#endif // __EMSCRIPTEN__
