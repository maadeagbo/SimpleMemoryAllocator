#include "MemoryAllocator.hpp"

#include <cstdio>
#include <cstdlib>
#include <cmath>

#define BASE_ALIGN 8
#define BASE_BUCKET 32

#ifndef MEM_MAX_SIZE
#define MEM_MAX_SIZE ( 0x1 << 20 ) * 500 // 500 mb
#endif

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

static ByteFormat TranslateByteFormat( float size, uint8_t byte_type );

static void*              s_MemBlockPtr;
static MemAlloc::FreeList s_FreeList;

static MemAlloc::PartitionData GetPartition( const uint64_t total_size, uint16_t bin_size, float percentage );
static uint32_t CalcAllignedAllocSize( uint64_t input );

static const uint32_t k_BlockHeaderSize = (uint32_t)sizeof( MemAlloc::BlockHeader );

MemAlloc::QueryResult MemAlloc::CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint )
{
  QueryResult result;
  alloc_size = CalcAllignedAllocSize( alloc_size );

  uint16_t heap_bins[k_NumLvl] = { k_Level0,
                                   k_Level1,
                                   k_Level2,
                                   k_Level3,
                                   k_Level4,
                                   k_Level5 };

  // Simple heuristic : find best-fit heap bucket
  uint16_t chosen_bucket           = k_Level0;
  uint16_t chosen_bucket_idx       = 0;
  uint32_t chosen_bucket_bin_count = 1;

  for( size_t i = 0; i < k_NumLvl; i++ )
  {
    if( alloc_size > chosen_bucket )
    {
      chosen_bucket     = chosen_bucket << 1;
      chosen_bucket_idx = i;
    }
  }

  // Strict heuristic : Attempt to allocate using specified heap bucket
  if( bucket_hint & k_HintStrictSize )
  {
    uint32_t min_bucket = 0;
    for( size_t ibin = 0; ibin < k_NumLvl && !min_bucket; ibin++ )
    {
      min_bucket = bucket_hint & heap_bins[ibin] ? heap_bins[ibin] : 0;
    }

    if( min_bucket )
    {
      chosen_bucket            = min_bucket;
      chosen_bucket_bin_count  = alloc_size % min_bucket ? 1 : 0;
      chosen_bucket_bin_count += alloc_size / min_bucket;

      printf( " Using k_HintStrictSize :: Num bins alloc : %u\n", chosen_bucket_bin_count );
    }
  }

  printf( "  Bucket size : %u\n", chosen_bucket );

  // check partition for free space

  if( s_FreeList.m_TrackerInfo[chosen_bucket_idx].m_BinOccupancy < chosen_bucket_bin_count )
  {
    result.m_Result.m_BHAllocCount = chosen_bucket_bin_count;
    result.m_Result.m_BHIndex      = chosen_bucket;
    result.m_Status                = QueryResult::k_NoFreeSpace;

    return result;
  }

  return result;
}

