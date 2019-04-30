#include <cstdio>

#include "MemoryAllocator.hpp"

static void Yah()
{
  PrintStackTrace();
}

void Bar()
{
  Yah();
}

void Foo()
{
  Bar();
}

int main( const int argc, const char* argv[] )
{
  RegisterExeForStackTrace( "memalloc_test" );

  printf( "\n *** Testing call stack dump *** \n\n" );

  Foo();
  
  printf( "\n *** Testing memory allocator initialization *** \n\n" );

  MemAlloc::InitBase();

  MemAlloc::PrintHeapStatus();

  printf( "\n *** Testing CalcAllocPartitionAndSize func *** \n\n" );

  uint32_t byte_request = 233;
  printf( "Requesting %u bytes \n", byte_request );
  MemAlloc::CalcAllocPartitionAndSize( byte_request );
  MemAlloc::CalcAllocPartitionAndSize( byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level0 );
  MemAlloc::CalcAllocPartitionAndSize( byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level2 );
  MemAlloc::CalcAllocPartitionAndSize( byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level4 );

  printf( "\n *** Testing ASSERT_F macro *** \n\n" );

  ASSERT_F( false, "Printing formatted message :: %s, %.3f, %p", "YAH", 32.1f, Foo );

  return 0;
}