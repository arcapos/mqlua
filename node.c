/*
 * Copyright (c) 2014 - 2020 Micro Systems Marc Balmer, CH-5073 Gipf-Oberfrick.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Micro Systems Marc Balmer nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL MICRO SYSTEMS MARC BALMER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Lua nodes */

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <zmq.h>

#include "node.h"
#include "luazmq.h"

/* 0MQ context */
extern void *zmq_context;

#ifdef HOUSEKEEPING
/* An internal "housekeeping" context */
extern void *zmq_housekeeping;
#endif
#ifdef CUSTOM_LOADER
extern int custom_loadfile(lua_State *, const char *);
#endif

struct node_state {
	lua_State *L;
	int nargs;
};

static void *node(void *);
static int luaopen_node(lua_State *L);

extern int luaopen_zmq(lua_State *);

/* Node functions */
static void
map_table(lua_State *L, lua_State *R, int t, int global)
{
	int top, pop = 0;
	char nam[64];

	switch (lua_type(L, -2)) {
	case LUA_TNUMBER:
		lua_pushnumber(R, lua_tonumber(L, -2));
		snprintf(nam, sizeof nam, "%d", (int)lua_tonumber(L, -2));
		break;
	case LUA_TSTRING:
		snprintf(nam, sizeof nam, "%s", lua_tostring(L, -2));
		lua_pushstring(R, lua_tostring(L, -2));
		break;
	default:
		luaL_error(L, "index must not be %s", luaL_typename(L, -2));
		return;
	}
	switch (lua_type(L, -1)) {
	case LUA_TBOOLEAN:
		lua_pushboolean(R, lua_toboolean(L, -1));
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(L, -1))
			lua_pushinteger(R, lua_tointeger(L, -1));
		else
			lua_pushnumber(R, lua_tonumber(L, -1));
		break;
	case LUA_TSTRING:
		lua_pushstring(R, lua_tostring(L, -1));
		break;
	case LUA_TNIL:
		lua_pushnil(R);
		break;
	case LUA_TTABLE:
		top = lua_gettop(L);
		lua_newtable(R);
		lua_pushnil(L);  /* first key */
		while (lua_next(L, top) != 0) {
			pop = 1;
			map_table(L, R, lua_gettop(R), 0);
			lua_pop(L, 1);
		}
		if (pop)
			lua_pop(L, 1);
		break;
	default:
		luaL_error(L, "value must not be %s", luaL_typename(L, -1));
		return;
	}
	if (global) {
		lua_setglobal(R, nam);
		lua_pop(R, 1);
	} else
		lua_settable(R, t);
}

void
node_openlibs(lua_State *L)
{
	luaL_openlibs(L);
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "preload");
	lua_pushcfunction(L, luaopen_node);
	lua_setfield(L, -2, "node");
	lua_pop(L, 2);
}

