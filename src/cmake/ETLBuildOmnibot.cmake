#-----------------------------------------------------------------
# ETLBuildOmnibot.cmake
#
# Builds the Omni-Bot Enemy Territory AI library (omnibot_et) as an
# Emscripten SIDE_MODULE so that the engine-side loader
# (vendor/Omnibot/Common/BotLoadLibrary.cpp, compiled into qagame) can
# dlopen() it at runtime.  Without this target the "bot" command exists in
# qagame but the dlopen() of omnibot_et.so fails and no bots can be created.
#
# Only wired up for Emscripten; the native build does not ship a bundled bot
# library from this tree.
#-----------------------------------------------------------------

if(NOT EMSCRIPTEN)
	return()
endif()

if(NOT (FEATURE_OMNIBOT AND BUILD_SERVER_MOD))
	return()
endif()

set(OB_ROOT "${PROJECT_SOURCE_DIR}/vendor/omni-bot/0.83/Omnibot")
set(OB_COMMON "${OB_ROOT}/Common")
set(OB_ET "${OB_ROOT}/ET")
set(OB_DEPS "${OB_ROOT}/dependencies")
set(OB_GM "${OB_DEPS}/gmscriptex/gmsrc_ex")
set(OB_SHIM "${PROJECT_SOURCE_DIR}/cmake/omnibot")

# --- gm script virtual machine + standard binds (debugger/editor skipped) ---
file(GLOB OB_GM_SRC CONFIGURE_DEPENDS
	"${OB_GM}/src/gm/*.cpp"
	"${OB_GM}/src/binds/*.cpp"
	"${OB_GM}/src/platform/win32gcc/*.cpp"
	"${OB_GM}/src/3rdParty/gmbinder2/*.cpp"
)
# gmSqliteLib needs the bundled sqlite amalgamation the bot does not ship.
list(FILTER OB_GM_SRC EXCLUDE REGEX ".*gmSqliteLib\\.cpp$")

# --- physfs (zip/7z virtual filesystem used for waypoint/goal/nav archives) ---
file(GLOB OB_PHYSFS_SRC CONFIGURE_DEPENDS
	"${OB_DEPS}/physfs/*.c"
	"${OB_DEPS}/physfs/archivers/*.c"
	"${OB_DEPS}/physfs/lzma/C/*.c"
	"${OB_DEPS}/physfs/lzma/C/Archive/7z/*.c"
	"${OB_DEPS}/physfs/lzma/C/Compress/Lzma/*.c"
	"${OB_DEPS}/physfs/lzma/C/Compress/Branch/*.c"
	"${OB_DEPS}/physfs/zlib123/*.c"
	"${OB_DEPS}/physfs/platform/posix.c"
	"${OB_DEPS}/physfs/platform/unix.c"
)

# The lzma SDK dir ships duplicate/alternative decoder implementations
# (LzmaStateDecode.c, LzmaTest.c, LzmaStateTest.c) that redefine the same
# LzmaDecode*/Lzma* symbols as LzmaDecode.c.  Keep only the canonical decoder
# and the 7z archive layer physfs actually links against.
list(FILTER OB_PHYSFS_SRC EXCLUDE REGEX "LzmaStateDecode\\.c$")
list(FILTER OB_PHYSFS_SRC EXCLUDE REGEX "LzmaStateTest\\.c$")
list(FILTER OB_PHYSFS_SRC EXCLUDE REGEX "LzmaTest\\.c$")
# LzmaDecodeSize.c is the size-tracking variant of the decoder and redefines
# LzmaDecode/LzmaDecodeProperties already provided by LzmaDecode.c.
list(FILTER OB_PHYSFS_SRC EXCLUDE REGEX "LzmaDecodeSize\\.c$")

# --- recast / detour navigation mesh ---
file(GLOB OB_RECAST_SRC CONFIGURE_DEPENDS
	"${OB_DEPS}/Recast/Recast/Source/*.cpp"
	"${OB_DEPS}/Recast/Detour/Source/*.cpp"
)

# --- WildMagic math ---
file(GLOB OB_WILDMAGIC_SRC CONFIGURE_DEPENDS
	"${OB_DEPS}/wildmagic/*.cpp"
)

