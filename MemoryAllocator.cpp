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

static void*              s_MemBlockPtr;
static MemAlloc::FreeList s_FreeList;

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

MemAlloc::QueryResult MemAlloc::CalcAllocPartitionAndSize( uint32_t alloc_size, uint32_t bucket_hint )
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
    printf("-- %d\n", chosen_bucket_bin_count * heap_bin);
    chosen_bucket_bin_count += 1;  // make sure there's room for header
  }

  result.m_AllocBins = chosen_bucket_bin_count;
  result.m_Status    = chosen_bucket;

  // check partition for free space
  
  if( s_FreeList.m_TrackerInfo[chosen_bucket_idx].m_BinOccupancy < chosen_bucket_bin_count )
  {
    result.m_Status |= QueryResult::k_NoFreeSpace;
    return result;
  }

  int32_t      free_bin_idx      = -1;
  TrackerData& tracked_bins_info = s_FreeList.m_TrackerInfo[chosen_bucket_idx];
  BlockHeader* tracked_bin       = s_FreeList.m_Tracker + tracked_bins_info.m_PartitionOffset;

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
  ASSERT_F( CalcAllignedAllocSize( tracker_list_size + s_FreeList.m_TotalPartitionSize ) < (uint32_t)-1, 
            "Memory to alloc exceeds limit : %zu\n",
            (uint32_t)-1 );
  
  s_MemBlockPtr = calloc( CalcAllignedAllocSize( tracker_list_size + s_FreeList.m_TotalPartitionSize ), sizeof( unsigned char ) );
  
  ASSERT_F( s_MemBlockPtr, "Failed to initialize memory" );

  // set addresses for memory tracker list & partitions

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

  for(uint32_t itracker_idx = 0, tracker_offsets = 0; itracker_idx < MemAlloc::k_NumLvl; itracker_idx++)
  {
    s_FreeList.m_TrackerInfo[itracker_idx].m_HeadIdx      = 0;
    s_FreeList.m_TrackerInfo[itracker_idx].m_TrackedCount = 1;

    MemAlloc::BlockHeader& mem_tag = s_FreeList.m_Tracker[tracker_offsets];
    mem_tag.m_BHAllocCount         = s_FreeList.m_PartitionLvlDetails[itracker_idx].m_BinCount;
    mem_tag.m_BHIndexNPartition    = itracker_idx; // partition index is encoded in lower 4 bits 

    s_FreeList.m_TrackerInfo[itracker_idx].m_BinOccupancy    = mem_tag.m_BHAllocCount; 
    s_FreeList.m_TrackerInfo[itracker_idx].m_PartitionOffset = tracker_offsets;

    tracker_offsets += s_FreeList.m_PartitionLvlDetails[itracker_idx].m_BinCount;
  }
}

