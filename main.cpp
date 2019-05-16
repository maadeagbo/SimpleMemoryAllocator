#include <cstdio>
#include <random>
#include <time.h>

#include "DebugLib.h"
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

void PrintAllocCalcResult( uint32_t alloc_size, uint32_t hints )
{
  printf( "o CalcAllocPartitionAndSize:\n" );
  if( hints & MemAlloc::k_HintStrictSize )
  {
    MemAlloc::ByteFormat b_data = TranslateByteFormat( (float)(hints & ~MemAlloc::k_HintStrictSize), MemAlloc::ByteFormat::k_Byte );
    printf( "  - Strict size hint : %4.2f %2s\n", b_data.m_Size, b_data.m_Type );
  }
  
  MemAlloc::QueryResult query = MemAlloc::CalcAllocPartitionAndSize( alloc_size, hints );

  printf( "  Status     : %s\n",  query.m_Status & MemAlloc::QueryResult::k_Success ? "Success" : 
                                  query.m_Status & MemAlloc::QueryResult::k_NoFreeSpace ? "No free space" :
                                  query.m_Status & MemAlloc::QueryResult::k_ExcessFragmentation ? "Fragmentation" : "unknown" );
  printf( "  Alloc bins : %u\n",  query.m_AllocBins );

  uint32_t partition = query.m_Status &= ~( MemAlloc::QueryResult::k_Success | MemAlloc::QueryResult::k_NoFreeSpace | MemAlloc::QueryResult::k_ExcessFragmentation );
  printf( "  Block      : %u B\n", partition );
}

int main( const int argc, const char* argv[] )
{
  (void*)argc;
  (void*)argv;

  RegisterExeForStackTrace( "memalloc_test" );

  printf( "\n *** Testing call stack dump *** \n\n" );

  Foo();
  
  printf( "\n *** Testing memory allocator initialization *** \n\n" );

  MemAlloc::InitBase();

  MemAlloc::PrintHeapStatus();

  printf( "\n *** Testing CalcAllocPartitionAndSize func *** \n\n" );

  srand( time( nullptr ) );
  uint32_t byte_request = rand() % 512;
  printf( "Requesting %u bytes \n", byte_request );

  PrintAllocCalcResult( (float)byte_request, MemAlloc::k_HintNone );
  PrintAllocCalcResult( (float)byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level0 );
  PrintAllocCalcResult( (float)byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level2 );
  PrintAllocCalcResult( (float)byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level4 );

  printf( "\n *** Testing Allocation func *** \n\n" );

  void* test_ptrs[5]; 
  printf( "\n*** Bang ***\n" );
  test_ptrs[0] = MemAlloc::Alloc( byte_request );
  printf( "\n*** Bang ***\n" );
  test_ptrs[1] = MemAlloc::Alloc( byte_request );
  printf( "\n*** Bang ***\n" );
  test_ptrs[2] = MemAlloc::Alloc( byte_request );
  printf( "\n*** Bang ***\n" );
  test_ptrs[3] = MemAlloc::Alloc( byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level0 );

  MemAlloc::Free( test_ptrs[1] );
  MemAlloc::Free( test_ptrs[0] );
  MemAlloc::Free( test_ptrs[2] );

  printf( "\n *** Testing ASSERT_F macro *** \n\n" );

  ASSERT_F( false, "Printing formatted message :: %s, %.3f, %p", "YAH", 32.1f, Foo );

  return 0;
}