#include "MemoryAllocator.hpp"

#include <cstdio>
#include <cstdlib>
#include <cmath>

#define BASE_ALIGN 8
#define BASE_BUCKET 32

#ifndef MEM_MAX_SIZE
#define MEM_MAX_SIZE ( 0x1 << 20 ) * 500 // 500 mb
#endif

#define SET_INDEX_PART( INDEX, PARTITION ) ( ( INDEX ) << MemAlloc::BlockHeader::k_IndexBitShift ) | PARTITION

#define EXTRACT_IDX( BLOCK_IDX_PARTION ) ( BLOCK_IDX_PARTION >> MemAlloc::BlockHeader::k_IndexBitShift )

#define EXTRACT_PART( BLOCK_IDX_PARTION ) ( BLOCK_IDX_PARTION & MemAlloc::BlockHeader::k_PartitionMask )

//static void*              s_MemBlockPtr;
//static MemAlloc::FreeList s_FreeList;

struct MemoryData
{
  void*              m_MemBlock;
  MemAlloc::FreeList m_FreeList;
};

#define MAX_MEM_THREADS 8
static MemoryData s_MemoryDataThreads[MAX_MEM_THREADS];

static MemAlloc::PartitionData GetPartition( const uint32_t total_size, uint16_t bin_size, float percentage );
static uint32_t CalcAllignedAllocSize( uint32_t input, uint32_t alignment = BASE_ALIGN );

static const uint32_t k_BlockHeaderSize = (uint32_t)sizeof( MemAlloc::BlockHeader );

static uint16_t s_HeapBinSizes[MemAlloc::k_NumLvl] = { MemAlloc::k_Level0,
                                                       MemAlloc::k_Level1,
                                                       MemAlloc::k_Level2,
                                                       MemAlloc::k_Level3,
                                                       MemAlloc::k_Level4,
                                                       MemAlloc::k_Level5 };

// Memory layout                 Gap ( due to aligned memory ) ___
//                                |                              |
//                                V                              V
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//  |        |                  |\\\|        |                  |\\\|
//  | Header | Allocated memory |\\\| Header | Allocated memory |\\\| -------->
//  |        |                  |\\\|        |                  |\\\|
//----------------------------------------------------------------------
//----------------------------------------------------------------------

MemAlloc::QueryResult MemAlloc::CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint, uint32_t thread_id )
{
  QueryResult result;
  alloc_size = CalcAllignedAllocSize( alloc_size );

  // Simple heuristic : find best-fit heap partition
  uint32_t chosen_bucket           = k_Level0;
  uint32_t chosen_bucket_idx       = 0;
  uint32_t chosen_bucket_bin_count = 0;

  for( size_t i = 0, max_bins = sizeof( s_HeapBinSizes ); i < max_bins; i++ )
  {
    if( alloc_size <= chosen_bucket )
    {
      break;
    }
    chosen_bucket = chosen_bucket << 1;
    chosen_bucket_idx++;
  }

  // Strict heuristic : Attempt to allocate using specified heap buckets (choose largest of specified buckets)
  if( bucket_hint & k_HintStrictSize )
  {
    uint32_t max_bucket = 0;
    for( uint32_t ibin = 0; ibin < k_NumLvl; ibin++ )
    {
      if( bucket_hint & s_HeapBinSizes[ibin] )
      {
        max_bucket        = s_HeapBinSizes[ibin];
        chosen_bucket_idx = ibin;
      }
    }
    chosen_bucket = max_bucket ? max_bucket : chosen_bucket;
  }

  // update results based on chosen_bucket
  uint32_t heap_bin = s_HeapBinSizes[chosen_bucket_idx] + k_BlockHeaderSize;
  chosen_bucket_bin_count  = alloc_size % heap_bin ? 1 : 0;
  chosen_bucket_bin_count += alloc_size / heap_bin;
  if( (int)( chosen_bucket_bin_count * heap_bin ) - k_BlockHeaderSize < 0 )
  {
    chosen_bucket_bin_count += 1;  // make sure there's room for header
  }

  result.m_AllocBins = chosen_bucket_bin_count;
  result.m_Status    = chosen_bucket;

  MemAlloc::FreeList& free_list = s_MemoryDataThreads[thread_id].m_FreeList;

  if( free_list.m_TrackerInfo[chosen_bucket_idx].m_BinOccupancy < chosen_bucket_bin_count )
  {
    result.m_Status |= QueryResult::k_NoFreeSpace;
    return result;
  }

  int32_t      free_bin_idx      = -1;
  TrackerData& tracked_bins_info = free_list.m_TrackerInfo[chosen_bucket_idx];
  BlockHeader* tracked_bin       = free_list.m_Tracker + tracked_bins_info.m_PartitionOffset;

  // find next available free space to allocate from
  for( uint32_t ibin = 0; ibin < tracked_bins_info.m_TrackedCount && free_bin_idx < 0; ibin++, tracked_bin++ )
  {
    free_bin_idx = tracked_bin->m_BHAllocCount >= chosen_bucket_bin_count ? ibin : -1;
  }
  
  // mark if partition exhibits too much fragmentation
  if( free_bin_idx == -1 )
  {
    result.m_Status |= QueryResult::k_NoFreeSpace | QueryResult::k_ExcessFragmentation;
    return result;
  }

  result.m_Status             |= QueryResult::k_Success;
  result.m_TrackerSelectedIdx  = free_bin_idx;

  return result;
}

