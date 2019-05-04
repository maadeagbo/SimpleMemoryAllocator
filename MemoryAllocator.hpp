#pragma once

#include <inttypes.h>
#include "DebugLib.h"

namespace MemAlloc
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
    uint32_t m_BHIndex;
    uint32_t m_BHAllocCount;
#ifdef TAG_MEMORY
    uint32_t m_BHTagHash;
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
    unsigned char* m_PartitionLvls[MemAlloc::k_NumLvl];
    PartitionData  m_PartitionLvlDetails[MemAlloc::k_NumLvl];
    
    BlockHeader*   m_Tracker;
    TrackerData    m_TrackerInfo[MemAlloc::k_NumLvl];

    uint32_t       m_TotalPartitionSize;
    uint32_t       m_TotalPartitionBins;
  };

  void  InitBase();

  bool  DeAlloc( void* data_ptr );

  void* Alloc( uint32_t byte_size, uint8_t pow_of_2_block_size = 8 );
  
  template<typename T>
  T* Alloc( uint32_t byte_size )
  {
    return (T*)Alloc( sizeof( T ) );
  }
  
  struct QueryResult
  {
    enum
    {
      k_Success             = 0x1,
      k_NoFreeSpace         = 0x2,
      k_ExcessFragmentation = 0x4,

      // can only use flags up to 0x20
    };
    
    BlockHeader m_Result;
    uint32_t    m_AllocBins;
    uint32_t    m_Status;
  };

  // Contains heuristics for what bucket the allocation will take place in
  QueryResult CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint = k_HintNone );

  // Dump detailed contents of memory state
  void        PrintHeapStatus();

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

  ByteFormat TranslateByteFormat( float size, uint8_t byte_type );
};

