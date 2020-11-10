#ifndef cspydr_common_h
#define cspydr_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UINT8_COUNT (UINT8_MAX + 1)

#ifdef NDEBUG
#define CSPYDR_VERSION "v.0.01 release"
#else
#define CSPYDR_VERSION "v.0.01 debug"
#define DEBUG_COLOR_OUTPUT
#define DEBUG_TRACE_EXECUTION
#define DEBUG_PRINT_CODE
#endif

#ifdef DEBUG_COLOR_OUTPUT
#define PRINT_ERROR(stream) fprintf(stream, "\033[1;31m");
#define PRINT_RESET(stream) fprintf(stream, "\033[0m");
#define PRINT_OK(stream) fprintf(stream, "\033[1;32m");
#else
#define PRINT_ERROR(stream) fprintf(stream, "");
#define PRINT_RESET(stream) fprintf(stream, "");
#define PRINT_OK(stream) fprintf(stream, "");
#endif

bool scannerIsMuted;

#endif