void MemAlloc::InitBase( uint32_t alloc_size, uint32_t thread_id )
{
  MemAlloc::FreeList& free_list = s_MemoryDataThreads[thread_id].m_FreeList;
  void*               mem_block = s_MemoryDataThreads[thread_id].m_MemBlock;

  free_list = {};
  /*
  o Partition scheme :
  ===============================================================================
  |  k_Level0  |  k_Level1  |  k_Level2  |  k_Level3  |  k_Level4  |  k_Level5  |
  |     5%     |    10%     |    15%     |    20%     |    25%     |    25%     |
  ===============================================================================
  */

  alloc_size = alloc_size == 0 ? MEM_MAX_SIZE : alloc_size;
  
  // calculate partition stats per memory level
  free_list.m_PartitionLvlDetails[0] = GetPartition( MEM_MAX_SIZE, k_Level0, 0.05f );
  free_list.m_PartitionLvlDetails[1] = GetPartition( MEM_MAX_SIZE, k_Level1, 0.10f );
  free_list.m_PartitionLvlDetails[2] = GetPartition( MEM_MAX_SIZE, k_Level2, 0.15f );
  free_list.m_PartitionLvlDetails[3] = GetPartition( MEM_MAX_SIZE, k_Level3, 0.20f );
  free_list.m_PartitionLvlDetails[4] = GetPartition( MEM_MAX_SIZE, k_Level4, 0.25f );
  free_list.m_PartitionLvlDetails[5] = GetPartition( MEM_MAX_SIZE, k_Level5, 0.25f );

  for(uint32_t ibin = 0; ibin < MemAlloc::k_NumLvl; ibin++)
  {
    free_list.m_TotalPartitionSize += free_list.m_PartitionLvlDetails[ibin].m_Size;
    free_list.m_TotalPartitionBins += free_list.m_PartitionLvlDetails[ibin].m_BinCount;
  }
  uint32_t tracker_list_size = k_BlockHeaderSize * free_list.m_TotalPartitionBins;

  // get heap memory from system for free list && partitions
  ASSERT_F( CalcAllignedAllocSize( tracker_list_size + free_list.m_TotalPartitionSize ) < (uint32_t)-1, 
            "Memory to alloc exceeds limit : %zu\n",
            (uint32_t)-1 );
  
  mem_block = calloc( CalcAllignedAllocSize( tracker_list_size + free_list.m_TotalPartitionSize ), sizeof( unsigned char ) );
  
  ASSERT_F( mem_block, "Failed to initialize memory" );

  // set addresses for memory tracker list & partitions

  free_list.m_Tracker    = (MemAlloc::BlockHeader*)mem_block;
  unsigned char* byte_ptr = (unsigned char*)mem_block;
  
  free_list.m_PartitionLvls[0] = byte_ptr + tracker_list_size; // offset b/c tracker list is at front
  free_list.m_PartitionLvls[1] = free_list.m_PartitionLvls[0] + free_list.m_PartitionLvlDetails[0].m_Size;
  free_list.m_PartitionLvls[2] = free_list.m_PartitionLvls[1] + free_list.m_PartitionLvlDetails[1].m_Size;
  free_list.m_PartitionLvls[3] = free_list.m_PartitionLvls[2] + free_list.m_PartitionLvlDetails[2].m_Size;
  free_list.m_PartitionLvls[4] = free_list.m_PartitionLvls[3] + free_list.m_PartitionLvlDetails[3].m_Size;
  free_list.m_PartitionLvls[5] = free_list.m_PartitionLvls[4] + free_list.m_PartitionLvlDetails[4].m_Size;
  
  ASSERT_F( ( free_list.m_PartitionLvls[5] + free_list.m_PartitionLvlDetails[5].m_Size ) == (byte_ptr + tracker_list_size + free_list.m_TotalPartitionSize), "Invalid buffer calculations {%p : %p}", free_list.m_PartitionLvls[5] + free_list.m_PartitionLvlDetails[5].m_Size, byte_ptr + tracker_list_size + free_list.m_TotalPartitionSize );

  // initialize tracker data for each memory partition

  for( uint32_t ipart_idx = 0, tracker_offsets = 0; ipart_idx < MemAlloc::k_NumLvl; ipart_idx++)
  {
    free_list.m_TrackerInfo[ipart_idx].m_HeadIdx      = 0;
    free_list.m_TrackerInfo[ipart_idx].m_TrackedCount = 1;

    MemAlloc::BlockHeader& mem_tag = free_list.m_Tracker[tracker_offsets];
    mem_tag.m_BHAllocCount         = free_list.m_PartitionLvlDetails[ipart_idx].m_BinCount;
    mem_tag.m_BHIndexNPartition    = SET_INDEX_PART( 0, ipart_idx ); // partition index is encoded in lower 4 bits 

    free_list.m_TrackerInfo[ipart_idx].m_BinOccupancy    = mem_tag.m_BHAllocCount; 
    free_list.m_TrackerInfo[ipart_idx].m_PartitionOffset = tracker_offsets;

    tracker_offsets += free_list.m_PartitionLvlDetails[ipart_idx].m_BinCount;
  }
}

