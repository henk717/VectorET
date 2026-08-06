/*
 * OBETEmscripten.cpp
 *
 * Emscripten unity translation unit for the Omni-Bot Enemy Territory module.
 *
 * Upstream ET_BatchBuild.cpp already pulls in the full set of ET game
 * bindings plus BotExports.cpp (which defines the OMNIBOT_API entry point
 * ExportBotFunctionsFromDLL that the engine-side loader dlsym()s).  We simply
 * compile that batch file directly; nothing in it depends on the networking /
 * IPC / native-debug translation units that the Common library drops for
 * Emscripten.
 */

#include "PrecompET.h"
#include "ET_BatchBuild.cpp"
