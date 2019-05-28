#include "MemoryAllocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define BASE_ALIGN 8
#define BASE_BUCKET 32

#ifndef MEM_MAX_SIZE
#define MEM_MAX_SIZE ( 0x1 << 20 ) * 500 // 500 mb
#endif

#define SET_INDEX_PART( INDEX, PARTITION ) ( ( INDEX ) << k_HeapBlockIndexBitShift ) | PARTITION

#define EXTRACT_IDX( BLOCK_IDX_PARTION ) ( BLOCK_IDX_PARTION >> k_HeapBlockIndexBitShift )

#define EXTRACT_PART( BLOCK_IDX_PARTION ) ( BLOCK_IDX_PARTION & k_HeapBlockPartitionMask )

//static void*              s_MemBlockPtr;
//static Heap::FreeList s_FreeList;

struct MemoryData
{
  void*               m_MemBlock;
  struct HeapFreeList m_FreeList;
};

#define MAX_MEM_THREADS 8
static struct MemoryData s_MemoryDataThreads[MAX_MEM_THREADS];
static bool              s_MemoryDataThreadValidFlag[MAX_MEM_THREADS];

static struct HeapPartitionData GetPartition( const uint64_t total_size, uint16_t bin_size, float percentage );
static uint64_t CalcAllignedAllocSize( uint64_t input, uint32_t alignment );

static const uint32_t s_BlockHeaderSize = (uint32_t)sizeof( struct HeapBlockHeader );

static uint16_t s_HeapBinSizes[k_HeapNumLvl] = { k_HeapLevel0,
                                                 k_HeapLevel1,
                                                 k_HeapLevel2,
                                                 k_HeapLevel3,
                                                 k_HeapLevel4,
                                                 k_HeapLevel5 };

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

