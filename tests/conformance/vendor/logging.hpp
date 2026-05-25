/* Minimal logging shim for the vendored parallel-rdp conformance utils.
 *
 * The upstream conformance helpers include Granite's "logging.hpp", whose
 * LOGE/LOGW/LOGI macros dispatch through Util::interface_log() and otherwise
 * fall back to fprintf(stderr, ...). Granite's logging.cpp defines
 * interface_log()/set_thread_logging_interface(); pulling it in would drag the
 * whole Granite util tree (and transitively Vulkan). This shim reimplements the
 * macros as direct fprintf wrappers and provides an inline no-op
 * interface_log() so no Granite translation unit is needed.
 *
 * NOT orthodox-enforced (vendored host-only test infrastructure).
 */

#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Util
{
class LoggingInterface
{
public:
	virtual ~LoggingInterface() = default;
	virtual bool log(const char *tag, const char *fmt, va_list va) = 0;
};

// Always returns false so the LOG* macros take the fprintf fallback path.
static inline bool interface_log(const char *, const char *, ...)
{
	return false;
}

static inline void set_thread_logging_interface(LoggingInterface *)
{
}
}

#define LOGE_FALLBACK(...)                        \
	do                                            \
	{                                             \
		fprintf(stderr, "[ERROR]: " __VA_ARGS__); \
		fflush(stderr);                           \
	} while (false)

#define LOGW_FALLBACK(...)                       \
	do                                           \
	{                                            \
		fprintf(stderr, "[WARN]: " __VA_ARGS__); \
		fflush(stderr);                          \
	} while (false)

#define LOGI_FALLBACK(...)                       \
	do                                           \
	{                                            \
		fprintf(stderr, "[INFO]: " __VA_ARGS__); \
		fflush(stderr);                          \
	} while (false)

#define LOGE(...) do { if (!::Util::interface_log("[ERROR]: ", __VA_ARGS__)) { LOGE_FALLBACK(__VA_ARGS__); }} while(0)
#define LOGW(...) do { if (!::Util::interface_log("[WARN]: ", __VA_ARGS__)) { LOGW_FALLBACK(__VA_ARGS__); }} while(0)
#define LOGI(...) do { if (!::Util::interface_log("[INFO]: ", __VA_ARGS__)) { LOGI_FALLBACK(__VA_ARGS__); }} while(0)
