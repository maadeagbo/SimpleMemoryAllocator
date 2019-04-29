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

  Foo();

  MemAlloc::CalcAllocPartitionAndSize( 233, MemAlloc::k_HintStrictSize | MemAlloc::k_Level0 );

  MemAlloc::InitBase();

  ASSERT_F( false, "Testing assert macros %s", "YAH" );

  return 0;
}