void MemAlloc::InitBase()
{
  s_FreeList = {};
  // Partition scheme :
  //    k_Level0 :  5%
  //    k_Level1 : 10%
  //    k_Level2 : 15%
  //    k_Level3 : 20%
  //    k_Level4 : 25%
  //    k_Level5 : 25%

  // calculate partition stats per memory level
  s_FreeList.m_PartitionLvlDetails[0] = GetPartition( MEM_MAX_SIZE, k_Level0, 0.05f );
  s_FreeList.m_PartitionLvlDetails[1] = GetPartition( MEM_MAX_SIZE, k_Level1, 0.10f );
  s_FreeList.m_PartitionLvlDetails[2] = GetPartition( MEM_MAX_SIZE, k_Level2, 0.15f );
  s_FreeList.m_PartitionLvlDetails[3] = GetPartition( MEM_MAX_SIZE, k_Level3, 0.20f );
  s_FreeList.m_PartitionLvlDetails[4] = GetPartition( MEM_MAX_SIZE, k_Level4, 0.25f );
  s_FreeList.m_PartitionLvlDetails[5] = GetPartition( MEM_MAX_SIZE, k_Level5, 0.25f );

  for(uint32_t ibin = 0; ibin < MemAlloc::k_NumLvl; ibin++)
  {
    s_FreeList.m_TotalPartitionSize += s_FreeList.m_PartitionLvlDetails[ibin].m_Size;
    s_FreeList.m_TotalPartitionBins += s_FreeList.m_PartitionLvlDetails[ibin].m_BinCount;
  }
  uint32_t tracker_list_size = k_BlockHeaderSize * s_FreeList.m_TotalPartitionBins;

  // get heap memory from system for free list && partitions
  // MOSES: TODO : trigger assert if memory > max(uint32_t)
  ASSERT_F( CalcAllignedAllocSize( tracker_list_size + s_FreeList.m_TotalPartitionSize ) < (uint32_t)-1, 
            "Memory to alloc exceeds limit : %zu\n",
            (uint32_t)-1 );
  s_MemBlockPtr = calloc( CalcAllignedAllocSize( tracker_list_size + s_FreeList.m_TotalPartitionSize ), sizeof( unsigned char ) );
  ASSERT_F( s_MemBlockPtr, "Failed to initialize memory" );

  // set addresses for memory tracker list && partitions
  s_FreeList.m_Tracker    = (MemAlloc::BlockHeader*)s_MemBlockPtr;
  unsigned char* byte_ptr = (unsigned char*)s_MemBlockPtr;
  
  s_FreeList.m_PartitionLvls[0] = byte_ptr + tracker_list_size; // offset b/c tracker list is at front
  s_FreeList.m_PartitionLvls[1] = s_FreeList.m_PartitionLvls[0] + s_FreeList.m_PartitionLvlDetails[0].m_Size;
  s_FreeList.m_PartitionLvls[2] = s_FreeList.m_PartitionLvls[1] + s_FreeList.m_PartitionLvlDetails[1].m_Size;
  s_FreeList.m_PartitionLvls[3] = s_FreeList.m_PartitionLvls[2] + s_FreeList.m_PartitionLvlDetails[2].m_Size;
  s_FreeList.m_PartitionLvls[4] = s_FreeList.m_PartitionLvls[3] + s_FreeList.m_PartitionLvlDetails[3].m_Size;
  s_FreeList.m_PartitionLvls[5] = s_FreeList.m_PartitionLvls[4] + s_FreeList.m_PartitionLvlDetails[4].m_Size;
  
  ASSERT_F( ( s_FreeList.m_PartitionLvls[5] + s_FreeList.m_PartitionLvlDetails[5].m_Size ) == (byte_ptr + tracker_list_size + s_FreeList.m_TotalPartitionSize), "Invalid buffer calculations {%p : %p}", s_FreeList.m_PartitionLvls[5] + s_FreeList.m_PartitionLvlDetails[5].m_Size, byte_ptr + tracker_list_size + s_FreeList.m_TotalPartitionSize );

  // initialize tracker data for each memory partition
  uint32_t tracker_offsets = 0;
  for(uint32_t itracker_idx = 0; itracker_idx < MemAlloc::k_NumLvl; itracker_idx++)
  {
    s_FreeList.m_TrackerInfo[itracker_idx].m_HeadIdx = 0;
    s_FreeList.m_TrackerInfo[itracker_idx].m_TrackedCount = 1;

    MemAlloc::BlockHeader& mem_tag = s_FreeList.m_Tracker[tracker_offsets];
    mem_tag.m_BHIndex = 0;
    mem_tag.m_BHAllocCount = s_FreeList.m_PartitionLvlDetails[itracker_idx].m_BinCount;

    s_FreeList.m_TrackerInfo[itracker_idx].m_BinOccupancy = mem_tag.m_BHAllocCount; 

    s_FreeList.m_TrackerInfo[itracker_idx].m_PartitionOffset = tracker_offsets;
    tracker_offsets += s_FreeList.m_PartitionLvlDetails[itracker_idx].m_BinCount;
  }
}

void* MemAlloc::Alloc( uint32_t byte_size, uint8_t pow_of_2_block_size )
{
  return nullptr;
}