void* MemAlloc::Alloc( uint32_t byte_size, uint32_t bucket_hints, uint8_t block_size, uint32_t thread_id )
{
  if ( byte_size == 0 )
  {
    return nullptr; // maybe trigger assert(?)
  }
  
  uint32_t aligned_alloc = CalcAllignedAllocSize( byte_size, block_size );

  QueryResult request = CalcAllocPartitionAndSize( aligned_alloc, bucket_hints );

  if( !( request.m_Status & QueryResult::k_Success ) ) // maybe assert(?)
  {
    if( bucket_hints & k_HintStrictSize )
    {
      //ASSERT_F( false, "Failed to allocate memory based on request" );
      return nullptr;
    }
    //ASSERT_F( !( request.m_Status & QueryResult::k_NoFreeSpace ),
    //          "Failure when attempting to allocate memory ( %u ). No free space",
    //          byte_size );
    //ASSERT_F( !( request.m_Status & QueryResult::k_ExcessFragmentation ),
    //          "Failure when attempting to allocate memory ( %u ) due to fragmentation",
    //          byte_size );
    return nullptr;
  }

  // mark && assign memory
  const uint32_t bin_size = ( request.m_Status & ~( QueryResult::k_Success ) ) + k_BlockHeaderSize;

  int32_t partition_idx = -1;
  for(uint32_t ipartition = 0; ipartition < k_NumLvl && partition_idx < 0; ipartition++)
  {
    partition_idx = s_HeapBinSizes[ipartition] == ( bin_size - k_BlockHeaderSize ) ? ipartition : -1;
  }
  ASSERT_F( partition_idx >= 0, "Invalid bin size returned from CalcAllocPartitionAndSize()" );
  
  MemAlloc::FreeList& free_list = s_MemoryDataThreads[thread_id].m_FreeList;

  TrackerData& free_part_info = free_list.m_TrackerInfo[partition_idx];
  BlockHeader* free_slot      = free_list.m_Tracker + ( free_part_info.m_PartitionOffset + request.m_TrackerSelectedIdx );
  
  BlockHeader* mem_marker         = (BlockHeader*)free_list.m_PartitionLvls[partition_idx] + ( bin_size * EXTRACT_IDX( free_slot->m_BHIndexNPartition ) );
  mem_marker->m_BHIndexNPartition = free_slot->m_BHIndexNPartition;
  mem_marker->m_BHAllocCount      = request.m_AllocBins;

  // subtract & update || remove free slot from list
  if( free_slot->m_BHAllocCount > request.m_AllocBins )
  {
    free_slot->m_BHAllocCount -= request.m_AllocBins;
    uint32_t index             = EXTRACT_IDX( free_slot->m_BHIndexNPartition );
             index            += request.m_AllocBins;

    free_slot->m_BHIndexNPartition = SET_INDEX_PART( index, partition_idx );
  }
  else
  {
    // find index of free_slot in the list
    if( free_part_info.m_TrackedCount == 1 || ( request.m_TrackerSelectedIdx + 1 ) == free_part_info.m_TrackedCount )
    {
      memset( free_slot, 0, k_BlockHeaderSize );
    }
    else
    {
      memmove( free_slot, free_slot + 1, k_BlockHeaderSize * ( free_part_info.m_TrackedCount - ( request.m_TrackerSelectedIdx + 1 ) ) );
    }
    free_part_info.m_TrackedCount--;
  }
  free_part_info.m_BinOccupancy -= request.m_AllocBins;
  
  return (unsigned char*)mem_marker + k_BlockHeaderSize; // return pointer to memory region after header
}

