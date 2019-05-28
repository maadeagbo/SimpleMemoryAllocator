#pragma once

#include <inttypes.h>
#include "DebugLib.h"
#include "MemoryAllocator.h"

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
  void InitBase( uint32_t alloc_size = 0, uint32_t thread_id = 0 )
  {
    HeapInitBase( alloc_size, thread_id );
  }

  bool QueryBaseValidity( uint32_t thread_id = 0 )
  {
    return HeapQueryBaseIsValid( thread_id );
  }

  // hints are an enum : k_Hint... | k_Level...
  void* Alloc( uint32_t byte_size, uint32_t bucket_hints = k_HintNone, uint8_t block_size = 4, uint64_t debug_hash = 0, uint32_t thread_id = 0 )
  {
    return HeapAlloc( byte_size, bucket_hints, block_size, debug_hash, thread_id );
  }

  bool  Free( void* data_ptr, uint32_t thread_id = 0 )
  {
    return HeapFree( data_ptr, thread_id );
  }
  
  template<typename T>
  T* AllocT( uint32_t count )
  {
    return (T*)Alloc( sizeof( T ) * count );
  }

  // Contains heuristics for what bucket the allocation will take place in
  HeapQueryResult CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint = k_HintNone, uint32_t thread_id = 0 )
  {
    return HeapCalcAllocPartitionAndSize( alloc_size, bucket_hint, thread_id );
  }

  // Dump detailed contents of memory state
  void PrintStatus( uint32_t thread_id = 0 )
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
      HeapFree( m_DataPtr, m_ThreadId );
    }

    void* Alloc( uint32_t byte_size, uint8_t block_size = 4, uint64_t debug_hash = 0 )
    {
      m_DataPtr = HeapAlloc( byte_size, Heap::k_HintNone, block_size, debug_hash, m_ThreadId );
      return m_DataPtr;
    }

    template< typename T >
    T* AllocT( uint32_t count = 1, uint64_t debug_hash = 0  )
    {
      return (T*)Alloc( sizeof( T ) * count, 1, debug_hash );
    }
  private:
    void*    m_DataPtr  = nullptr;
    uint32_t m_ThreadId = 0;
  };
};

