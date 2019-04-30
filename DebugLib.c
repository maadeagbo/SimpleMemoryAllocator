#include "DebugLib.h"
#include <execinfo.h>
#include <cxxabi.h>
#include <stdio.h>

#ifndef BACKTRACE_MAX_DEPTH
  #define BACKTRACE_MAX_DEPTH 20
#endif

struct DebugString
{
  char str[128];
};

static const char*        s_ExeName = "";
static DebugPrintCallback s_PrintCB = NULL;

void RegisterExeForStackTrace( const char* exe_name )
{
  s_ExeName = exe_name;
}

void RegisterDebugPrint( DebugPrintCallback cb )
{
  s_PrintCB = cb;
}

void PrintHandler( const char* fmt_str, ... )
{
  if( s_PrintCB )
  {
    va_list args;

    va_start( args, fmt_str );
    s_PrintCB( fmt_str, args );
    va_end( args );
  }
  else
  {
    // default to variadic printf
    va_list args;

    va_start( args, fmt_str );
    vprintf( fmt_str, args );
    va_end( args );
  }
}

// '('        <-- start of name, '+' or ')' <-- end of name
static DebugString ExtractMangledName( const char* input )
{
  DebugString output = {};
  snprintf( output.str, sizeof( DebugString::str ), "%s", input );

  const char* str_position = input;
  const char* start        = NULL;
  uint32_t    str_length   = 0;

  // pull out mangled name
  while( str_position )
  {
    if( *str_position == '(' )
    {
      start = str_position + 1;
    }
    else if( *str_position == '+' || *str_position == ')' )
    {
      break;
    }

    str_length += start ? 1 : 0;
    str_position++;
  }
  
  // translate if possible
  if( str_length > 1 )
  {
    snprintf( output.str, str_length, "%s", start );
    
    int   status   = 0;
    char* new_name = abi::__cxa_demangle( output.str, NULL, NULL, &status ); // uses malloc

    if( status == 0 ) // only success state
    {
      snprintf( output.str, sizeof( DebugString::str ), "%s", new_name );
    }
    free( new_name ); // release malloc'd memory
  }

  return output;
}

void PrintStackTrace()
{
#ifdef WIN32
  #error Unimplemented
#elif __linux__
  // backtrace_symbols makes use of malloc
  // backtrace_symbols_fd makes use of a provided file descriptor

  void* trace[BACKTRACE_MAX_DEPTH];

  int    num_addresses = backtrace( trace, BACKTRACE_MAX_DEPTH );
  char** symbol_data   = backtrace_symbols( trace, num_addresses );
  
  if( symbol_data == NULL )
  {
      printf( "\nbacktrace_symbols failed. Could not trace stack\n" );
      return;
  }

  printf( "o Backtrace (max depth : %d): \n", BACKTRACE_MAX_DEPTH );

  // the (num_addresses - 2) expression accounts for program && lib.c entry points 
  for( int iframe = 0; iframe < num_addresses - 2; iframe++ )
  {
    DebugString func = ExtractMangledName( symbol_data[iframe] );
    printf(" #%.*d %s, ", 2, ( num_addresses - 2 ) - iframe - 1, func.str );
    
    char sys_command[256];
    // if successful, it will automatically print
    snprintf( sys_command, sizeof( sys_command ), "addr2line %p -e %s", trace[iframe], s_ExeName);
    
    FILE *fp = popen( sys_command, "r");

    // need to finish off line if exe is uninitialized or command fails
    if( *s_ExeName == '\0' && fp == NULL )
    {
      printf( "\n" );
    }

    // prints location info from addr2line
    if( fgets( sys_command, sizeof( sys_command ), fp ) != NULL )
    {
      printf("%s", sys_command);
    }

    int status = pclose( fp );
    if( status == -1 )
    {
      printf( "Error closing pclose-opened pipe\n" );
    }
  }

  free( symbol_data );
#endif
}