#include "MemoryAllocator.hpp"

#include <cstdio>
#include <cstdlib>

#define BASE_ALIGN 8
#define BASE_BUCKET 32

#ifndef MEM_MAX_SIZE
#define MEM_MAX_SIZE ( 0x1 << 20 ) * 500 // 500 mb
#endif

static void*              s_MemBlockPtr;
static MemAlloc::FreeList s_FreeList;

static MemAlloc::PartitionData GetPartition( const uint64_t total_size, uint16_t bin_size, float percentage );
static uint32_t CalcAllignedAllocSize( uint64_t input );

static const uint32_t k_BlockHeaderSize = (uint32_t)sizeof( MemAlloc::BlockHeader );

MemAlloc::BlockHeader MemAlloc::CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint )
{
  MemAlloc::BlockHeader block_output;
  alloc_size = CalcAllignedAllocSize( alloc_size );

  uint16_t heap_bins[MemAlloc::k_NumLvl] = { k_Level0,
                                             k_Level1,
                                             k_Level2,
                                             k_Level3,
                                             k_Level4,
                                             k_Level5 };

  // Simple heuristic : find best-fit heap bucket
  uint16_t chosen_bucket = k_Level0;
  for( size_t i = 0; i < MemAlloc::k_NumLvl; i++ )
  {
    if( alloc_size > chosen_bucket )
    {
      chosen_bucket = chosen_bucket << 1;
    }
  }

  // Strict heuristic : Attempt to allocate using specified heap bucket
  if( bucket_hint & k_HintStrictSize )
  {
    uint32_t min_bucket = 0;
    for( size_t ibin = 0; ibin < MemAlloc::k_NumLvl && !min_bucket; ibin++ )
    {
      min_bucket = bucket_hint & heap_bins[ibin] ? heap_bins[ibin] : 0;
    }

    if( min_bucket )
    {
      chosen_bucket           =  min_bucket;
      uint32_t bins_to_alloc  =  alloc_size % min_bucket ? 1 : 0;
      bins_to_alloc           += alloc_size / min_bucket;

      printf( "Num bins alloc : %u\n", bins_to_alloc );
    }
  }

  printf( "  Bucket size : %u\n", chosen_bucket );

  return block_output;
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

  printf( "Alloc size        : %u (%u)\n", s_FreeList.m_TotalPartitionSize, MEM_MAX_SIZE );
  printf( "Alloc bins        : %u\n", s_FreeList.m_TotalPartitionBins );
  printf( "Tracker list size : %u (%u)\n", tracker_list_size, k_BlockHeaderSize );
  printf( "Final size        : %u\n", tracker_list_size + s_FreeList.m_TotalPartitionSize );

  // get heap memory from system for free list && partitions
  s_MemBlockPtr = malloc( CalcAllignedAllocSize( tracker_list_size + s_FreeList.m_TotalPartitionSize ) );
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
  
  printf( "Partition last { %p, %p } : %" PRIu64 "\n", s_FreeList.m_PartitionLvls[5], byte_ptr + tracker_list_size + s_FreeList.m_TotalPartitionSize, ( byte_ptr + tracker_list_size + s_FreeList.m_TotalPartitionSize ) - s_FreeList.m_PartitionLvls[5]);
  printf( "Partition last size %u\n", s_FreeList.m_PartitionLvlDetails[5].m_Size );

  ASSERT_F( ( s_FreeList.m_PartitionLvls[5] + s_FreeList.m_PartitionLvlDetails[5].m_Size ) == (byte_ptr + tracker_list_size + s_FreeList.m_TotalPartitionSize), "Invalid buffer calculations {%p : %p}", s_FreeList.m_PartitionLvls[5] + s_FreeList.m_PartitionLvlDetails[5].m_Size, ( byte_ptr + (tracker_list_size + s_FreeList.m_TotalPartitionSize) ) );
}

//***********************************************************************************************
//***********************************************************************************************

// Size of partition is restristed by 2 factors: freelist tracker && block header
// * Each bin in the partition must support a blockheader
// * Each bin in the partition must be possibly represented by a tracker in the free list
static MemAlloc::PartitionData GetPartition( const uint64_t total_size, uint16_t bin_size, float percentage )
{
  MemAlloc::PartitionData part_output;

  uint64_t fixed_part_size = CalcAllignedAllocSize( (uint64_t)( (double)total_size * (double)percentage ) );

  part_output.m_BinSize  = bin_size + k_BlockHeaderSize;
  part_output.m_BinCount = fixed_part_size / ( part_output.m_BinSize + k_BlockHeaderSize );
  part_output.m_Size     = part_output.m_BinCount * part_output.m_BinSize;

  printf( "    Partition {%u} : bin count %u, size %u (%" PRIu64 ")\n", bin_size, part_output.m_BinCount, part_output.m_Size, fixed_part_size );

  return part_output;
}

static uint32_t CalcAllignedAllocSize( uint64_t input )
{
  const uint32_t remainder = input % BASE_ALIGN;
  input += remainder ? BASE_ALIGN - remainder : 0;

  return input;
}