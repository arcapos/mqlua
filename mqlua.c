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
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* mqlua - run Lua programs in separate threads, connected with 0MQ */

#include <err.h>
#include <fcntl.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <zmq.h>

#include "node.h"

/* 0MQ context */
void *zmq_context;

#ifdef HOUSEKEEPING
/* An internal "housekeeping" context */
void *zmq_housekeeping;
#endif
#ifdef CUSTOM_LOADER
extern int custom_dofile(lua_State *, char *);
#endif

struct node_state {
	lua_State *L;
	int nargs;
};

static void
usage(void)
{
	(void)fprintf(stderr, "usage: mqlua <path> [args ...]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	lua_State *L;
#ifdef INTERACTIVE
	char input[160];
#endif
#ifdef HOUSEKEEPING
	void *housekeeping;
	int events, msg, nodes = 0;
	size_t len;
#endif
	int n;

	if (argc < 2)
		usage();

	if ((zmq_context = zmq_ctx_new()) == NULL)
		err(1, "zmq_ctx_new failed");

#ifdef HOUSEKEEPING
	if ((zmq_housekeeping = zmq_ctx_new()) == NULL)
		err(1, "zmq_ctx_new failed (housekeeping context)");
#endif

	if ((L = luaL_newstate()) == NULL)
		err(1, "luaL_newstate failed");

	node_openlibs(L);

	/* Set command-line arguments */
	lua_newtable(L);
	for (n = 2; n < argc; n++) {
		lua_pushinteger(L, n - 1);
		lua_pushstring(L, argv[n]);
		lua_settable(L, -3);
	}
	lua_setglobal(L, "arg");

#ifdef HOUSEKEEPING
	/* Set up the internal houskeeping queue */
	housekeeping = zmq_socket(zmq_housekeeping, ZMQ_PULL);
	zmq_bind(housekeeping, "inproc://housekeeping");
#endif

	/* Run the control script, which creates the working threads */
#ifdef CUSTOM_LOADER
	if (custom_dofile(L, argv[1]))
#else
	if (luaL_dofile(L, argv[1]))
#endif
		errx(1, "%s", lua_tostring(L, -1));

#ifdef INTERACTIVE
	/* XXX should use readline or a similar library */
	printf("mqlua Copyright (C) 2014 - 2020 Marc Balmer\n");
	while (fgets(input, sizeof input, stdin) != NULL)
		if (luaL_dostring(L, input))
			printf("%s", lua_tostring(L, -1));
#endif
#ifdef HOUSEKEEPING
	len = sizeof events;
	zmq_getsockopt(housekeeping, ZMQ_EVENTS, &events, &len);
	if (events & ZMQ_POLLIN)
		do {
			n = zmq_recv(housekeeping, &msg, sizeof(msg), 0);
			switch (msg) {
			case NODE_STARTING:
				++nodes;
				break;
			case NODE_TERMINATED:
				--nodes;
				break;
			}
		} while (nodes > 0);

	zmq_close(housekeeping);
	zmq_ctx_term(zmq_housekeeping);
#endif
	zmq_ctx_term(zmq_context);
	return 0;
}
