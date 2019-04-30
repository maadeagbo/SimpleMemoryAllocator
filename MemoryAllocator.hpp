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
    uint32_t m_Occupancy;
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

  void* Alloc( uint32_t byte_size, uint8_t pow_of_2_block_size );

  bool  DeAlloc( void* data_ptr );
  
  // Contains heuristics for what bucket the allocation will take place in
  BlockHeader CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint = k_HintNone );

  // Dump detailed contents of memory state
  void        PrintHeapStatus();
};

