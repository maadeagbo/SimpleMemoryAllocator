#include "DebugLib.h"
#include <stdio.h>

#ifdef __linux__
#include <execinfo.h>
#include <cxxabi.h>
#elif _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

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

#ifdef __linux__
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
#endif

void PrintStackTrace()
{
  printf( "o Backtrace (max depth : %d): \n", BACKTRACE_MAX_DEPTH );

#ifdef WIN32
  // capture stack addresses
  void* stack_frames[BACKTRACE_MAX_DEPTH];
  WORD stack_depth = CaptureStackBackTrace( 0, BACKTRACE_MAX_DEPTH, stack_frames, NULL );

  HANDLE curr_process = GetCurrentProcess();
  SymInitialize( curr_process, NULL, TRUE );

  SYMBOL_INFO *symbol  = (SYMBOL_INFO *)malloc( sizeof( SYMBOL_INFO )+( sizeof( struct DebugString ) - 1) * sizeof( TCHAR ));
  symbol->MaxNameLen   = sizeof( struct DebugString );
  symbol->SizeOfStruct = sizeof( SYMBOL_INFO );

  DWORD displacement;
  IMAGEHLP_LINE64 *line = (IMAGEHLP_LINE64 *)malloc( sizeof(IMAGEHLP_LINE64) );
  line->SizeOfStruct    = sizeof( IMAGEHLP_LINE64 );
  
  for( int iframe = 0; iframe < stack_depth; iframe++ )
  {
    DWORD64 address = (DWORD64)( stack_frames[iframe] );
    SymFromAddr( curr_process, address, NULL, symbol );

    if( SymGetLineFromAddr64( curr_process, address, &displacement, line ) )
    {
      //  #%.*d %s in ", 2, ( num_addresses - 2 ) - iframe - 1
      printf(" #%.*d %s in %s : %lu\n", 2, ( stack_depth - 2 ) - iframe - 1, symbol->Name, line->FileName, line->LineNumber );
    }
  }
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

  // the (num_addresses - 2) expression accounts for program && lib.c entry points 
  for( int iframe = 0; iframe < num_addresses; iframe++ )
  {
    DebugString func = ExtractMangledName( symbol_data[iframe] );
    printf(" #%.*d %s in ", 2, num_addresses - iframe - 1, func.str );
    
    char sys_command[256];
    snprintf( sys_command, sizeof( sys_command ), "addr2line %p -e %s", trace[iframe], s_ExeName );
    
    // dump results of system command to file ( need to finish off line if exe is uninitialized or command fails )
    FILE *fp = popen( sys_command, "r" );
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
  printf("\n");
}