static int
node_create(lua_State *L)
{
	lua_State *R;
	pthread_t t;
	const char *path;
	void *housekeeping;
	int n, top, msg, pop = 0;
	struct node_state *s;

	R = luaL_newstate();
	if (R == NULL)
		return luaL_error(L, "can not create a new Lua state");

	node_openlibs(R);

	path = strdup(luaL_checkstring(L, 1));

#ifdef CUSTOM_LOADER
	if (custom_loadfile(R, path)) {
#else
	if (luaL_loadfile(R, path)) {
#endif
		lua_close(R);
		return luaL_error(L, "can not load Lua code from %s", path);
	}

	/* Map arguments, if any, to the new state */
	for (n = 2; n <= lua_gettop(L); n++) {
		switch (lua_type(L, n)) {
		case LUA_TBOOLEAN:
			lua_pushboolean(R, lua_toboolean(L, n));
			break;
		case LUA_TNUMBER:
			if (lua_isinteger(L, n))
				lua_pushinteger(R, lua_tointeger(L, n));
			else
				lua_pushnumber(R, lua_tonumber(L, n));
			break;
		case LUA_TSTRING:
			lua_pushstring(R, lua_tostring(L, n));
			break;
		case LUA_TNIL:
			lua_pushnil(R);
			break;
		case LUA_TTABLE:
			top = n;
			lua_newtable(R);
			lua_pushnil(L);  /* first key */
			while (lua_next(L, top) != 0) {
				pop = 1;
				map_table(L, R, lua_gettop(R), 0);
				lua_pop(L, 1);
			}
			if (pop)
				lua_pop(L, 1);
			break;
		default:
			return luaL_error(L, "argument must not be %s",
			    luaL_typename(L, n));
		}
	}
	s = malloc(sizeof(struct node_state));
	s->L = R;
	s->nargs = n - 2;

	if (pthread_create(&t, NULL, node, s)) {
		lua_close(R);
		free(s);
		return luaL_error(L, "can not create a new node");
	}
#ifdef HOUSEKEEPING
	housekeeping = zmq_socket(zmq_housekeeping, ZMQ_PUSH);

	/* Tell the housekeeper that we are starting */
	zmq_connect(housekeeping, "inproc://housekeeping");
	msg = NODE_STARTING;
	zmq_send(housekeeping, &msg, sizeof(msg), 0);
	zmq_close(housekeeping);
#endif
	lua_pushinteger(L, (lua_Integer)t);
	return 1;
}

static int
node_zmq_context(lua_State *L)
{
	void **ctx;

	ctx = lua_newuserdata(L, sizeof(void *));
	*ctx = zmq_context;
	luaL_getmetatable(L, ZMQ_CTX_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static int
node_id(lua_State *L)
{
	lua_pushnumber(L, (unsigned long)pthread_self());
	lua_pushinteger(L, getpid());
	return 2;
}

static int socket_types[] = {
	ZMQ_PUB,
	ZMQ_SUB,
	ZMQ_XPUB,
	ZMQ_XSUB,
	ZMQ_PUSH,
	ZMQ_PULL,
	ZMQ_PAIR,
	ZMQ_STREAM,
	ZMQ_REQ,
	ZMQ_REP,
	ZMQ_DEALER,
	ZMQ_ROUTER
};

static const char *socket_type_nm[] = {
	"pub",
	"sub",
	"xpub",
	"xsub",
	"push",
	"pull",
	"pair",
	"stream",
	"req",
	"rep",
	"dealer",
	"router",
	NULL
};

static int
node_socket(lua_State *L)
{
	void **sock;

	sock = lua_newuserdata(L, sizeof(void *));
	*sock = zmq_socket(zmq_context,
	    socket_types[luaL_checkoption(L, 1, NULL, socket_type_nm)]);
	luaL_getmetatable(L, ZMQ_SOCKET_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static int
luaopen_node(lua_State *L)
{
	struct luaL_Reg functions[] = {
		{ "create",		node_create },
		{ "zmq_context",	node_zmq_context },
		{ "id",			node_id },
		{ "socket",		node_socket },
		{ NULL,		NULL }
	};

	luaL_newlib(L, functions);
	lua_pushliteral(L, "_COPYRIGHT");
	lua_pushliteral(L, "Copyright (C) 2014 - 2020 by "
	    "micro systems marc balmer");
	lua_settable(L, -3);
	lua_pushliteral(L, "_DESCRIPTION");
	lua_pushliteral(L, "Lua nodes");
	lua_settable(L, -3);
	lua_pushliteral(L, "_VERSION");
	lua_pushliteral(L, "node 1.1.0");
	lua_settable(L, -3);
	return 1;
}

static void *
node(void *state)
{
	struct node_state *s = state;
#ifdef HOUSEKEEPING
	void *housekeeping;
	int msg;
#endif

	pthread_detach(pthread_self());

	if (lua_pcall(s->L, s->nargs, 0, 0))
		printf("pcall failed: %s\n", lua_tostring(s->L, -1));
	lua_close(s->L);
	free(s);

#ifdef HOUSEKEEPING
	housekeeping = zmq_socket(zmq_housekeeping, ZMQ_PUSH);

	/* Tell the housekeeper that we terminated */
	zmq_connect(housekeeping, "inproc://housekeeping");
	msg = NODE_TERMINATED;
	zmq_send(housekeeping, &msg, sizeof(msg), 0);
	zmq_close(housekeeping);
#endif
	return NULL;
}