# --- bot common + ET module (Emscripten unity wrappers) ---
set(OB_BOT_SRC
	"${OB_SHIM}/OBCommonEmscripten.cpp"
	"${OB_SHIM}/OBETEmscripten.cpp"
)

add_library(omnibot_et MODULE
	${OB_BOT_SRC}
	${OB_GM_SRC}
	${OB_PHYSFS_SRC}
	${OB_RECAST_SRC}
	${OB_WILDMAGIC_SRC}
)

# Build as a dlopen-able wasm side module (same mechanism as qagame).
etl_emscripten_side_module(omnibot_et)

set_target_properties(omnibot_et PROPERTIES
	PREFIX ""
	C_STANDARD 99
	CXX_STANDARD 17
	# Must be .so, not the CMake-default .wasm: the engine-side loader
	# (BotLoadLibrary.cpp) dlopen()s "<lib>.so" and Emscripten resolves a
	# .so-named side module to its wasm content regardless of extension.
	OUTPUT_NAME "omnibot_et"
	SUFFIX ".so"
	LIBRARY_OUTPUT_DIRECTORY "${MODNAME}"
	LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
	LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
	LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MODNAME}"
	RUNTIME_OUTPUT_DIRECTORY "${MODNAME}"
)

# Link exactly like the other mod side modules (qagame/cgame/ui): plain
# -sSIDE_MODULE=1 with the default export list. Do NOT restrict
# EXPORTED_FUNCTIONS or silence undefined symbols here — a side module must
# export its full symbol table and resolve the rest against the MAIN_MODULE
# (etl.wasm) at dlopen() time, otherwise loading it corrupts the shared wasm
# table and the cgame VM fails to instantiate.
target_link_options(omnibot_et PRIVATE
	"-sUSE_BOOST_HEADERS=1"
)
target_compile_options(omnibot_et PRIVATE "-sUSE_BOOST_HEADERS=1")

# Force-include the boost->std shim ahead of every bot translation unit.
target_compile_options(omnibot_et PRIVATE
	"-include" "${OB_SHIM}/OBEmscriptenShim.h"
	"-ffriend-injection"
	"-fno-strict-aliasing"
	"-Wno-deprecated"
)

# Shim headers for libstdc++-only <ext/hash_map> / <ext/functional> that the
# bot's common.h includes.  These map onto std::unordered_map / std::functional.
# Must come first so `#include <ext/...>` resolves to our shims, not the
# (nonexistent) libc++ system headers.
target_include_directories(omnibot_et BEFORE PRIVATE
	"${OB_SHIM}/gnu_ext"
)

target_include_directories(omnibot_et PRIVATE
	"${OB_COMMON}"
	"${OB_ET}"
	# boost headers are provided by -sUSE_BOOST_HEADERS=1
	"${OB_GM}/src/gm"
	"${OB_GM}/src/binds"
	"${OB_GM}/src/platform/win32gcc"
	"${OB_GM}/src/3rdParty"
	"${OB_GM}/src/3rdParty/gmbinder2"
	"${OB_GM}/src/3rdParty/mathlib"
	"${OB_DEPS}/wildmagic"
	"${OB_DEPS}/iprof"
	"${OB_DEPS}/physfs"
	"${OB_DEPS}/physfs/lzma/C"
	"${OB_DEPS}/physfs/lzma/C/Archive/7z"
	"${OB_DEPS}/physfs/lzma/C/Compress/Lzma"
	"${OB_DEPS}/physfs/lzma/C/Compress/Branch"
	"${OB_DEPS}/physfs/zlib123"
	"${OB_DEPS}/Recast/Recast/Include"
	"${OB_DEPS}/Recast/Detour/Include"
	"${OB_DEPS}/Recast/DebugUtils/Include"
)

target_compile_definitions(omnibot_et PRIVATE
	ENABLE_PATH_PLANNERS=1
	PHYSFS_SUPPORTS_ZIP=1
	PHYSFS_SUPPORTS_7Z=1
	PHYSFS_NO_CDROM_SUPPORT=1
)