bool MemAlloc::Free( void* data_ptr, uint32_t thread_id )
{
  // This function will maintain the invariant : each free list partition is sorted incrementally by block index

  if( data_ptr == nullptr )
  {
    return false;
  }

  BlockHeader* header      = (BlockHeader*)( (unsigned char*)data_ptr - k_BlockHeaderSize );
  const uint32_t slot_idx  = header->m_BHIndexNPartition >> BlockHeader::k_IndexBitShift;
  const uint32_t part_idx  = header->m_BHIndexNPartition & BlockHeader::k_PartitionMask;
  const uint32_t slot_bins = header->m_BHAllocCount;
  
  MemAlloc::FreeList& free_list = s_MemoryDataThreads[thread_id].m_FreeList;

  TrackerData& tracker_info = free_list.m_TrackerInfo[part_idx];
  BlockHeader* tracker_data = free_list.m_Tracker + tracker_info.m_PartitionOffset;
  
  if( tracker_info.m_TrackedCount == 0 ) // if free list is empty, add new slot
  {
    *tracker_data = *header;
    tracker_info.m_TrackedCount++;
    return true;
  }

  auto CoalesceSlot = [&tracker_info, &tracker_data]( uint32_t tracker_idx, uint32_t base_idx, uint32_t coalesce_idx, uint32_t coalesce_bins )
  {
    tracker_data[tracker_idx].m_BHIndexNPartition  = SET_INDEX_PART( base_idx < coalesce_idx ? base_idx : coalesce_idx, EXTRACT_PART( tracker_data[tracker_idx].m_BHIndexNPartition ) );
    tracker_data[tracker_idx].m_BHAllocCount      += coalesce_bins;
    tracker_info.m_BinOccupancy                   += coalesce_bins;
  };

  auto InsertSlot = [&tracker_info, &tracker_data, &header]( uint32_t tracker_idx, bool shift_right )
  {
    switch( (int)shift_right )
    {
      case 0: // append
      {
        tracker_data[tracker_idx] = *header;
        break;
      }
      default: // shift then set
      {
        memmove( tracker_data + tracker_idx + 1, tracker_data + tracker_idx, k_BlockHeaderSize * ( tracker_info.m_TrackedCount - tracker_idx ) );
        tracker_data[tracker_idx] = *header;
        break;
      }
    }

    tracker_info.m_BinOccupancy += header->m_BHAllocCount;
    tracker_info.m_TrackedCount++;
  };

  if( tracker_info.m_TrackedCount == 1 ) // if free list has 1 slot, coalesce
  {
    int32_t base_idx  = EXTRACT_IDX( tracker_data[0].m_BHIndexNPartition );
    int32_t base_bins = tracker_data[0].m_BHAllocCount;
    int32_t head_dist = base_idx - (int)( slot_idx + slot_bins );
    int32_t tail_dist = (int)slot_idx - ( base_idx + base_bins );

    if( head_dist == 0 || tail_dist == 0 )
    {
      CoalesceSlot( 0, base_idx, slot_idx, slot_bins );
    }
    else
    {
      if( head_dist > 0 )
      {
        InsertSlot( 0, true ); // new head
      }
      else
      {
        InsertSlot( 1, false); // append
      }
    }

    return true;
  }
  
  // - use divide & conquer to find its spot in list
  int32_t head = 0;
  int32_t tail = tracker_info.m_TrackedCount - 1;
  while( ( tail - head ) > 0 )
  {
    uint32_t pivot_idx = head + ( ( tail - head ) / 2 );

    int32_t left_idx        = EXTRACT_IDX( tracker_data[pivot_idx].m_BHIndexNPartition  );
    int32_t right_idx       = EXTRACT_IDX( tracker_data[pivot_idx + 1].m_BHIndexNPartition );
    int32_t left_idx_offset = tracker_data[pivot_idx].m_BHAllocCount;

    int32_t left_dist  = (int)slot_idx - ( left_idx + left_idx_offset );
    int32_t right_dist = right_idx - (int)( slot_idx + slot_bins );
    
    if( left_dist >= 0 )
    {
      if( right_dist >= 0 )
      {
        if( left_dist == 0 && right_dist == 0 ) // coalesce both sides
        {
          CoalesceSlot( pivot_idx, left_idx, slot_idx, slot_bins );
          tracker_data[pivot_idx].m_BHAllocCount += tracker_data[pivot_idx + 1].m_BHAllocCount;

          memmove( tracker_data + pivot_idx + 1, tracker_data + pivot_idx + 2, k_BlockHeaderSize * ( tracker_info.m_TrackedCount - pivot_idx + 2 ) );
          tracker_info.m_TrackedCount--;
          return true;
        }
        else if( left_dist == 0 ) // coalesce left
        {
          CoalesceSlot( pivot_idx, left_idx, slot_idx, slot_bins );
          return true;
        }
        else if( right_dist == 0 ) // coalesce right
        {
          CoalesceSlot( pivot_idx + 1, right_idx, slot_idx, slot_bins );
          return true;
        }

        // insert between left & right
        InsertSlot( pivot_idx + 1, true );
        return true;
      }
      else // left_idx < right_idx < slot_idx
      {
        head = pivot_idx + 1;
      }
    }
    else // slot_idx < left_idx < right_idx
    {
      tail = pivot_idx;
    }
  }
  
  if( head == 0 ) // merge/insert at head
  {
    int32_t base_idx  = EXTRACT_IDX( tracker_data[0].m_BHIndexNPartition );
    int32_t base_bins = tracker_data[0].m_BHAllocCount;
    int32_t head_dist = base_idx - (int)( slot_idx + slot_bins );
    int32_t tail_dist = (int)slot_idx - ( base_idx + base_bins );

    if( head_dist == 0 || tail_dist == 0 ) // merge/insert at tail
    {
      CoalesceSlot( 0, base_idx, slot_idx, slot_bins );
    }
    else
    {
      InsertSlot( 0, true );
    }
  }
  else // merge/insert at tail
  {
    int32_t base_idx  = EXTRACT_IDX( tracker_data[tracker_info.m_TrackedCount - 1].m_BHIndexNPartition );
    int32_t base_bins = tracker_data[ tracker_info.m_TrackedCount - 1 ].m_BHAllocCount;
    int32_t head_dist = base_idx - (int)( slot_idx + slot_bins );
    int32_t tail_dist = (int)slot_idx - ( base_idx + base_bins );
    
    if( head_dist == 0 || tail_dist == 0 )
    {
      CoalesceSlot( tracker_info.m_TrackedCount - 1, base_idx, slot_idx, slot_bins );
    }
    else
    {
      InsertSlot( tracker_info.m_TrackedCount - 1, false );
    }
  }

  return true;
}