void MemAlloc::PrintHeapStatus()
{
  // Total allocated memory
  ByteFormat b_data = TranslateByteFormat( s_FreeList.m_TotalPartitionSize + k_BlockHeaderSize * s_FreeList.m_TotalPartitionBins, ByteFormat::k_Byte );
  printf( "o Total allocated heap memory : %10.3f %2s\n", b_data.m_Size, b_data.m_Type  );
  b_data = TranslateByteFormat( s_FreeList.m_TotalPartitionSize, ByteFormat::k_Byte );
  printf( "  - Total partition sizes     : %10.3f %2s\n", b_data.m_Size, b_data.m_Type );
  b_data = TranslateByteFormat( k_BlockHeaderSize * s_FreeList.m_TotalPartitionBins, ByteFormat::k_Byte );
  printf( "  - Tracker list size         : %10.3f %2s\n", b_data.m_Size, b_data.m_Type );
  
  // Partition characteristics
  printf( "o Partition Data:\n" );
  for(uint32_t ipartition = 0; ipartition < k_NumLvl; ipartition++)
  {
    PartitionData& part_data = s_FreeList.m_PartitionLvlDetails[ipartition];

    b_data             = TranslateByteFormat( part_data.m_BinSize, ByteFormat::k_Byte );
    ByteFormat b_data2 = TranslateByteFormat( part_data.m_BinCount * part_data.m_BinSize, ByteFormat::k_Byte );
    
    printf( "  - Partition %u :: %10.3f %2s (bin size + %zu B), %10u (bin count), %10.3f %2s (partition size)\n", ipartition, b_data.m_Size, b_data.m_Type, sizeof(BlockHeader), part_data.m_BinCount, b_data2.m_Size, b_data2.m_Type );
  }
  
  // For each partition: allocations && free memory (percentages), fragmentation(?)
  printf( "o Tracker Data :\n" );
  char percent_str[21] = {};
  for(uint32_t ipartition = 0; ipartition < k_NumLvl; ipartition++)
  {
    printf( "  - Partition %u:\n", ipartition );

    PartitionData& part_data    = s_FreeList.m_PartitionLvlDetails[ipartition];
    TrackerData&   tracked_data = s_FreeList.m_TrackerInfo[ipartition];

    float    mem_occupancy = ceilf32( (float)tracked_data.m_BinOccupancy / (float)part_data.m_BinCount );
    uint32_t bar_ticks     = (uint32_t)( (float)(sizeof( percent_str ) - 1) * ( 1.f - mem_occupancy ) );

    memset( percent_str, 0, sizeof( percent_str ) );
    memset( percent_str, 'x', bar_ticks );

    printf( "    [%*s] (%.3f%% allocated)\n", 20, percent_str, ( 1.f - mem_occupancy ) * 100.f );

    for(uint32_t itracker_idx = 0; itracker_idx < tracked_data.m_TrackedCount; itracker_idx++)
    {
      BlockHeader& tracker = s_FreeList.m_Tracker[tracked_data.m_PartitionOffset + itracker_idx];
      b_data               = TranslateByteFormat( tracker.m_BHAllocCount * part_data.m_BinSize, ByteFormat::k_Byte );
      
      printf( "    | %u (bin index), %u (coalesced blocks), %10.3f %2s\n", tracker.m_BHIndex, tracker.m_BHAllocCount, b_data.m_Size, b_data.m_Type );
    }
    
  }
}

//***********************************************************************************************
//***********************************************************************************************

static ByteFormat TranslateByteFormat( float size, uint8_t byte_type )
{
  if( byte_type == ByteFormat::k_Byte )
  {
    if( size > 1024.f && size < 1048576.f )
    {
      return { size / 1024.f, "kB" };
    }
    else if( size > 1048576.f )
    {
      return { size / 1048576.f, "mB" };
    }
    return { size, "B" };
  }
  else if( byte_type == ByteFormat::k_KiloByte )
  {
    if( size > 1024.f )
    {
      return { size / 1024.f, "mB" };
    }
    return { size, "kB" };
  }
  return { size, "mB" };
}

// Size of partition is restricted by 2 factors: freelist tracker && block header
// * Each bin in the partition must support a blockheader
// * Each bin in the partition must be possibly represented by a tracker in the free list
static MemAlloc::PartitionData GetPartition( const uint64_t total_size, uint16_t bin_size, float percentage )
{
  MemAlloc::PartitionData part_output;

  uint64_t fixed_part_size = CalcAllignedAllocSize( (uint64_t)( (double)total_size * (double)percentage ) );

  part_output.m_BinSize  = bin_size + k_BlockHeaderSize;
  part_output.m_BinCount = fixed_part_size / ( part_output.m_BinSize + k_BlockHeaderSize );
  part_output.m_Size     = part_output.m_BinCount * part_output.m_BinSize;

  return part_output;
}

static uint32_t CalcAllignedAllocSize( uint64_t input )
{
  const uint32_t remainder = input % BASE_ALIGN;
  input += remainder ? BASE_ALIGN - remainder : 0;

  return input;
}