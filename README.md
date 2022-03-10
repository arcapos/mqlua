mqlua - Run multiple Lua states in POSIX threads and use ZeroMQ for
communication between the states.

Copyright (C) 2014 - 2022 Micro Systems Marc Balmer.
You can reach the author at marc@msys.ch

GNUmakefile is for Linux systems

To build mqlua, you must also fetch the arcapos/luazmq repository and
configure the GNUmakefile so that it can pickup luazmq.h from there.
