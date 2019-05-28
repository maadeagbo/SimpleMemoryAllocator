#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <inttypes.h>
#include <stdbool.h>
#include "DebugLib.h"

enum
{
  k_HeapBlockPartitionMask = 0xf,
  k_HeapBlockIndexBitShift = 4,   // How far to shift m_BHIndexNPartition to get index (based on k_PartitionMask )
};

// Used to track allocated blocks of memory
struct HeapBlockHeader
{

  uint64_t m_BHIndexNPartition;
  uint64_t m_BHAllocCount;
#ifdef TAG_MEMORY
  uint64_t m_BHTagHash;
#endif
};

// Details a partitioned section of memory
struct HeapPartitionData
{
  uint64_t m_Size;
  uint64_t m_BinCount;
  uint32_t m_BinSize;
};

// Runtime information on partitioned memory
struct HeapTrackerData
{
  uint64_t m_HeadIdx;
  uint64_t m_TrackedCount;
  uint64_t m_PartitionOffset;
  uint64_t m_BinOccupancy;
};

enum // sizes of fixed allocation BucketFlags
{
  k_HeapHintNone        = 0,
  k_HeapHintStrictSize  = 0x1,

  k_HeapNumLvl          = 6,

  k_HeapLevel0          = 0x20,
  k_HeapLevel1          = k_HeapLevel0 << 1,
  k_HeapLevel2          = k_HeapLevel1 << 1,
  k_HeapLevel3          = k_HeapLevel2 << 1,
  k_HeapLevel4          = k_HeapLevel3 << 1,
  k_HeapLevel5          = k_HeapLevel4 << 1,
};

// Data structure contains information on current state of managed memory allocations
struct HeapFreeList
{

  unsigned char*            m_PartitionLvls[k_HeapNumLvl];
  struct HeapPartitionData  m_PartitionLvlDetails[k_HeapNumLvl];
    
  struct HeapBlockHeader*   m_Tracker;
  struct HeapBlockHeader    m_LargestAlloc[k_HeapNumLvl];
  struct HeapTrackerData    m_TrackerInfo[k_HeapNumLvl];

  uint64_t       m_TotalPartitionSize;
  uint64_t       m_TotalPartitionBins;
};

// Over estimate size. Current calculations reduce available size due to the need to
// create memory management data structures
// Passing in zero to both parameters means set to default size && thread
void HeapInitBase( uint64_t alloc_size /* = 0 */, uint32_t thread_id /* = 0 */ );

// Query the status of the heap contained in the thread ( 0 means main thread )
bool HeapQueryBaseIsValid( uint32_t thread_id /* = 0 */ );

// hints are an enum : k_HeapHint... | k_HeapLevel...
void* HeapAllocate( uint64_t byte_size, uint32_t bucket_hints /* = k_HeapHintNone */, uint8_t block_size /* = 0 */, uint64_t debug_hash /* = 0 */, uint32_t thread_id /* = 0 */ );

bool  HeapRelease( void* data_ptr, uint32_t thread_id /* = 0 */ );
  
enum
{
  k_QuerySuccess             = 0x1,
  k_QueryNoFreeSpace         = 0x2,
  k_QueryExcessFragmentation = 0x4,

  // can only use flags up to and not including 0x20
};
    
struct HeapQueryResult
{
  uint64_t     m_AllocBins;
  uint64_t     m_TrackerSelectedIdx;
  uint32_t     m_Status;
};

// Contains heuristics for what bucket the allocation will take place in
struct HeapQueryResult HeapCalcAllocPartitionAndSize( uint64_t alloc_size, uint32_t bucket_hint /* = k_HeapHintNone */, uint32_t thread_id /* = 0 */ );

// Dump detailed contents of memory state
void HeapPrintStatus( uint32_t thread_id );

//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
  
enum
{
  k_FormatByte = 0,
  k_FormatKiloByte,
  k_FormatMegaByte,
};

struct ByteFormat
{
  double      m_Size;
  const char* m_Type;
};

struct ByteFormat TranslateByteFormat( uint64_t size, uint8_t byte_type );

#ifdef __cplusplus
}
#endif