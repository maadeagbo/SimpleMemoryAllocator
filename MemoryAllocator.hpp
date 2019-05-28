#pragma once

#include <inttypes.h>
#include "DebugLib.h"
#include "MemoryAllocator.h"

#ifndef HEAP_SCOPED_STACK_DEPTH
  #define HEAP_SCOPED_STACK_DEPTH 64
#endif // !HEAP_SCOPED_STACK_DEPTH


namespace Heap
{
  enum : uint16_t
  {
    k_HintNone        = k_HeapHintNone,
    k_HintStrictSize  = k_HeapHintStrictSize,

    k_NumLvl          = k_HeapNumLvl,

    k_Level0          = k_HeapLevel0,
    k_Level1          = k_HeapLevel0,
    k_Level2          = k_HeapLevel1,
    k_Level3          = k_HeapLevel2,
    k_Level4          = k_HeapLevel3,
    k_Level5          = k_HeapLevel4,
  };

  // Over estimate size. Current calculations reduce available size due to the need to
  // create memory management data structures
  inline void InitBase( uint32_t alloc_size = 0, uint32_t thread_id = 0 )
  {
    HeapInitBase( alloc_size, thread_id );
  }

  inline bool QueryBaseValidity( uint32_t thread_id = 0 )
  {
    return HeapQueryBaseIsValid( thread_id );
  }

  // hints are an enum : k_Hint... | k_Level...
  inline void* Alloc( uint32_t byte_size, uint32_t bucket_hints = k_HintNone, uint8_t block_size = 4, uint64_t debug_hash = 0, uint32_t thread_id = 0 )
  {
    return HeapAllocate( byte_size, bucket_hints, block_size, debug_hash, thread_id );
  }

  inline bool Free( void* data_ptr, uint32_t thread_id = 0 )
  {
    return HeapRelease( data_ptr, thread_id );
  }
  
  template<typename T>
  T* AllocT( uint32_t count )
  {
    return (T*)Alloc( sizeof( T ) * count );
  }

  // Contains heuristics for what bucket the allocation will take place in
  inline HeapQueryResult CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint = k_HintNone, uint32_t thread_id = 0 )
  {
    return HeapCalcAllocPartitionAndSize( alloc_size, bucket_hint, thread_id );
  }

  // Dump detailed contents of memory state
  inline void PrintStatus( uint32_t thread_id = 0 )
  {
    HeapPrintStatus( thread_id );
  }
  

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

  class ScopedAllocator
  {
  public:
    ScopedAllocator( uint32_t thread_id = 0 ) : m_ThreadId( thread_id ) {}
    ~ScopedAllocator()
    {
      for( uint32_t iptr = 0; iptr < m_StackDepth; iptr++ )
      {
        HeapRelease( m_PtrStack[iptr], m_ThreadId );
      }
    }

    void* Alloc( uint32_t byte_size, uint8_t block_size = 4, uint64_t debug_hash = 0 )
    {
      if( m_StackDepth == HEAP_SCOPED_STACK_DEPTH )
      {
        return nullptr;
      }

      m_PtrStack[m_StackDepth] = HeapAllocate( byte_size, Heap::k_HintNone, block_size, debug_hash, m_ThreadId );
      m_StackDepth++;

      return m_PtrStack[m_StackDepth - 1];
    }

    template< typename T >
    T* AllocT( uint32_t count = 1, uint64_t debug_hash = 0  )
    {
      return (T*)Alloc( sizeof( T ) * count, 1, debug_hash );
    }
  private:
    void*    m_PtrStack[HEAP_SCOPED_STACK_DEPTH];
    uint32_t m_ThreadId   = 0;
    uint32_t m_StackDepth = 0;
  };
};

