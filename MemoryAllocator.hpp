#pragma once

#include <inttypes.h>
#include "DebugLib.h"

namespace Heap
{
  enum : uint16_t // sizes of fixed allocation BucketFlags
  {
    k_HintNone        = 0,
    k_HintStrictSize  = 0x1,

    k_NumLvl          = 6,

    k_Level0          = 0x20,
    k_Level1          = k_Level0 << 1,
    k_Level2          = k_Level1 << 1,
    k_Level3          = k_Level2 << 1,
    k_Level4          = k_Level3 << 1,
    k_Level5          = k_Level4 << 1,
  };

  // Used to track allocated blocks of memory
  struct BlockHeader
  {
    enum
    {
      k_PartitionMask = 0xf,
      k_IndexBitShift = 4,   // How far to shift m_BHIndexNPartition to get index (based on k_PartitionMask )
    };

    uint32_t m_BHIndexNPartition;
    uint32_t m_BHAllocCount;
#ifdef TAG_MEMORY
    uint64_t m_BHTagHash;
#endif
  };

  // Details a partitioned section of memory
  struct PartitionData
  {
    uint32_t m_Size;
    uint32_t m_BinCount;
    uint32_t m_BinSize;
  };

  // Runtime information on partitioned memory
  struct TrackerData
  {
    uint32_t m_HeadIdx;
    uint32_t m_TrackedCount;
    uint32_t m_PartitionOffset;
    uint32_t m_BinOccupancy;
  };

  // Data structure contains information on current state of managed memory allocations
  struct FreeList
  {
    unsigned char* m_PartitionLvls[Heap::k_NumLvl];
    PartitionData  m_PartitionLvlDetails[Heap::k_NumLvl];
    
    BlockHeader*   m_Tracker;
    BlockHeader    m_LargestAlloc[Heap::k_NumLvl];
    TrackerData    m_TrackerInfo[Heap::k_NumLvl];

    uint32_t       m_TotalPartitionSize;
    uint32_t       m_TotalPartitionBins;
  };

  // Over estimate size. Current calculations reduce available size due to the need to
  // create memory management data structures
  void  InitBase( uint32_t alloc_size = 0, uint32_t thread_id = 0 );

  // hints are an enum : k_Hint... | k_Level...
  void* Alloc( uint32_t byte_size, uint32_t bucket_hints = k_HintNone, uint8_t block_size = 4, uint64_t debug_hash = 0, uint32_t thread_id = 0 );

  bool  Free( void* data_ptr, uint32_t thread_id = 0 );
  
  template<typename T>
  T* AllocT( uint32_t count )
  {
    return (T*)Alloc( sizeof( T ) * count );
  }
  
  struct QueryResult
  {
    enum
    {
      k_Success             = 0x1,
      k_NoFreeSpace         = 0x2,
      k_ExcessFragmentation = 0x4,

      // can only use flags up to and not including 0x20
    };
    
    uint32_t     m_AllocBins;
    uint32_t     m_Status;
    uint32_t     m_TrackerSelectedIdx;
  };

  // Contains heuristics for what bucket the allocation will take place in
  QueryResult CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint = k_HintNone, uint32_t thread_id = 0 );

  // Dump detailed contents of memory state
  void        PrintHeapStatus( uint32_t thread_id = 0 );
  

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------

  class ScopedAllocator
  {
  public:
    ScopedAllocator( uint32_t thread_id = 0 ) : m_ThreadId( thread_id ) {}
    ~ScopedAllocator();

    void* Alloc( uint32_t byte_size, uint8_t block_size = 4, uint64_t debug_hash = 0 );

    template< typename T >
    T* AllocT( uint32_t count = 1, uint64_t debug_hash = 0  )
    {
      return (T*)Alloc( sizeof( T ) * count, 1, debug_hash );
    }
  private:
    void*    m_DataPtr  = nullptr;
    uint32_t m_ThreadId = 0;
  };

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
  
  struct ByteFormat
  {
    enum : uint8_t
    {
      k_Byte = 0,
      k_KiloByte,
      k_MegaByte,
    };
    float       m_Size;
    const char* m_Type;
  };

  ByteFormat TranslateByteFormat( uint32_t size, uint8_t byte_type );
};

