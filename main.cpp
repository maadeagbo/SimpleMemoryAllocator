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

static void PrintAllocCalcResult( uint32_t alloc_size, uint32_t hints );

static uint32_t GenerateHint( uint32_t key );

static void DumpMemory( void* ptrs[], bool ptr_flags[], uint32_t num_allocs );

static int32_t Test1();
static int32_t Test2();
static int32_t Test3();
static int32_t Test4();
static int32_t Test5();
static int32_t Test6();
static int32_t Test7();

int main( const int argc, const char* argv[] )
{

  RegisterExeForStackTrace( "memalloc_test" );
  
  Heap::InitBase();

  if( argc == 2 )
  {
    switch ( *argv[1] )
    {
      case '1':
      {
        return Test1();
      }
      case '2':
      {
        return Test2();
      }
      case '3':
      {
        return Test3();
      }
      case '4':
      {
        return Test4();
      }
      case '5':
      {
        return Test5();
      }
      case '6':
      {
        return Test6();
      }
      case '7':
      {
        return Test7();
      }
    }
  }

  Test1();
  
  Test2();

  Test3();

  Test4();

  Test5();

  Test6();

  Test7();

  return 0;
}

static void PrintAllocCalcResult( uint32_t alloc_size, uint32_t hints )
{
  printf( "o CalcAllocPartitionAndSize:\n" );
  if( hints & Heap::k_HintStrictSize )
  {
    ByteFormat b_data = TranslateByteFormat( hints & ~Heap::k_HintStrictSize, k_FormatByte );
    printf( "  - Strict size hint : %4.2f %2s\n", b_data.m_Size, b_data.m_Type );
  }
  
  HeapQueryResult query = Heap::CalcAllocPartitionAndSize( alloc_size, hints );

  printf( "  Status     : %s\n",  query.m_Status & k_QuerySuccess ? "Success" : 
                                  query.m_Status & k_QueryNoFreeSpace ? "No free space" :
                                  query.m_Status & k_QueryExcessFragmentation ? "Fragmentation" : "unknown" );
  printf( "  Alloc bins : %" PRIu64 "\n",  query.m_AllocBins );

  uint32_t partition = query.m_Status &= ~( k_QuerySuccess | 
                                            k_QueryNoFreeSpace |
                                            k_QueryExcessFragmentation );
  printf( "  Block      : %u B\n", partition );
}

static uint32_t GenerateHint( uint32_t key )
{
  uint32_t hint = Heap::k_HintNone;
  
  if( key % 2 == 0 )
  {
    hint = Heap::k_HintStrictSize;
    switch( key % 5 )
    {
      case 0:
      {
        hint |= Heap::k_Level0;
        break;
      }
      case 1:
      {
        hint |= Heap::k_Level1;
        break;
      }
      case 2:
      {
        hint |= Heap::k_Level2;
        break;
      }
      case 3:
      {
        hint |= Heap::k_Level3;
        break;
      }
      case 4:
      {
        hint |= Heap::k_Level4;
        break;
      }
      case 5:
      {
        hint |= Heap::k_Level5;
        break;
      }
    }
  }

  return hint;
}

static void DumpMemory( void* ptrs[], bool ptr_flags[], uint32_t num_allocs )
{
  for( uint32_t iptr = 0; iptr < num_allocs; ++iptr )
  {
    if( ptr_flags[iptr] )
    {
      ptr_flags[iptr] = false;
      Heap::Free( ptrs[iptr] );
    }
  }
}

static int32_t Test1()
{
  printf( "\n *** Testing call stack dump *** \n\n" );

  Foo();

  return 0;
}

static int32_t Test2()
{
  printf( "\n *** Testing CalcAllocPartitionAndSize func *** \n\n" );

  srand( (unsigned int)time( nullptr ) );
  uint32_t byte_request = ( ( rand() % 512 ) * rand() ) % 512;
  printf( "Requesting %u bytes \n", byte_request );

  PrintAllocCalcResult( byte_request, Heap::k_HintNone );
  PrintAllocCalcResult( byte_request, Heap::k_HintStrictSize | Heap::k_Level4 );
  PrintAllocCalcResult( byte_request, Heap::k_HintStrictSize | Heap::k_Level2 );
  PrintAllocCalcResult( byte_request, Heap::k_HintStrictSize | Heap::k_Level0 );

  return 0;
}

