/*
 * OBEmscriptenShim.h
 *
 * Force-included (via -include) ahead of every Omni-Bot translation unit in
 * the Emscripten SIDE_MODULE build.
 *
 * The upstream bot uses two compiled Boost libraries that the Emscripten
 * toolchain does not provide (only boost *headers* ship as a port):
 *
 *   - boost::filesystem   -> mapped onto C++17 <filesystem>
 *   - boost::regex        -> mapped onto C++11 <regex>
 *
 * Everything else the bot includes from Boost (shared_ptr, weak_ptr,
 * shared_array, dynamic_bitset, bind, lexical_cast, array, multi_array,
 * algorithm/string, static_assert) is header-only and is used unmodified.
 *
 * The network / IPC / debug-window translation units that would need
 * boost::asio / boost::thread are simply not compiled in this build, and the
 * precompiled header already guards those behind ENABLE_FILE_DOWNLOADER,
 * which we never define.
 */
#ifndef OB_EMSCRIPTEN_SHIM_H
#define OB_EMSCRIPTEN_SHIM_H

#ifdef __EMSCRIPTEN__

/*
 * This shim is force-included into BOTH C (physfs, lzma, zlib) and C++
 * translation units.  All of the C++-only shims below must be guarded so the
 * plain-C archivers/compressors still parse.
 */
#ifdef __cplusplus

/*
 * prof_gather.h / prof.h route their platform by preprocessor: WIN32, else
 * __linux__/__MACH__, else #error.  Emscripten defines none of them.  The
 * __linux__ branch (prof_unix.h) is portable - a `long long` typedef and an
 * empty inline timestamp - so satisfy that branch.  prof_unix.h has no x86
 * asm, so this is wasm-safe.
 */
#ifndef __linux__
#define __linux__ 1
#define OB_SHIM_DEFINED_LINUX 1
#endif

#include <filesystem>
#include <regex>
#include <string>

/*
 * common.h does `#include <boost/filesystem/...>` and then
 * `namespace fs = boost::filesystem;`.  We pre-populate the include guards of
 * the two boost.filesystem headers it pulls in so those includes become
 * no-ops, define a minimal `boost::filesystem` that aliases std::filesystem,
 * and predefine BOOST_FILESYSTEM_VERSION (boost v2 API == std::filesystem).
 */
#ifndef BOOST_FILESYSTEM_PATH_HPP
#define BOOST_FILESYSTEM_PATH_HPP
#endif
#ifndef BOOST_FILESYSTEM_OPERATIONS_HPP
#define BOOST_FILESYSTEM_OPERATIONS_HPP
#endif
#ifndef FILESYSTEM_PATH_HPP_INCLUDED
#define FILESYSTEM_PATH_HPP_INCLUDED
#endif
#ifndef FILESYSTEM_OPERATIONS_HPP_INCLUDED
#define FILESYSTEM_OPERATIONS_HPP_INCLUDED
#endif

namespace boost
{
	namespace filesystem
	{
		using namespace std::filesystem;
		using std::filesystem::path;
		using std::filesystem::exists;
		using std::filesystem::is_directory;
		using std::filesystem::create_directories;
	}
}

/*
 * common.h does `#include <boost/regex.hpp>`, uses `boost::regex`,
 * `boost::regex_match`, `boost::regex_replace` and the flag constants
 * basic / icase / grep in REGEX_OPTIONS.  Map onto std::regex.
 *
 * We pre-populate the include guards of the real boost regex headers (this
 * boost version uses BOOST_RE_REGEX_HPP for regex.hpp and
 * BOOST_RE_REGEX_HPP_INCLUDED for regex/v5/regex.hpp) so the include becomes
 * a no-op, then alias the std::regex symbols the bot actually references.
 * `boost::regex::basic/icase/grep` resolve to the identical std::regex
 * (syntax_option_type) constants.  The glob-style patterns the live code
 * feeds it (e.g. "[^A-Za-z0-9_]", "*.bot") are accepted by these grammars.
 */
#ifndef BOOST_RE_REGEX_HPP
#define BOOST_RE_REGEX_HPP
#endif
#ifndef BOOST_RE_REGEX_HPP_INCLUDED
#define BOOST_RE_REGEX_HPP_INCLUDED
#endif
#ifndef BOOST_REGEX_HPP
#define BOOST_REGEX_HPP
#endif
#ifndef BOOST_REGEX_V4_HPP
#define BOOST_REGEX_V4_HPP
#endif

namespace boost
{
	using std::regex;
	using std::regex_match;
	using std::regex_replace;
	using std::regex_search;
	// NOTE: do not alias smatch/cmatch here - the real boost headers define
	// boost::smatch/boost::cmatch and would clash with the std:: typedefs.
	// The live Omni-Bot code never references them directly.
}

/*
 * `namespace fs = boost::filesystem;` in common.h picks up the alias above,
 * which resolves to std::filesystem.  Nothing further needed.
 */

#include <strings.h>   /* strcasecmp */
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <random>
#include <vector>

/*
 * --- Platform compat the bot normally gets from a WIN32/linux platform.h ---
 * The Emscripten toolchain selects neither branch, so provide them here.
 */
#ifndef PATHDELIMITER
#define PATHDELIMITER '/'
#endif
#ifndef PATHDELIMITER2
#define PATHDELIMITER2 '/'
#endif
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef stricmp
#define stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif

/*
 * GameMonkey (gm) expects GM_DEFAULT_ALLOC_ALIGNMENT from its per-platform
 * gmConfig_p.h (platform/win32gcc etc).  Emscripten has no dedicated platform
 * dir, and gmConfig.h picks one only for known compilers, so define it here.
 */
#ifndef GM_DEFAULT_ALLOC_ALIGNMENT
#define GM_DEFAULT_ALLOC_ALIGNMENT 16
#endif

/*
 * (prof_gather.h / prof.h are satisfied by the __linux__ define above, which
 * selects the portable prof_unix.h branch - no manual stub needed.)
 */

/*
 * C++17 removals that this older codebase still relies on.  We compile the
 * bot as C++17 (for <filesystem>), so restore the pieces it uses.
 */

/* 'register' storage-class specifier was removed in C++17. */
#if __cplusplus >= 201703L
#ifndef register
#define register
#endif
#endif

/* std::random_shuffle was removed in C++17; re-add onto std::shuffle. */
#if __cplusplus >= 201703L
namespace std
{
	template <typename RandomIt>
	void random_shuffle(RandomIt first, RandomIt last)
	{
		std::random_device rd;
		std::mt19937 g(rd());
		std::shuffle(first, last, g);
	}
	template <typename RandomIt, typename RandomFunc>
	void random_shuffle(RandomIt first, RandomIt last, RandomFunc &&r)
	{
		typedef typename std::iterator_traits<RandomIt>::difference_type diff_t;
		for (diff_t i = (last - first) - 1; i > 0; --i)
		{
			std::swap(first[i], first[r(i + 1)]);
		}
	}
}
#endif

#endif /* __cplusplus */

#endif /* __EMSCRIPTEN__ */

#endif /* OB_EMSCRIPTEN_SHIM_H */
