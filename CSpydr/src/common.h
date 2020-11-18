#ifndef cspydr_common_h
#define cspydr_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UINT8_COUNT (UINT8_MAX + 1)

#ifdef NDEBUG
#define CSPYDR_VERSION "v.0.01 release"
#else
#define DEBUG_COLOR_OUTPUT
#define CSPYDR_VERSION "v.0.01 debug"
#define DEBUG_TRACE_EXECUTION
#define DEBUG_PRINT_CODE
//#define DEBUG_STRESS_GC
#define DEBUG_LOG_GC
#endif

#ifdef DEBUG_COLOR_OUTPUT
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[1;34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_GRAY	   "\x1b[1;30m"	

#define PRINT_ERROR(stream) fprintf(stream, ANSI_COLOR_RED);
#define PRINT_INFO(stream) fprintf(stream, ANSI_COLOR_YELLOW);
#define PRINT_RESET(stream) fprintf(stream, ANSI_COLOR_RESET);
#define PRINT_DEBUG(stream) fprintf(stream, ANSI_COLOR_GRAY);
#define PRINT_SPECIAL(stream) fprintf(stream, ANSI_COLOR_BLUE);
#define PRINT_OK(stream) fprintf(stream, ANSI_COLOR_GREEN);
#else
#define PRINT_ERROR(stream) fprintf(stream, "");
#define PRINT_RESET(stream) fprintf(stream, "");
#define PRINT_OK(stream) fprintf(stream, "");
#endif

bool scannerIsMuted;

#endif