void MemAlloc::PrintHeapStatus( uint32_t thread_id )
{
  MemAlloc::FreeList& free_list = s_MemoryDataThreads[thread_id].m_FreeList;

  // Total allocated memory
  ByteFormat b_data = TranslateByteFormat( free_list.m_TotalPartitionSize + k_BlockHeaderSize * free_list.m_TotalPartitionBins, ByteFormat::k_Byte );
  printf( "o Total allocated heap memory : %10.3f %2s\n", b_data.m_Size, b_data.m_Type  );
  b_data = TranslateByteFormat( free_list.m_TotalPartitionSize, ByteFormat::k_Byte );
  printf( "  - Total partition sizes     : %10.3f %2s\n", b_data.m_Size, b_data.m_Type );
  b_data = TranslateByteFormat( k_BlockHeaderSize * free_list.m_TotalPartitionBins, ByteFormat::k_Byte );
  printf( "  - Tracker list size         : %10.3f %2s\n", b_data.m_Size, b_data.m_Type );
  
  // Partition characteristics
  printf( "o Partition Data:\n" );
  for(uint32_t ipartition = 0; ipartition < k_NumLvl; ipartition++)
  {
    PartitionData& part_data = free_list.m_PartitionLvlDetails[ipartition];

    b_data             = TranslateByteFormat( part_data.m_BinSize, ByteFormat::k_Byte );
    ByteFormat b_data2 = TranslateByteFormat( part_data.m_BinCount * part_data.m_BinSize, ByteFormat::k_Byte );
    
    printf( "  - Partition %u :: %10.3f %2s (bin size + %zu B), %10u (bin count), %10.3f %2s (partition size)\n", ipartition, b_data.m_Size, b_data.m_Type, sizeof(BlockHeader), part_data.m_BinCount, b_data2.m_Size, b_data2.m_Type );
  }
  
  // For each partition: allocations && free memory (percentages), fragmentation(?)
  printf( "o Tracker Data :\n" );
  char percent_str[51] = {};
  for(uint32_t ipartition = 0; ipartition < k_NumLvl; ipartition++)
  {
    printf( "  - Partition %u:\n", ipartition );

    PartitionData& part_data    = free_list.m_PartitionLvlDetails[ipartition];
    TrackerData&   tracked_data = free_list.m_TrackerInfo[ipartition];

    float    mem_occupancy = (float)tracked_data.m_BinOccupancy / (float)part_data.m_BinCount;
    uint32_t bar_ticks     = (uint32_t)( ( sizeof( percent_str ) - 1 ) * ( 1.f - mem_occupancy ) );

    memset( percent_str, 0, sizeof( percent_str ) );
    memset( percent_str, '-', sizeof( percent_str ) - 1 );
    memset( percent_str, 'x', bar_ticks );

    printf( "    [%-*s] (%.3f%% allocated, free slots %u)\n", (int)sizeof( percent_str ) - 1, percent_str, ( 1.f - mem_occupancy ) * 100.f, tracked_data.m_TrackedCount );

    uint32_t total_free_blocks = 0;
    uint32_t largest_block     = 0;
    for(uint32_t itracker_idx = 0; itracker_idx < tracked_data.m_TrackedCount; itracker_idx++)
    {
      BlockHeader& tracker = free_list.m_Tracker[tracked_data.m_PartitionOffset + itracker_idx];
      b_data               = TranslateByteFormat( tracker.m_BHAllocCount * part_data.m_BinSize, ByteFormat::k_Byte );
      
      total_free_blocks += tracker.m_BHAllocCount;
      largest_block      = tracker.m_BHAllocCount > largest_block ? tracker.m_BHAllocCount : largest_block;

      printf( "    | %10u, %10u (coalesced blocks), %10.5f %2s\n", EXTRACT_IDX( tracker.m_BHIndexNPartition ), tracker.m_BHAllocCount, b_data.m_Size, b_data.m_Type );
    }
    printf( "    - fragmentation %10.5f%%\n", total_free_blocks == 0 ? 100.f : (double)( total_free_blocks - largest_block ) / (double)total_free_blocks );
  }
}

