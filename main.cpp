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

uint32_t GenerateHint( uint32_t key );

void PrintAllocCalcResult( uint32_t alloc_size, uint32_t hints )
{
  printf( "o CalcAllocPartitionAndSize:\n" );
  if( hints & MemAlloc::k_HintStrictSize )
  {
    MemAlloc::ByteFormat b_data = TranslateByteFormat( hints & ~MemAlloc::k_HintStrictSize, MemAlloc::ByteFormat::k_Byte );
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

  srand( (unsigned int)time( nullptr ) );
  uint32_t byte_request = ( ( rand() % 512 ) * rand() ) % 512;
  printf( "Requesting %u bytes \n", byte_request );

  PrintAllocCalcResult( byte_request, MemAlloc::k_HintNone );
  PrintAllocCalcResult( byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level4 );
  PrintAllocCalcResult( byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level2 );
  PrintAllocCalcResult( byte_request, MemAlloc::k_HintStrictSize | MemAlloc::k_Level0 );

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
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::Free( test_ptrs[3] );
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::Free( test_ptrs[0] );
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::Free( test_ptrs[2] );
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );

  printf( "\n *** Testing Allocation func 2 *** \n\n" );

  const uint32_t alloc_count = 5000;
  void*          test_ptrs2[alloc_count];
  bool           free_idx_flags[alloc_count];
  
  for( uint32_t irequest = 0; irequest < alloc_count; ++irequest )
  {
    byte_request = ( ( rand() % 512 ) * rand() ) % 800;

    // randomize hints
    test_ptrs2[irequest] = MemAlloc::Alloc( byte_request, GenerateHint( byte_request ) );
    free_idx_flags[irequest] = true;
    
    // printf( "> request %u\n", byte_request );
  }
  
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );

  uint32_t freed_count = 0;
  uint32_t force_stop = 0;
  while( freed_count < alloc_count && force_stop < alloc_count * 2 )
  {
    uint32_t free_idx = rand() % alloc_count;

    if( free_idx_flags[free_idx] )
    {
      free_idx_flags[free_idx] = false;
      MemAlloc::Free( test_ptrs2[free_idx] );

      freed_count++;
    }
    else
    {
      byte_request = ( ( rand() % 512 ) * rand() ) % 800;

      test_ptrs2[free_idx] = MemAlloc::Alloc( byte_request, GenerateHint( byte_request ) );
      free_idx_flags[free_idx] = test_ptrs2[free_idx] ? true : false;
      
      // printf( "> request %u\n", byte_request );
    }
    force_stop++;
  }

  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  
  printf( "\n *** Testing Allocation func 3 ( alloc & free null )*** \n\n" );

  ASSERT_F( MemAlloc::Alloc( 0 ) == nullptr, "" );
  ASSERT_F( MemAlloc::Free( nullptr ) == false, "" );

  printf( "\n *** Testing Allocation func 4 *** \n\n" );
  
  for( uint32_t irequest = 0; irequest < alloc_count; ++irequest )
  {
    if( free_idx_flags[irequest] )
    {
      free_idx_flags[irequest] = false;
      MemAlloc::Free( test_ptrs2[irequest] );
    }
  }

  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );

  const uint32_t alloc_count2 = alloc_count * 10;
  void* test_ptrs3[alloc_count2];
  bool  free_idx_flags2[alloc_count2];
  for( uint32_t irequest = 0; irequest < alloc_count2; ++irequest )
  {
    byte_request = ( ( rand() % 512 ) * rand() ) % 4096;

    // randomize hints
    test_ptrs3[irequest] = MemAlloc::Alloc( byte_request, GenerateHint( byte_request ) );
    free_idx_flags2[irequest] = test_ptrs3[irequest] ? true : false;
    
    // printf( "> request %u\n", byte_request );
  }

  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  
  for( uint32_t irequest = 0; irequest < alloc_count2; ++irequest )
  {
    uint32_t free_idx = rand() % alloc_count2;

    if( free_idx_flags2[free_idx] )
    {
      free_idx_flags2[free_idx] = false;
      MemAlloc::Free( test_ptrs3[free_idx] );
    }
  }
  
  printf( "--------------------------------------------------------------------------------\n" );
  MemAlloc::PrintHeapStatus();
  printf( "--------------------------------------------------------------------------------\n" );

  //printf( "\n *** Testing ASSERT_F macro *** \n\n" );

  //ASSERT_F( false, "Printing formatted message :: %s, %.3f, %p", "YAH", 32.1f, Foo );

  return 0;
}

uint32_t GenerateHint( uint32_t key )
{
  uint32_t hint = MemAlloc::k_HintNone;
  
  if( key % 2 == 0 )
  {
    hint = MemAlloc::k_HintStrictSize;
    switch( key % 5 )
    {
      case 0:
      {
        hint |= MemAlloc::k_Level0;
        break;
      }
      case 1:
      {
        hint |= MemAlloc::k_Level1;
        break;
      }
      case 2:
      {
        hint |= MemAlloc::k_Level2;
        break;
      }
      case 3:
      {
        hint |= MemAlloc::k_Level3;
        break;
      }
      case 4:
      {
        hint |= MemAlloc::k_Level4;
        break;
      }
      case 5:
      {
        hint |= MemAlloc::k_Level5;
        break;
      }
    }
  }

  return hint;
}