void HeapInitBase( uint64_t alloc_size, uint32_t thread_id )
{
  struct HeapFreeList* free_list = &s_MemoryDataThreads[thread_id].m_FreeList;
  void*                mem_block = s_MemoryDataThreads[thread_id].m_MemBlock;

  memset( free_list, 0, sizeof( struct HeapFreeList ) );
  /*
  o Partition scheme :
  ===============================================================================
  |  k_Level0  |  k_Level1  |  k_Level2  |  k_Level3  |  k_Level4  |  k_Level5  |
  |     5%     |    10%     |    15%     |    20%     |    25%     |    25%     |
  ===============================================================================
  */

  alloc_size = alloc_size == 0 ? MEM_MAX_SIZE : alloc_size;
  
  // calculate partition stats per memory level
  free_list->m_PartitionLvlDetails[0] = GetPartition( MEM_MAX_SIZE, k_HeapLevel0, 0.05f );
  free_list->m_PartitionLvlDetails[1] = GetPartition( MEM_MAX_SIZE, k_HeapLevel1, 0.10f );
  free_list->m_PartitionLvlDetails[2] = GetPartition( MEM_MAX_SIZE, k_HeapLevel2, 0.15f );
  free_list->m_PartitionLvlDetails[3] = GetPartition( MEM_MAX_SIZE, k_HeapLevel3, 0.20f );
  free_list->m_PartitionLvlDetails[4] = GetPartition( MEM_MAX_SIZE, k_HeapLevel4, 0.25f );
  free_list->m_PartitionLvlDetails[5] = GetPartition( MEM_MAX_SIZE, k_HeapLevel5, 0.25f );

  for(uint32_t ibin = 0; ibin < k_HeapNumLvl; ibin++)
  {
    free_list->m_TotalPartitionSize += free_list->m_PartitionLvlDetails[ibin].m_Size;
    free_list->m_TotalPartitionBins += free_list->m_PartitionLvlDetails[ibin].m_BinCount;
  }
  uint64_t tracker_list_size = s_BlockHeaderSize * free_list->m_TotalPartitionBins;

  // get heap memory from system for free list && partitions
  ASSERT_F( CalcAllignedAllocSize( tracker_list_size + free_list->m_TotalPartitionSize, BASE_ALIGN ) < (uint32_t)-1, 
            "Memory to alloc exceeds limit : %zu\n",
            (uint32_t)-1 );
  
  mem_block = calloc( CalcAllignedAllocSize( tracker_list_size + free_list->m_TotalPartitionSize, BASE_ALIGN ), sizeof( unsigned char ) );
  
  ASSERT_F( mem_block, "Failed to initialize memory" );

  // set addresses for memory tracker list & partitions

  free_list->m_Tracker    = (struct HeapBlockHeader*)mem_block;
  unsigned char* byte_ptr = (unsigned char*)mem_block;
  
  free_list->m_PartitionLvls[0] = byte_ptr + tracker_list_size; // offset b/c tracker list is at front
  free_list->m_PartitionLvls[1] = free_list->m_PartitionLvls[0] + free_list->m_PartitionLvlDetails[0].m_Size;
  free_list->m_PartitionLvls[2] = free_list->m_PartitionLvls[1] + free_list->m_PartitionLvlDetails[1].m_Size;
  free_list->m_PartitionLvls[3] = free_list->m_PartitionLvls[2] + free_list->m_PartitionLvlDetails[2].m_Size;
  free_list->m_PartitionLvls[4] = free_list->m_PartitionLvls[3] + free_list->m_PartitionLvlDetails[3].m_Size;
  free_list->m_PartitionLvls[5] = free_list->m_PartitionLvls[4] + free_list->m_PartitionLvlDetails[4].m_Size;
  
  ASSERT_F( ( free_list->m_PartitionLvls[5] + free_list->m_PartitionLvlDetails[5].m_Size ) == ( byte_ptr + tracker_list_size + free_list->m_TotalPartitionSize ), "Invalid buffer calculations {%p : %p}", free_list->m_PartitionLvls[5] + free_list->m_PartitionLvlDetails[5].m_Size, byte_ptr + tracker_list_size + free_list->m_TotalPartitionSize );

  // initialize tracker data for each memory partition

  for( uint64_t ipart_idx = 0, tracker_offsets = 0; ipart_idx < k_HeapNumLvl; ipart_idx++)
  {
    free_list->m_TrackerInfo[ipart_idx].m_HeadIdx      = 0;
    free_list->m_TrackerInfo[ipart_idx].m_TrackedCount = 1;

    struct HeapBlockHeader* mem_tag = &free_list->m_Tracker[tracker_offsets];
    mem_tag->m_BHAllocCount         = free_list->m_PartitionLvlDetails[ipart_idx].m_BinCount;
    mem_tag->m_BHIndexNPartition    = SET_INDEX_PART( 0, ipart_idx ); // partition index is encoded in lower 4 bits 

    free_list->m_TrackerInfo[ipart_idx].m_BinOccupancy    = mem_tag->m_BHAllocCount; 
    free_list->m_TrackerInfo[ipart_idx].m_PartitionOffset = tracker_offsets;

    tracker_offsets += free_list->m_PartitionLvlDetails[ipart_idx].m_BinCount;
  }

  s_MemoryDataThreadValidFlag[thread_id] = true;
}

bool HeapQueryBaseIsValid(uint32_t thread_id)
{
  return s_MemoryDataThreadValidFlag[thread_id];
}