MemAlloc::ByteFormat MemAlloc::TranslateByteFormat( uint32_t size, uint8_t byte_type )
{
  if( byte_type == ByteFormat::k_Byte )
  {
    if( size > 1024 && size < 1048576 )
    {
      return { size / 1024.f, "kB" };
    }
    else if( size > 1048576 )
    {
      return { size / 1048576.f, "mB" };
    }
    return { (float)size, "B" };
  }
  else if( byte_type == ByteFormat::k_KiloByte )
  {
    if( size > 1024 )
    {
      return { size / 1024.f, "mB" };
    }
    return { (float)size, "kB" };
  }
  return { (float)size, "mB" };
}

//***********************************************************************************************
//***********************************************************************************************

// Size of partition is restricted by 2 factors: freelist tracker && block header
// * Each bin in the partition must support a blockheader
// * Each bin in the partition must be possibly represented by a tracker in the free list
static MemAlloc::PartitionData GetPartition( const uint32_t total_size, uint16_t bin_size, float percentage )
{
  MemAlloc::PartitionData part_output;

  uint32_t fixed_part_size = CalcAllignedAllocSize( (uint32_t)( (double)total_size * (double)percentage ) );

  part_output.m_BinSize  = bin_size + k_BlockHeaderSize;
  // m_BinCount calculation : k_BlockHeaderSize is added to the denominator because each bin needs
  // a corresponding tracker in the free list. This calculation saves space for the resulting free
  // list tracking array
  part_output.m_BinCount = fixed_part_size / ( part_output.m_BinSize + k_BlockHeaderSize ); 
  part_output.m_Size     = part_output.m_BinCount * part_output.m_BinSize;

  return part_output;
}

static uint32_t CalcAllignedAllocSize( uint32_t input, uint32_t alignment )
{
  const uint32_t remainder = input % alignment;
  input += remainder ? alignment : 0;

  return input;
}