static int32_t Test3()
{
  srand( (unsigned int)time( nullptr ) );
  uint32_t byte_request = ( ( rand() % 512 ) * rand() ) % 512;

  printf( "\n *** Testing Allocation func ( 4 simple allocations & frees ) *** \n\n" );

  void* test_ptrs[5]; 
  printf( "o Alloc 1 | hint : Heap::k_HintNone                              | %u B\n", byte_request );
  test_ptrs[0] = Heap::Alloc( byte_request );
  printf( "o Alloc 2 | hint : Heap::k_HintStrictSize | Heap::k_Level4 | %u B\n", byte_request );
  test_ptrs[1] = Heap::Alloc( byte_request, Heap::k_HintStrictSize | Heap::k_Level4 );
  printf( "o Alloc 3 | hint : Heap::k_HintStrictSize | Heap::k_Level2 | %u B\n", byte_request );
  test_ptrs[2] = Heap::Alloc( byte_request, Heap::k_HintStrictSize | Heap::k_Level2 );
  printf( "o Alloc 4 | hint : Heap::k_HintStrictSize | Heap::k_Level0 | %u B\n", byte_request );
  test_ptrs[3] = Heap::Alloc( byte_request, Heap::k_HintStrictSize | Heap::k_Level0 );

  printf( "--------------------------------------------------------------------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  Heap::Free( test_ptrs[1] );
  Heap::Free( test_ptrs[3] );
  Heap::Free( test_ptrs[0] );
  Heap::Free( test_ptrs[2] );
  printf( "--------------------------------------------------------------------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );

  return 0;
}

static int32_t Test4()
{
  srand( (unsigned int)time( nullptr ) );
  uint32_t byte_request = ( ( rand() % 512 ) * rand() ) % 512;

  const uint32_t alloc_count = 1000000;
  printf( "\n *** Testing Allocation func ( %u allocations and random-ordered frees ) *** \n\n", alloc_count );

  Heap::ScopedAllocator allocator;
  void** test_ptrs      = allocator.AllocT<void*>( alloc_count );
  bool*  free_idx_flags = allocator.AllocT<bool>( alloc_count );
  
  printf( "----------------------------State bfore allocations----------------------------\n" );
  Heap::PrintStatus();
  printf( "-------------------------------------------------------------------------------\n" );
  
  for( uint32_t irequest = 0; irequest < alloc_count; ++irequest )
  {
    byte_request = ( ( rand() % 512 ) * rand() ) % 4096; // randomize sizes

    test_ptrs[irequest]      = Heap::Alloc( byte_request, GenerateHint( byte_request ) ); // randomize hints
    free_idx_flags[irequest] = true;
  }
  
  printf( "-----------------------------State after allocations----------------------------\n" );
  Heap::PrintStatus();
  printf( "----------------------------Randomizing free & alloc----------------------------\n" );

  uint32_t freed_count = 0;
  uint32_t force_stop = 0;
  while( freed_count < alloc_count && force_stop < alloc_count * 3 )
  {
    uint32_t free_idx = rand() % alloc_count;

    if( free_idx_flags[free_idx] )
    {
      Heap::Free( test_ptrs[free_idx] );
      free_idx_flags[free_idx] = false;

      freed_count++;
    }
    else if( free_idx % 100 == 0 ) // 1 out of 100 chance of alloc
    {
      byte_request = ( ( rand() % 512 ) * rand() ) % 4096;

      test_ptrs[free_idx]      = Heap::Alloc( byte_request, GenerateHint( byte_request ) );
      free_idx_flags[free_idx] = test_ptrs[free_idx] ? true : false;
    }
    force_stop++;
  }

  printf( "------------------------State after randomized free & alloc---------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  
  DumpMemory( test_ptrs, free_idx_flags, alloc_count );

  printf( "----------------------------State after memory dump-----------------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  
  return 0;
}

static int32_t Test5()
{
  printf( "\n *** Testing Allocation func ( alloc( 0 ) & free( null ) ) w/ asserts *** \n\n" );

  ASSERT_F( Heap::Alloc( 0 ) == nullptr, "" );
  ASSERT_F( Heap::Free( nullptr ) == false, "" );

  return 0;
}

static int32_t Test6()
{
  srand( (unsigned int)time( nullptr ) );
  uint32_t byte_request = ( ( rand() % 512 ) * rand() ) % 512;

  const uint32_t alloc_count = 100000 * 100;
  printf( "\n *** Testing Allocation func ( %u allocations and random-ordered frees ) *** \n\n", alloc_count );

  Heap::ScopedAllocator allocator;
  void** test_ptrs      = allocator.AllocT<void*>( alloc_count );
  bool*  free_idx_flags = allocator.AllocT<bool>( alloc_count );

  ASSERT_F( *test_ptrs && *free_idx_flags, "Failed to allocate testing containers" );

  printf( "-----------------------State before randomized free & alloc---------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );

  for( uint32_t irequest = 0; irequest < alloc_count; ++irequest )
  {
    byte_request = ( ( rand() % 512 ) * rand() ) % 8192;

    // randomize hints
    test_ptrs[irequest]      = Heap::Alloc( byte_request, GenerateHint( byte_request ) );
    free_idx_flags[irequest] = test_ptrs[irequest] ? true : false;
  }

  printf( "------------------------------State after allocations---------------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  
  uint32_t freed_count = 0;
  for( uint32_t irequest = 0; irequest < alloc_count * 5 && freed_count < alloc_count; ++irequest )
  {
    uint32_t free_idx = rand() % alloc_count;

    if( free_idx_flags[free_idx] )
    {
      free_idx_flags[free_idx] = false;
      Heap::Free( test_ptrs[free_idx] );

      freed_count++;
    }
  }
  
  printf( "-----------------------------State after randomized free------------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );
  
  DumpMemory( test_ptrs, free_idx_flags, alloc_count );
  
  printf( "----------------------------------State after dump------------------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );

  return 0;
}

static int32_t Test7()
{
  srand( (unsigned int)time( nullptr ) );
  printf( "\n *** Testing Scoped Allocation *** \n\n" );

  {
    Heap::ScopedAllocator allocator;

    uint32_t count = ( rand() % 200 ) + 10;

    printf( "Scoped alloc array size : %u\n\n", count );
    int32_t* values = allocator.AllocT< int32_t >( count );

    printf( "Playing w/ scoped values %u\n\n", count );

    for( uint32_t scope_idx = 0; scope_idx < count; scope_idx++ )
    {
      values[scope_idx] = rand();
    }
    for( uint32_t scope_idx = 0; scope_idx < count; scope_idx++ )
    {
      printf( "- Scope val[%u] = %d\n", scope_idx, values[scope_idx] );
    }
    printf( "\n\n" );
    
    printf( "---------------------------------scope in---------------------------------------\n" );
    Heap::PrintStatus();
    printf( "---------------------------------scope in---------------------------------------\n" );

    printf( "Exiting scope\n\n" );
  }

  printf( "--------------------------------------------------------------------------------\n" );
  Heap::PrintStatus();
  printf( "--------------------------------------------------------------------------------\n" );

  return 0;
}