void* HeapAllocate( uint64_t byte_size, uint32_t bucket_hints, uint8_t block_size, uint64_t debug_hash, uint32_t thread_id )
{
  if ( byte_size == 0 )
  {
    return NULL; // maybe trigger assert(?)
  }

  block_size = block_size ? block_size : 4;
  
  uint64_t aligned_alloc = CalcAllignedAllocSize( byte_size, block_size );

  struct HeapQueryResult request = HeapCalcAllocPartitionAndSize( aligned_alloc, bucket_hints, thread_id );

  if( !( request.m_Status & k_QuerySuccess ) ) // maybe assert(?)
  {
    if( bucket_hints & k_HeapHintStrictSize )
    {
      return NULL; // maybe assert(?)
    }
    return NULL; // maybe assert(?)
  }

  // mark && assign memory

  const uint32_t bin_size = ( request.m_Status & ~( k_QuerySuccess ) ) + s_BlockHeaderSize;

  int32_t partition_idx = -1;
  for(uint32_t ipartition = 0; ipartition < k_HeapNumLvl && partition_idx < 0; ipartition++)
  {
    partition_idx = s_HeapBinSizes[ipartition] == ( bin_size - s_BlockHeaderSize ) ? ipartition : -1;
  }
  ASSERT_F( partition_idx >= 0, "Invalid bin size returned from CalcAllocPartitionAndSize()" );
  
  struct HeapFreeList* free_list = &s_MemoryDataThreads[thread_id].m_FreeList;

  struct HeapTrackerData* free_part_info = &free_list->m_TrackerInfo[partition_idx];
  struct HeapBlockHeader* free_slot      = free_list->m_Tracker + ( free_part_info->m_PartitionOffset + request.m_TrackerSelectedIdx );
  
  struct HeapBlockHeader* mem_marker = (struct HeapBlockHeader*)( free_list->m_PartitionLvls[partition_idx] + ( bin_size * EXTRACT_IDX( free_slot->m_BHIndexNPartition ) ) );
  mem_marker->m_BHIndexNPartition    = free_slot->m_BHIndexNPartition;
  mem_marker->m_BHAllocCount         = request.m_AllocBins;

  // subtract & update || remove free slot from list
  if( free_slot->m_BHAllocCount > request.m_AllocBins )
  {
    free_slot->m_BHAllocCount -= request.m_AllocBins;
    uint64_t index             = EXTRACT_IDX( free_slot->m_BHIndexNPartition );
             index            += request.m_AllocBins;

    free_slot->m_BHIndexNPartition = SET_INDEX_PART( index, partition_idx );
  }
  else
  {
    // find index of free_slot in the list
    if( free_part_info->m_TrackedCount == 1 || ( request.m_TrackerSelectedIdx + 1 ) == free_part_info->m_TrackedCount )
    {
      memset( free_slot, 0, s_BlockHeaderSize );
    }
    else
    {
      memmove( free_slot, free_slot + 1, s_BlockHeaderSize * ( free_part_info->m_TrackedCount - ( request.m_TrackerSelectedIdx + 1 ) ) );
    }
    free_part_info->m_TrackedCount--;
  }
  free_part_info->m_BinOccupancy -= request.m_AllocBins;

#ifdef TAG_MEMORY
  mem_marker->m_BHTagMesh = debug_hash;
#else
  debug_hash = debug_hash;
#endif // TAG_MEMORY

  unsigned char* data    = (unsigned char*)mem_marker + s_BlockHeaderSize;
                 data[0] = 1; // set value of 1st point to a number other than 0
  
  return data; // return pointer to memory region after header
}

static void CoalesceSlot( struct HeapTrackerData* tracker_info, struct HeapBlockHeader tracker_data[], uint64_t tracker_idx, uint64_t base_idx, uint64_t coalesce_idx, uint64_t coalesce_bins )
{
  tracker_data[tracker_idx].m_BHIndexNPartition  = SET_INDEX_PART( base_idx < coalesce_idx ? base_idx : coalesce_idx, EXTRACT_PART( tracker_data[tracker_idx].m_BHIndexNPartition ) );
  tracker_data[tracker_idx].m_BHAllocCount      += coalesce_bins;
  tracker_info->m_BinOccupancy                  += coalesce_bins;
}