void* MemAlloc::Alloc( uint32_t byte_size, uint32_t bucket_hints, uint8_t block_size )
{
  if ( byte_size == 0 )
  {
    return nullptr; // maybe trigger assert(?)
  }

  // block_size needs to even & divisible by 4 bytes
  ASSERT_F( block_size % 4 == 0,
            "Alloc only accepts 4 byte alligned block sizes (%u)",
            block_size );
  
  uint32_t aligned_alloc = CalcAllignedAllocSize( byte_size, block_size );

  QueryResult request = CalcAllocPartitionAndSize( aligned_alloc, bucket_hints );

  // handle allocation failures

  if( !( request.m_Status & QueryResult::k_Success ) )
  {
    if( bucket_hints & k_HintStrictSize )
    {
      ASSERT_F( false, "Failed to allocate memory based on request" );
    }
    else
    {
      // re-attempt allocating memory by cycling thru each partition
      for(uint32_t ipartition = 0; ipartition < sizeof( s_HeapBinSizes ) && !( request.m_Status & QueryResult::k_Success ); ipartition++)
      {
        request = CalcAllocPartitionAndSize( aligned_alloc, k_HintStrictSize | s_HeapBinSizes[ipartition] );
      }

      ASSERT_F( request.m_Status & QueryResult::k_Success, "Unknown failure when attempting to allocate memory" );
    }
  }

  // mark && assign memory
  const uint32_t bin_size = ( request.m_Status & ~( QueryResult::k_Success ) ) + k_BlockHeaderSize;

  ASSERT_F( bin_size > k_BlockHeaderSize, "Missing bin size returned from CalcAllocPartitionAndSize()" );

  int32_t partition_idx = -1;
  for(uint32_t ipartition = 0; ipartition < k_NumLvl && partition_idx < 0; ipartition++)
  {
    partition_idx = s_HeapBinSizes[ipartition] == ( bin_size - k_BlockHeaderSize ) ? ipartition : -1;
  }
  ASSERT_F( partition_idx >= 0, "Invalid bin size returned from CalcAllocPartitionAndSize()" );
  
  TrackerData& free_part_info = s_FreeList.m_TrackerInfo[partition_idx];
  BlockHeader* free_slot      = s_FreeList.m_Tracker + ( free_part_info.m_PartitionOffset + request.m_TrackerSelectedIdx );
  
  printf( "- Free Slot : index %u, alloc count %u\n", EXTRACT_IDX( free_slot->m_BHIndexNPartition ), free_slot->m_BHAllocCount );

  BlockHeader* mem_marker         = (BlockHeader*)s_FreeList.m_PartitionLvls[partition_idx] + ( bin_size * EXTRACT_IDX( free_slot->m_BHIndexNPartition ) );
  mem_marker->m_BHIndexNPartition = free_slot->m_BHIndexNPartition;
  mem_marker->m_BHAllocCount      = request.m_AllocBins;

  // update free memory list : free memory slots & m_BinOccupancy in selected partition
  
  printf( "%u partition ( %u ) :: free bins %u, alloc'd bins %u (%u B)\n",
          bin_size - k_BlockHeaderSize,
          partition_idx,
          free_part_info.m_BinOccupancy,
          mem_marker->m_BHAllocCount,
          bin_size * mem_marker->m_BHAllocCount );

  // subtract & update || remove free slot from list
  
  if( free_slot->m_BHAllocCount > request.m_AllocBins )
  {
    free_slot->m_BHAllocCount -= request.m_AllocBins;
    uint32_t index             = EXTRACT_IDX( free_slot->m_BHIndexNPartition );
             index            += request.m_AllocBins;

    free_slot->m_BHIndexNPartition = SET_INDEX_PART( index, partition_idx );

    printf( "- Modified free space : %u (%u)\n",
            EXTRACT_IDX( free_slot->m_BHIndexNPartition ),
            free_slot->m_BHAllocCount );
  }
  else
  {
    // - find index of free_slot in the list
    //    - if its the only thing in the list         : decrement the count
    //    - if its at the end of the list             : decrement the count
    //    - if its in the middle | front of the list  : shift list shorter by k_BlockHeaderSize w/ memmove
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

bool MemAlloc::Free( void* data_ptr )
{
  // This function will maintain the invariant : each free list partition is sorted incrementally by block index

  if( data_ptr == nullptr )
  {
    return false;
  }

  printf( "\nFree\n" );

  // grab block header and partition index
  BlockHeader* header      = (BlockHeader*)( (unsigned char*)data_ptr - k_BlockHeaderSize );
  const uint32_t slot_idx  = header->m_BHIndexNPartition >> BlockHeader::k_IndexBitShift;
  const uint32_t part_idx  = header->m_BHIndexNPartition & BlockHeader::k_PartitionMask;
  const uint32_t slot_bins = header->m_BHAllocCount;
  printf( "o Header : index %u, bins %u, partition %u\n", slot_idx, slot_bins, part_idx );

  TrackerData& tracker_info = s_FreeList.m_TrackerInfo[part_idx];
  BlockHeader* tracker_data = s_FreeList.m_Tracker + tracker_info.m_PartitionOffset;
  
  // attempt to free/coalesce memory region

  // - check if free list is empty. If so, add new slot
  if( tracker_info.m_TrackedCount == 0 )
  {
    *tracker_data = *header;
    tracker_info.m_TrackedCount++;

    return true;
  }

  auto CoalesceSlot = [&tracker_info, &tracker_data]( uint32_t tracker_idx, uint32_t base_idx, uint32_t coalesce_idx, uint32_t coalesce_bins )
  {
    printf( "o merging %u( bin count %u ) --> %u\n", coalesce_idx, coalesce_bins, base_idx);
    (void*)tracker_idx;

    tracker_data[tracker_idx].m_BHIndexNPartition  = SET_INDEX_PART( base_idx < coalesce_idx ? base_idx : coalesce_idx, EXTRACT_PART( tracker_data[tracker_idx].m_BHIndexNPartition ) );
    tracker_data[tracker_idx].m_BHAllocCount      += coalesce_bins;

    tracker_info.m_BinOccupancy += coalesce_bins;
  };

  auto InsertShot = [&tracker_info, &tracker_data, &header]( uint32_t tracker_idx, uint32_t base_idx, bool shift_right )
  {
    switch( (int)shift_right )
    {
      case 0: // append
      {
        printf( "o appending %u\n", base_idx );
        tracker_data[tracker_idx] = *header;

        break;
      }
      default: // shift then set
      {
        printf( "o inserting %u at index %u\n", base_idx, tracker_idx );

        memmove( tracker_data + tracker_idx + 1, tracker_data + tracker_idx, k_BlockHeaderSize * ( tracker_info.m_TrackedCount - tracker_idx ) );
        tracker_data[tracker_idx] = *header;

        break;
      }
    }

    tracker_info.m_BinOccupancy += header->m_BHAllocCount;
    tracker_info.m_TrackedCount++;
  };

  // - check if free list has 1 slot
  if( tracker_info.m_TrackedCount == 1 )
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
        InsertShot( 0, slot_idx, true ); // new head
      }
      else
      {
        InsertShot( 1, slot_idx, false); // append
      }
    }

    return true;
  }
  
  // - use divide & conquer to find its spot in list ( using index as pivot )
  //   - if index is +-1 from the free slot either in front or back of it, coalesce
  //   - if can't coalesce, shift list to the right & insert reclaimed memory
  int32_t head       = 0;
  int32_t tail       = tracker_info.m_TrackedCount - 1;
  while( (int)( tail - head ) > 0 )
  {
    uint32_t pivot_idx = head + ( ( tail - head ) / 2 );
    printf( "--size %u (%u : %u) pivot %u\n", tail - head, head, tail, pivot_idx );

    int32_t left_idx        = EXTRACT_IDX( tracker_data[pivot_idx].m_BHIndexNPartition  );
    int32_t right_idx       = EXTRACT_IDX( tracker_data[pivot_idx + 1].m_BHIndexNPartition );
    int32_t left_idx_offset = tracker_data[pivot_idx].m_BHAllocCount;

    printf( "o Bang. Divide & Conquer : %.2u | %.2u | %.2u \n", left_idx, slot_idx, right_idx );

    int32_t left_dist  = (int)slot_idx - ( left_idx + left_idx_offset );
    int32_t right_dist = right_idx - (int)( slot_idx + slot_bins );
    
    printf( "o distance : left %.2d, right %.2d\n", left_dist, right_dist );

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
          // continue coalesce if possible
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
        InsertShot( pivot_idx + 1, slot_idx, true );
        return true;
      }
      else // left_idx < right_idx < slot_idx
      {
        head = pivot_idx + 1;
      }
    }
    else
    {
      tail = pivot_idx; // slot_idx < left_idx < right_idx
    }
  }
  
  if( head == 0 ) // merge/insert at head
  {
    printf( "o Bang 4. Divide & Conquer : %u\n", head );

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
      InsertShot( 0, slot_idx, true );
    }
  }
  else
  {
    printf( "o Bang 5. Divide & Conquer : %u\n", tail );

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
      InsertShot( tracker_info.m_TrackedCount - 1, slot_idx, false );
    }
  }

  return true;
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

    float    mem_occupancy = (float)tracked_data.m_BinOccupancy / (float)part_data.m_BinCount;
    uint32_t bar_ticks     = (uint32_t)( ( sizeof( percent_str ) - 1 ) * ( 1.f - mem_occupancy ) );

    memset( percent_str, 0, sizeof( percent_str ) );
    memset( percent_str, 'x', bar_ticks );

    printf( "    [%*s] (%.3f%% allocated, free slots %u)\n", 20, percent_str, ( 1.f - mem_occupancy ) * 100.f, tracked_data.m_TrackedCount );

    for(uint32_t itracker_idx = 0; itracker_idx < tracked_data.m_TrackedCount; itracker_idx++)
    {
      BlockHeader& tracker = s_FreeList.m_Tracker[tracked_data.m_PartitionOffset + itracker_idx];
      b_data               = TranslateByteFormat( tracker.m_BHAllocCount * part_data.m_BinSize, ByteFormat::k_Byte );
      
      printf( "    | %u, %.6u (coalesced blocks), %10.5f %2s\n", EXTRACT_IDX( tracker.m_BHIndexNPartition ), tracker.m_BHAllocCount, b_data.m_Size, b_data.m_Type );
    }
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