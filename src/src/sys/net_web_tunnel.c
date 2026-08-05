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
 * @file net_web_tunnel.c
 * @brief Browser networking: tunnels the engine's UDP datagrams through a
 *        single WebSocket to a ws<->udp proxy colocated with the server.
 *
 * Browsers cannot send raw UDP. The engine's packet flow is intercepted at
 * the Sys_SendPacket / NET_Sleep boundary (see net_ip.c): every outgoing
 * datagram is sent as one binary WebSocket message, and each incoming
 * message is dispatched exactly like a received datagram. The proxy decides
 * where datagrams actually go (Phase 3: one fixed game server), so the
 * remote address of incoming packets is synthesized from the last
 * destination the engine sent to.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/websocket.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

void CL_PacketEvent(const netadr_t *from, msg_t *msg);

#define TUNNEL_MAX_PACKET 1500   // engine fragments to <= 1400 (MAX_PACKETLEN)
#define TUNNEL_QUEUE_LEN  128    // ring slots; excess packets are dropped (UDP semantics)

typedef struct
{
	int  len;
	byte data[TUNNEL_MAX_PACKET];
} tunnelPacket_t;

static tunnelPacket_t tunnelQueue[TUNNEL_QUEUE_LEN];
static volatile int   queueHead = 0; // written by ws callback
static volatile int   queueTail = 0; // read by pump

static EMSCRIPTEN_WEBSOCKET_T tunnelSocket = 0;
static qboolean               tunnelOpen   = qfalse;
static netadr_t               tunnelRemote;
static qboolean               tunnelHaveRemote = qfalse;

static cvar_t *net_wsUrl;

static EM_BOOL Tunnel_OnOpen(int eventType, const EmscriptenWebSocketOpenEvent *e, void *userData)
{
	Com_Printf("WebSocket tunnel connected\n");
	tunnelOpen = qtrue;
	return EM_TRUE;
}

static EM_BOOL Tunnel_OnClose(int eventType, const EmscriptenWebSocketCloseEvent *e, void *userData)
{
	Com_Printf("WebSocket tunnel closed (%d)\n", e->code);
	tunnelOpen   = qfalse;
	tunnelSocket = 0;
	return EM_TRUE;
}

static EM_BOOL Tunnel_OnError(int eventType, const EmscriptenWebSocketErrorEvent *e, void *userData)
{
	Com_Printf(S_COLOR_YELLOW "WebSocket tunnel error\n");
	return EM_TRUE;
}

static EM_BOOL Tunnel_OnMessage(int eventType, const EmscriptenWebSocketMessageEvent *e, void *userData)
{
	int next;

	if (e->isText || e->numBytes <= 0 || e->numBytes > TUNNEL_MAX_PACKET)
	{
		return EM_TRUE;
	}

	next = (queueHead + 1) % TUNNEL_QUEUE_LEN;
	if (next == queueTail)
	{
		return EM_TRUE; // queue full - drop, like a kernel would
	}

	tunnelQueue[queueHead].len = (int)e->numBytes;
	Com_Memcpy(tunnelQueue[queueHead].data, e->data, e->numBytes);
	queueHead = next;

	return EM_TRUE;
}

static void Tunnel_Connect(void)
{
	EmscriptenWebSocketCreateAttributes attr;
	char                                defaultUrl[256];

	if (tunnelSocket || !emscripten_websocket_is_supported())
	{
		return;
	}

	if (!net_wsUrl)
	{
		// default: on HTTPS pages use the page origin at /net (the reverse
		// proxy terminates TLS and forwards to the ws<->udp proxy, avoiding
		// mixed-content blocks and nonstandard ports); on plain http (local
		// dev) talk to the proxy directly on :27970
		EM_ASM({
			var url;
			if (location.protocol === 'https:') {
				url = 'wss://' + location.host + '/net';
			} else {
				url = 'ws://' + (location.hostname || 'localhost') + ':27970/';
			}
			stringToUTF8(url, $0, $1);
		}, defaultUrl, (int)sizeof(defaultUrl));
		net_wsUrl = Cvar_Get("net_wsUrl", defaultUrl, CVAR_ARCHIVE);
	}

	emscripten_websocket_init_create_attributes(&attr);
	attr.url                = net_wsUrl->string;
	attr.createOnMainThread = EM_TRUE;

	tunnelSocket = emscripten_websocket_new(&attr);
	if (tunnelSocket <= 0)
	{
		Com_Printf(S_COLOR_YELLOW "WebSocket tunnel: failed to create socket to %s\n", net_wsUrl->string);
		tunnelSocket = 0;
		return;
	}

	Com_Printf("WebSocket tunnel connecting to %s...\n", net_wsUrl->string);
	emscripten_websocket_set_onopen_callback(tunnelSocket, NULL, Tunnel_OnOpen);
	emscripten_websocket_set_onclose_callback(tunnelSocket, NULL, Tunnel_OnClose);
	emscripten_websocket_set_onerror_callback(tunnelSocket, NULL, Tunnel_OnError);
	emscripten_websocket_set_onmessage_callback(tunnelSocket, NULL, Tunnel_OnMessage);
}

/**
 * @brief Send a datagram through the tunnel.
 * @return qtrue if the packet was consumed by the tunnel
 */
qboolean NET_WebTunnelSend(int length, const void *data, const netadr_t *to)
{
	if (to->type != NA_IP && to->type != NA_IP6)
	{
		return qfalse;
	}

	Tunnel_Connect();

	// incoming packets will be attributed to the last destination
	tunnelRemote     = *to;
	tunnelHaveRemote = qtrue;

	if (tunnelOpen)
	{
		emscripten_websocket_send_binary(tunnelSocket, (void *)data, length);
	}
	// drop silently while connecting: the engine's connectionless
	// handshake retries cover this

	return qtrue;
}

/**
 * @brief Dispatch queued tunnel packets like received datagrams.
 *        Called once per frame from NET_Sleep.
 */
void NET_WebTunnelPump(void)
{
	byte  bufData[MAX_MSGLEN + 1];
	msg_t netmsg;

	while (queueTail != queueHead)
	{
		tunnelPacket_t *pkt = &tunnelQueue[queueTail];

		if (tunnelHaveRemote)
		{
			netadr_t from = tunnelRemote;

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
		}

		queueTail = (queueTail + 1) % TUNNEL_QUEUE_LEN;
	}
}

#endif // __EMSCRIPTEN__
