#pragma once

#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define SIMPLE_HALT() __debugbreak()
#else
#define SIMPLE_HALT() __builtin_trap();
#endif  // linux

	void RegisterExeForStackTrace( const char* exe_name );

	typedef void (*DebugPrintCallback)( const char*, ... );
	void RegisterDebugPrint( DebugPrintCallback cb );

	void PrintStackTrace();

	void PrintHandler( const char* fmt_str, ... );

#ifdef NDEBUG

#define ASSERT_F( A, M, ... )

#else

#define ASSERT_F( A, M, ... )                                          \
  if( !( A ) )                                                         \
  {                                                                    \
    PrintHandler("o Assert (%s : %d): " #A "\n", __FILE__, __LINE__ ); \
    PrintHandler("  - " M "\n", ##__VA_ARGS__);                        \
    PrintStackTrace();                                                 \
    SIMPLE_HALT();                                                     \
  }

#endif  // NDEBUG

#ifdef __cplusplus
}
#endif