static void InsertSlot( struct HeapTrackerData* tracker_info, struct HeapBlockHeader tracker_data[],  struct HeapBlockHeader* header, uint64_t tracker_idx, bool shift_right )
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
        memmove( tracker_data + tracker_idx + 1, tracker_data + tracker_idx, s_BlockHeaderSize * ( tracker_info->m_TrackedCount - tracker_idx ) );
        tracker_data[tracker_idx] = *header;
        break;
      }
    }

    tracker_info->m_BinOccupancy += header->m_BHAllocCount;
    tracker_info->m_TrackedCount++;
  };

bool HeapRelease( void* data_ptr, uint32_t thread_id )
{
  // This function will maintain the invariant : each free list partition is sorted incrementally by block index

  if( data_ptr == NULL )
  {
    return false;
  }

  struct HeapBlockHeader header       = *( (struct HeapBlockHeader*)( (unsigned char*)data_ptr - s_BlockHeaderSize ) );
  const uint64_t slot_idx  = EXTRACT_IDX( header.m_BHIndexNPartition );
  const uint64_t part_idx  = EXTRACT_PART( header.m_BHIndexNPartition );
  const uint64_t slot_bins = header.m_BHAllocCount;

  // clear marker/data once copied
  memset( (unsigned char*)data_ptr - s_BlockHeaderSize, 0, s_BlockHeaderSize + 1 );
  
  struct HeapFreeList* free_list = &s_MemoryDataThreads[thread_id].m_FreeList;

  struct HeapTrackerData* tracker_info = &free_list->m_TrackerInfo[part_idx];
  struct HeapBlockHeader* tracker_data = free_list->m_Tracker + tracker_info->m_PartitionOffset;
  
  if( tracker_info->m_TrackedCount == 0 ) // if free list is empty, add new slot
  {
    tracker_data[0] = header;
    tracker_info->m_TrackedCount++;
    return true;
  }

  if( tracker_info->m_TrackedCount == 1 ) // if free list has 1 slot, coalesce or insert
  {
    int64_t base_idx  = EXTRACT_IDX( tracker_data[0].m_BHIndexNPartition );
    int64_t base_bins = tracker_data[0].m_BHAllocCount;
    int64_t head_dist = base_idx - (int)( slot_idx + slot_bins );
    int64_t tail_dist = (int)slot_idx - ( base_idx + base_bins );

    if( head_dist == 0 || tail_dist == 0 )
    {
      CoalesceSlot( tracker_info, tracker_data, 0, base_idx, slot_idx, slot_bins );
    }
    else
    {
      if( head_dist > 0 )
      {
        InsertSlot( tracker_info, tracker_data, &header, 0, true ); // new head
      }
      else
      {
        InsertSlot( tracker_info, tracker_data, &header, 1, false ); // append
      }
    }
    return true;
  }
  
  // - use divide & conquer to find its spot in list
  int64_t head = 0;
  int64_t tail = tracker_info->m_TrackedCount - 1;
  while( ( tail - head ) > 0 )
  {
    uint64_t pivot_idx = head + ( ( tail - head ) / 2 );

    int64_t left_idx        = EXTRACT_IDX( tracker_data[pivot_idx].m_BHIndexNPartition );
    int64_t right_idx       = EXTRACT_IDX( tracker_data[pivot_idx + 1].m_BHIndexNPartition );
    int64_t left_idx_offset = tracker_data[pivot_idx].m_BHAllocCount;

    int64_t left_dist  = (int)slot_idx - ( left_idx + left_idx_offset );
    int64_t right_dist = right_idx - (int)( slot_idx + slot_bins );

    if( left_dist >= 0 )
    {
      if( right_dist >= 0 )
      {
        if( left_dist == 0 && right_dist == 0 ) // coalesce both sides
        {
          CoalesceSlot( tracker_info, tracker_data, pivot_idx, left_idx, slot_idx, slot_bins );
          tracker_data[pivot_idx].m_BHAllocCount += tracker_data[pivot_idx + 1].m_BHAllocCount;

          memmove( tracker_data + pivot_idx + 1, tracker_data + pivot_idx + 2, s_BlockHeaderSize * ( tracker_info->m_TrackedCount - pivot_idx + 2 ) );
          tracker_info->m_TrackedCount--;
          return true;
        }
        else if( left_dist == 0 ) // coalesce left
        {
          CoalesceSlot( tracker_info, tracker_data, pivot_idx, left_idx, slot_idx, slot_bins );
          return true;
        }
        else if( right_dist == 0 ) // coalesce right
        {
          CoalesceSlot( tracker_info, tracker_data, pivot_idx + 1, right_idx, slot_idx, slot_bins );
          return true;
        }

        // insert between left & right
        InsertSlot( tracker_info, tracker_data, &header, pivot_idx + 1, true );
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
    int64_t base_idx  = EXTRACT_IDX( tracker_data[0].m_BHIndexNPartition );
    int64_t base_bins = tracker_data[0].m_BHAllocCount;
    int64_t head_dist = base_idx - (int)( slot_idx + slot_bins );
    int64_t tail_dist = (int)slot_idx - ( base_idx + base_bins );

    if( head_dist == 0 || tail_dist == 0 ) // merge/insert at tail
    {
      CoalesceSlot( tracker_info, tracker_data, 0, base_idx, slot_idx, slot_bins );
      return true;
    }
    else
    {
      InsertSlot( tracker_info, tracker_data, &header, 0, true );
      return true;
    }
  }
  else // merge/insert at tail
  {
    int64_t base_idx  = EXTRACT_IDX( tracker_data[tracker_info->m_TrackedCount - 1].m_BHIndexNPartition );
    int64_t base_bins = tracker_data[ tracker_info->m_TrackedCount - 1 ].m_BHAllocCount;
    int64_t head_dist = base_idx - (int)( slot_idx + slot_bins );
    int64_t tail_dist = (int)slot_idx - ( base_idx + base_bins );
    
    if( head_dist == 0 || tail_dist == 0 )
    {
      CoalesceSlot( tracker_info, tracker_data, tracker_info->m_TrackedCount - 1, base_idx, slot_idx, slot_bins );
      return true;
    }
    else
    {
      InsertSlot( tracker_info, tracker_data, &header, tracker_info->m_TrackedCount, false );
      return true;
    }
  }
}

struct HeapQueryResult HeapCalcAllocPartitionAndSize( uint64_t alloc_size, uint32_t bucket_hint, uint32_t thread_id )
{
  struct HeapQueryResult result;
  alloc_size = CalcAllignedAllocSize( alloc_size, BASE_ALIGN );

  // Simple heuristic : find best-fit heap partition
  uint32_t chosen_bucket           = k_HeapLevel0;
  uint32_t chosen_bucket_idx       = 0;
  uint64_t chosen_bucket_bin_count = 0;

  for( size_t i = 0; i < k_HeapNumLvl && chosen_bucket < s_HeapBinSizes[k_HeapNumLvl - 1]; i++ )
  {
    if( alloc_size <= chosen_bucket )
    {
      break;
    }
    chosen_bucket = chosen_bucket << 1;
    chosen_bucket_idx++;
  }

  // Strict heuristic : Attempt to allocate using specified heap buckets (choose largest of specified buckets)
  if( bucket_hint & k_HeapHintStrictSize )
  {
    uint32_t max_bucket = 0;
    for( uint32_t ibin = 0; ibin < k_HeapNumLvl; ibin++ )
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
  uint32_t heap_bin = s_HeapBinSizes[chosen_bucket_idx] + s_BlockHeaderSize;
  chosen_bucket_bin_count  = alloc_size % heap_bin ? 1 : 0;
  chosen_bucket_bin_count += alloc_size / heap_bin;
  if( (int)( chosen_bucket_bin_count * heap_bin ) - s_BlockHeaderSize < 0 )
  {
    chosen_bucket_bin_count += 1;  // make sure there's room for header
  }

  result.m_AllocBins = chosen_bucket_bin_count;
  result.m_Status    = chosen_bucket;

  struct HeapFreeList* free_list = &s_MemoryDataThreads[thread_id].m_FreeList;

  if( free_list->m_TrackerInfo[chosen_bucket_idx].m_BinOccupancy < chosen_bucket_bin_count )
  {
    result.m_Status |= k_QueryNoFreeSpace;
    return result;
  }

  int32_t                 free_bin_idx      = -1;
  struct HeapTrackerData* tracked_bins_info = &free_list->m_TrackerInfo[chosen_bucket_idx];
  struct HeapBlockHeader* tracked_bin       = free_list->m_Tracker + tracked_bins_info->m_PartitionOffset;

  // find next available free space to allocate from
  for( uint32_t ibin = 0; ibin < tracked_bins_info->m_TrackedCount && free_bin_idx < 0; ibin++, tracked_bin++ )
  {
    free_bin_idx = tracked_bin->m_BHAllocCount >= chosen_bucket_bin_count ? ibin : -1;
  }
  
  // mark if partition exhibits too much fragmentation
  if( free_bin_idx == -1 )
  {
    result.m_Status |= k_QueryNoFreeSpace | k_QueryExcessFragmentation;
    return result;
  }

  result.m_Status             |= k_QuerySuccess;
  result.m_TrackerSelectedIdx  = free_bin_idx;

  return result;
}

void HeapPrintStatus(uint32_t thread_id)
{
  struct HeapFreeList* free_list = &s_MemoryDataThreads[thread_id].m_FreeList;

  // Total allocated memory
  struct ByteFormat b_data = TranslateByteFormat( free_list->m_TotalPartitionSize + s_BlockHeaderSize * free_list->m_TotalPartitionBins, k_FormatByte );
  printf( "o Total allocated heap memory : %10.3f %2s\n", b_data.m_Size, b_data.m_Type  );
  b_data = TranslateByteFormat( free_list->m_TotalPartitionSize, k_FormatByte );
  printf( "  - Total partition sizes     : %10.3f %2s\n", b_data.m_Size, b_data.m_Type );
  b_data = TranslateByteFormat( s_BlockHeaderSize * free_list->m_TotalPartitionBins, k_FormatByte );
  printf( "  - Tracker list size         : %10.3f %2s\n", b_data.m_Size, b_data.m_Type );
  
  // Partition characteristics
  printf( "o Partition Data:\n" );
  for(uint32_t ipartition = 0; ipartition < k_HeapNumLvl; ipartition++)
  {
    struct HeapPartitionData* part_data = &free_list->m_PartitionLvlDetails[ipartition];

    b_data                    = TranslateByteFormat( part_data->m_BinSize, k_FormatByte );
    struct ByteFormat b_data2 = TranslateByteFormat( part_data->m_BinCount * part_data->m_BinSize, k_FormatByte );
    
    printf( "  - Partition %u :: %10.3f %2s (bin size + %u B), %10" PRIu64 " (bin count), %10.3f %2s (partition size)\n", ipartition, b_data.m_Size, b_data.m_Type, s_BlockHeaderSize, part_data->m_BinCount, b_data2.m_Size, b_data2.m_Type );
  }
  
  // For each partition: allocations && free memory (percentages), fragmentation(?)
  printf( "o Tracker Data :\n" );
  
  char percent_str[51];
  
  for(uint32_t ipartition = 0; ipartition < k_HeapNumLvl; ipartition++)
  {
    printf( "  - Partition %u:\n", ipartition );

    struct HeapPartitionData* part_data    = &free_list->m_PartitionLvlDetails[ipartition];
    struct HeapTrackerData*   tracked_data = &free_list->m_TrackerInfo[ipartition];

    float    mem_occupancy = (float)tracked_data->m_BinOccupancy / (float)part_data->m_BinCount;
    uint32_t bar_ticks     = (uint32_t)( ( sizeof( percent_str ) - 1 ) * ( 1.f - mem_occupancy ) );

    memset( percent_str, 0, sizeof( percent_str ) );
    memset( percent_str, '-', sizeof( percent_str ) - 1 );
    memset( percent_str, 'x', bar_ticks );

    printf( "    [%-*s] (%.3f%% allocated, free slots %" PRIu64 ")\n", (int)sizeof( percent_str ) - 1, percent_str, ( 1.f - mem_occupancy ) * 100.f, tracked_data->m_TrackedCount );

    uint64_t total_free_blocks = 0;
    uint64_t largest_block     = 0;
    for(uint32_t itracker_idx = 0; itracker_idx < tracked_data->m_TrackedCount; itracker_idx++)
    {
      struct HeapBlockHeader* tracker = &free_list->m_Tracker[tracked_data->m_PartitionOffset + itracker_idx];
      b_data                          = TranslateByteFormat( tracker->m_BHAllocCount * part_data->m_BinSize, k_FormatByte );
      
      total_free_blocks += tracker->m_BHAllocCount;
      largest_block      = tracker->m_BHAllocCount > largest_block ? tracker->m_BHAllocCount : largest_block;

      printf( "    | %10" PRIu64 ", %10" PRIu64 " (coalesced blocks), %10.5f %2s\n", EXTRACT_IDX( tracker->m_BHIndexNPartition ), tracker->m_BHAllocCount, b_data.m_Size, b_data.m_Type );
    }
    printf( "    - fragmentation %10.5f%%\n", total_free_blocks == 0 ? 100.f : (double)( total_free_blocks - largest_block ) / (double)total_free_blocks );
  }
}

struct ByteFormat TranslateByteFormat( uint64_t size, uint8_t byte_type )
{
  struct ByteFormat bf = { 0 };

  if( byte_type == k_FormatByte )
  {
    if( size > 1024 && size < 1048576 )
    {
      bf.m_Size = size / 1024.f;
      bf.m_Type = "kB";
      return bf;
    }
    else if( size > 1048576 )
    {
      bf.m_Size = size / 1048576.f;
      bf.m_Type = "mB";
      return bf;
    }
    bf.m_Size = (double)size;
    bf.m_Type = "B";
    return bf;
  }
  else if( byte_type == k_FormatKiloByte )
  {
    if( size > 1024 )
    {
      bf.m_Size = size / 1024.f;
      bf.m_Type = "mB";
      return bf;
    }
    bf.m_Size = (double)size;
    bf.m_Type = "kB";
    return bf;
  }
  bf.m_Size = (double)size;
  bf.m_Type = "kB";
  return bf;
}

//***********************************************************************************************
//***********************************************************************************************

// Size of partition is restricted by 2 factors: freelist tracker && block header
// * Each bin in the partition must support a blockheader
// * Each bin in the partition must be possibly represented by a tracker in the free list
static struct HeapPartitionData GetPartition( const uint64_t total_size, uint16_t bin_size, float percentage )
{
  struct HeapPartitionData part_output = { 0 };

  uint64_t fixed_part_size = CalcAllignedAllocSize( (uint32_t)( (double)total_size * (double)percentage ), BASE_ALIGN );

  part_output.m_BinSize  = bin_size + s_BlockHeaderSize;
  // m_BinCount calculation : s_BlockHeaderSize is added to the denominator because each bin needs
  // a corresponding tracker in the free list. This calculation saves space for the resulting free
  // list tracking array
  part_output.m_BinCount = fixed_part_size / ( part_output.m_BinSize + s_BlockHeaderSize ); 
  part_output.m_Size     = part_output.m_BinCount * part_output.m_BinSize;

  return part_output;
}

static uint64_t CalcAllignedAllocSize( uint64_t input, uint32_t alignment )
{
  const uint32_t remainder = input % alignment;
  input += remainder ? alignment : 0;

  return input;
}
