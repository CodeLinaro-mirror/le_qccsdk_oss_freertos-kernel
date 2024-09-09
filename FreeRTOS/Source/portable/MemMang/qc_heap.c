/* =========================================================================

DESCRIPTION
  Implementation of a simple sub-allocator to manage memory allocations
  and deallocations using a Next Fit strategy.

Copyright (c) 1997-2016 by QUALCOMM Technologies Incorporated.  All Rights Reserved.
============================================================================ */

/* =========================================================================

                             Edit History

$PVCSPath: O:/src/asw/COMMON/vcs/memheap.c_v   1.2   22 Mar 2002 16:54:42   rajeevg  $
$Header: //components/rel/core.ioe/1.0/v3/rom/drivers/services/utils/src/memheap_lite.c#2 $ $DateTime: 2017/08/11 18:57:31 $ $Author: pwbldsvc $

when       who     what, where, why
--------   ---     ---------------------------------------------------------
05/13/15    ps       Initial Release on SPSS

============================================================================ */


/* ------------------------------------------------------------------------
** Includes
** ------------------------------------------------------------------------ */
#include "qc_heap.h"
#if (NT_FN_QC_HEAP == 1)
#include <stdio.h>
#include <stdlib.h>
//#include "PrngML.h"
#include "err.h"
#include "string.h"
//#include "qurt_mutex.h"
//#include "platform_wrapper.h"
#ifdef FEATURE_MEM_DEBUG
#include "qurt_thread.h"
#endif
#include "assert.h"

extern void *memset(void *dest, int c, size_t n);


/* Anonymous enum used for keeping tracking of memory. */
enum {
  kBlockFree = 1,/* needed some sapce for checksum in block header so reduced it to 1*/
  kBlockUsed = 0,
  kLastBlock = 1,/* needed some sapce for checksum in block header so reduced it to 1*/
  kMinChunkSize = 8,
  kMinBlockSize = 16,
};
/** @endcond */

enum {
  kUseLegacyImpl = 0,
  kUseBINsImpl = 2,
};

/* ------------------------------------------------------------------------
** Defines
** ------------------------------------------------------------------------ */
#define OVERFLOW_CHECK(elt_count, elt_size) (!(elt_count >= (1U<<10) || elt_size >= (1U<<22)) || ((((uint64)elt_count * (uint64)elt_size) >> 32) == 0))

#define BOUNDARY_CHECK(theBlock, heap_ptr) ((theBlock >= ((mem_heap_type*)heap_ptr)->first_block) && (theBlock < (((mem_heap_type*)heap_ptr)->first_block + heap_ptr->total_bytes)))

#define FRD_OFFSET_CHECK(block, heap_ptr) ((((mem_block_header_type *)block)->forw_offset + (char *)block) <= ((((char*)((mem_heap_type*)heap_ptr)->first_block) + ((mem_heap_type*)heap_ptr)->total_bytes)))

#define FOOTER_FRD_OFFSET_CHECK(block, heap_ptr, footer_offset) ((char *)block >= (char*)((mem_heap_type*)heap_ptr)->first_block) \
   &&((footer_offset) <= ((char *)block - ((char*)(((mem_heap_type*)heap_ptr)->first_block))))

//#define MEMHEAP_VERIFY_HEADER(block, heap_ptr) (((mem_block_header_type *)block)->header_guard == ((mem_heap_type*)heap_ptr)->block_header_guard ? TRUE : FALSE)

/* XOR based Guard byte calculations and restore */
#define INTEGRITY_CHECK_ON_USED_HEADER(magic_num_used, block) \
            (block[0]^block[1]^block[2]^block[3]^magic_num_used)


#define INTEGRITY_CHECK_ON_FREE_HEADER(magic_num_free, block) \
            (block[0]^block[1]^block[2]^block[3]^magic_num_free)


#define ADD_GUARD_BYTES_TO_USED_HEADER(magic_num_used, block) \
            (block[0] = block[1]^block[2]^block[3]^magic_num_used)


#define ADD_GUARD_BYTES_TO_FREE_HEADER(magic_num_free, block) \
            (block[0] = block[1]^block[2]^block[3]^magic_num_free)


#define MAX_HEAP_INIT 8
uint32 magic_num[MAX_HEAP_INIT] = {(uint32)-1,(uint32)-1,(uint32)-1,(uint32)-1,(uint32)-1,(uint32)-1,(uint32)-1,(uint32)-1};
uint16 magic_num_index_array[MAX_HEAP_INIT] = {0,1,2,3,4,5,6,7};
uint16 magic_num_index = 0;

/*============================================================================
                             FORWARD DECLARATIONS
============================================================================*/

mem_heap_type amss_mem_heap ;
static boolean is_amss_heap_initialized = FALSE;

/* Beginning address of the heap is provided by the linker */
static uint8_t *heap __attribute__((section(".heap"))) = &_ln_RAM_addr_heap_start__;
heap_config_type heap_config = {
  .heap_type = 0,
  .heap_start_address = &heap,
  .heap_size = 0 /* heap size determined at the heap initialization time */
};

static mem_block_header_type *mem_find_free_block(
   mem_heap_type *heap_ptr,
     /*  The heap to search for a free block
     */
   unsigned long  size_needed
     /*  The minimum size in bytes of the block needed (this size
         INCLUDES the size of the memory block header itself)
     */
);

static void mem_heap_get_random_num(void*  random_ptr, int random_len);


#define MEMHEAP_MIN_BLOCK_SIZE 16

/* Code to enter and exit critical sections.
*/
   #define BEGIN_CRITICAL_SECTION(heap) \
     do { \
       if ((heap)->lock_fnc_ptr) \
         (heap)->lock_fnc_ptr(heap); \
     } while (0)
   #define END_CRITICAL_SECTION(heap) \
     do { \
       if ((heap)->free_fnc_ptr) \
         (heap)->free_fnc_ptr(heap); \
     } while (0)

static void mem_init_block_header(mem_block_header_type *, unsigned long, mem_heap_type *hep_ptr);

#ifdef FEATURE_MEM_DEBUG
#ifndef MEM_HEAP_CALLER_ADDRESS_LEVEL
#define MEM_HEAP_CALLER_ADDRESS_LEVEL 1
#endif
#define MEM_HEAP_CALLER_ADDRESS(level) ((void *)__return_address())
#endif

#ifdef NT_TU_HEAP_STATS

UBaseType_t ulTaskId = 0;	//Used for updating/retrieving the Task ID into heap structure
static int curr_total_usage = 0;	//current usage from total heap
int max_total_usage = 0;		//Maximum usage from total heap
int CurrTotalUsagePerStep = 0;
int MaxTotalUsagePerStep = 0;
UBaseType_t SchedulerState = NULL;
#endif


#define MEMHEAP_ASSERT( xx_exp ) \
      if( !(xx_exp) ) \
      { \
		assert(0);\
      }

/* Lock function for Memheap.
*/
static void
mem_heap_enter_crit_sect( void * heap_ptr)
{
  if(NULL != heap_ptr)
  {
//    platform_mutex_lock((qurt_mutex_t *)(&((mem_heap_type *)heap_ptr)->memheap_crit_sect));
  }
} /* END mem_heap_enter_crit_sect */

/* Matching free function for mem_heap_lock_mutex().
*/
static void
mem_heap_leave_crit_sect( void * heap_ptr)
{
  if(NULL != heap_ptr)
  {
//     platform_mutex_unlock((qurt_mutex_t *)(&((mem_heap_type *)heap_ptr)->memheap_crit_sect));
  }
} /* END mem_heap_leave_crit_sect */


static void memheap_copy_frd_offset_at_end(mem_block_header_type *mem_block, mem_heap_type *heap_ptr)
{
  frd_Offset_info *temp = NULL;
  // store the frd_offset at the last bytes in the free block
  temp = (frd_Offset_info *)((char*)mem_block + (mem_block->forw_offset - sizeof(frd_Offset_info)));
  if(mem_block->forw_offset != kMinChunkSize){
    temp->pad = magic_num[heap_ptr->magic_num_index];
  }
  temp->freeBlock_frdOff = mem_block->forw_offset;
}

/*===========================================================================
FUNCTION
memheap_clear_footer

DESCRIPTION
In a used block the footer should be cleared to avoid incorrectly defragmenting the heap memory.

===========================================================================*/
static void memheap_clear_footer (mem_block_header_type *mem_block, mem_heap_type *heap_ptr)
{
     frd_Offset_info *temp = NULL;
     // clear the footer in the used block
     temp = (frd_Offset_info *)((char*)mem_block + (mem_block->forw_offset - sizeof(frd_Offset_info)));
     if(mem_block->forw_offset != kMinChunkSize){
       temp->pad = 0;
     }
     temp->freeBlock_frdOff = 0;
}

/*===========================================================================
FUNCTION
mem_heap_get_random_num

DESCRIPTION
Helper API to get a random number of the specified length.

===========================================================================*/
static void mem_heap_get_random_num(void*  random_ptr, int random_len)
{
#ifdef FEATURE_MEMHEAP2_USE_PRNG
 if(PRNGML_ERROR_NONE != PrngML_getdata(((uint8*)random_ptr), random_len))
  {
    MEMHEAP_ASSERT(0);
  }
#else
  if(random_len == 4)
  {
    uint32 *ran_num = (uint32 *)random_ptr;
    *ran_num = 0xabcddcba;
  }
  else if(random_len == 2)
  {
    uint16 *ran_num = (uint16 *)random_ptr;
    *ran_num = 0xabcd;
  }
#endif
}

/*===========================================================================
FUNCTION MEM_GET_NEXT_BLOCK

DESCRIPTION
  Return the next block header in the heap for the block following the
  given one.  If the given block is the last block in the heap, return
  the first block in the heap.  Never returns NULL.

  Returns a pointer the the memory block header of the block following the given
  block (or the first block of the heap if the given block was the last
  block of the heap).

===========================================================================*/
static mem_block_header_type *mem_get_next_block
(
   const mem_heap_type         *heap_ptr,
     /*  The heap the given block belongs to -- may NOT be NULL!
     */
   const mem_block_header_type *block_ptr
     /*  The block in the heap for which to return the following heap
         block
     */
)
{
   mem_block_header_type *nextBlkPtr = NULL;
   uint16 *pblk=NULL;
   MEMHEAP_ASSERT(block_ptr != NULL);
   MEMHEAP_ASSERT(heap_ptr != NULL); //this may be redundent since , it a static function re-visit this for possible optimisation
   MEMHEAP_ASSERT(block_ptr->forw_offset != 0);
   MEMHEAP_ASSERT(FRD_OFFSET_CHECK(block_ptr, heap_ptr));
   MEMHEAP_ASSERT(!((block_ptr->forw_offset)%kMinChunkSize));

   nextBlkPtr =  block_ptr->last_flag  ? heap_ptr->first_block
           : (mem_block_header_type *) ((char *) block_ptr + block_ptr->forw_offset);
   pblk = (uint16*)nextBlkPtr;
   if(nextBlkPtr->free_flag == kBlockFree){
      MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk));
   }
   else{
      MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_USED_HEADER(heap_ptr->magic_num_used, pblk));
   }
   return nextBlkPtr;
}/* END mem_get_next_block */





/*===========================================================================
FUNCTION MEM_INIT_HEAP

DESCRIPTION
  Initializes the heap_ptr object and sets up inMemoryChunk for use with the
  heap_ptr object.  inMemoryChunk may be aligned on any boundary.  Beginning
  bytes will be skipped until a paragraph boundary is reached.  Do NOT pass
  in NULL pointers.  infail_fnc_ptr may be NULL in which case no function will
  be called if nt_mem_malloc or mem_calloc is about to fail.  If infail_fnc_ptr
  is provided, it will be called once and then the allocation will be
  attempted again.  See description of my_allocator_failed_proc for details.
  There is no protection for initializing a heap more than once.  If a heap
  is re-initialized, all pointers previously allocated from the heap are
  immediately invalidated and their contents possibly destroyed.  If that's
  the desired behavior, a heap may be initialized more than once.
===========================================================================*/
/*lint -sem(mem_init_heap,1p,2p,2P>=3n) */
void mem_init_heap(
   mem_heap_type                 *heap_ptr,
      /* Statically allocated heap structure
      */
    void                          *heap_mem_ptr,
      /* Pointer to contiguous block of memory used for this heap
      */
   unsigned long                  heap_mem_size
      /* The size in bytes of the memory pointed to by heap_mem_ptr
      */
)
{
  char *memory_end_ptr;
    /* 1 beyond computed end of memory passed in to use as heap.
    */
  char *memory_start_ptr;
    /* The computed beginning of the memory passed in to use as heap.  This
       computed value guarantees the heap actually starts on a paragraph
       boundary.
    */
  unsigned long chunks;
    /* How many whole blocks of size kMinChunkSize fit in the area of
       memory starting at memory_start_ptr and ending at (memory_end_ptr-1)
    */
  uint16 * pblk = NULL;

  MEMHEAP_ASSERT(heap_ptr);


  MEMHEAP_ASSERT(magic_num_index < MAX_HEAP_INIT); /* support at the most 8 heaps*/

  if( (heap_ptr->magic_num) &&
      (heap_ptr->magic_num == magic_num[heap_ptr->magic_num_index])){
  	/* heap is already initialized so just return */
  	return;
  }

  memset(heap_ptr, 0, sizeof(mem_heap_type));

  MEMHEAP_ASSERT(heap_mem_ptr);
  MEMHEAP_ASSERT(heap_mem_size);
  MEMHEAP_ASSERT(heap_mem_size >= (2*kMinChunkSize-1));

  memory_start_ptr = (char *)heap_mem_ptr;
  memory_end_ptr   = memory_start_ptr + heap_mem_size;

//  heap_ptr->memheap_crit_sect = (void *)platform_force_mutex_init((void *)(heap_ptr->memheap_crit_sect_mem));


    /* by default it is critical section */
  heap_ptr->lock_fnc_ptr = mem_heap_enter_crit_sect;
  heap_ptr->free_fnc_ptr = mem_heap_leave_crit_sect;

  /* Advance to the nearest paragraph boundary. This while loop should work
  ** regardless of how many bits are required for address pointers near or
  ** far, etc.
  **
  ** Turn off lint "size incompatibility" warning because cast from pointer
  ** to unsigned long will lose bits, but we don't care because we're only
  ** interested in the least significant bits and we never cast back into a
  ** pointer, so the warning can be safely ignored
  */
  /*lint --e(507)*/

  while( (((unsigned long)memory_start_ptr) & 0x000FUL) )
  {
     ++memory_start_ptr;
  }

  chunks = (unsigned long) ((memory_end_ptr - memory_start_ptr) / kMinChunkSize);

  heap_ptr->first_block            = (mem_block_header_type *) memory_start_ptr;
  heap_ptr->next_block             = heap_ptr->first_block;

  mem_heap_get_random_num((&magic_num[magic_num_index_array[magic_num_index]]), 4);
  heap_ptr->magic_num = magic_num[magic_num_index_array[magic_num_index]];
  heap_ptr->magic_num_index = magic_num_index_array[magic_num_index];
  mem_heap_get_random_num(&(heap_ptr->magic_num_free), 2);
  mem_heap_get_random_num(&(heap_ptr->magic_num_used), 2);
  magic_num_index++;
  mem_init_block_header(heap_ptr->first_block, chunks * kMinChunkSize, heap_ptr);
  heap_ptr->first_block->last_flag = (char) kLastBlock;
  heap_ptr->total_blocks           = 1;
  heap_ptr->max_used               = 0;
  heap_ptr->max_request            = 0;
  heap_ptr->used_bytes             = 0;
  heap_ptr->total_bytes            = chunks * kMinChunkSize;
  pblk = (uint16*)(heap_ptr->first_block);
  ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);
  return;
} /* END mem_init_heap */


/*===========================================================================
FUNCTION MEM_DEINIT_HEAP

DESCRIPTION
  De-Initializes the heap_ptr object only if the heap is in reset state.
  User is responsible for freeing all the allocated pointers before  calling
  into this function.
===========================================================================*/
void mem_deinit_heap(
   mem_heap_type                 *heap_ptr
      /* Statically allocated heap structure
      */

)
{
/* Change request has been communicated to Gaurang P.
 *temporary workaround until official change gets promoted */

  // return the magic number
  magic_num_index--;
  if(magic_num_index < MAX_HEAP_INIT)
  {
	 magic_num_index_array[magic_num_index]=heap_ptr->magic_num_index;
  }
#if 1 //support OM transitions where clients are not forced to de-allocate their memory
  memset(heap_ptr, 0, sizeof(mem_heap_type));
#else
 /* De-initialize heap only if all the allocated blocks are freed */
 if(heap_ptr->used_bytes == 0)
 {
   //qurt_pimutex_destroy((qurt_mutex_t*)&(heap_ptr->memheap_crit_sect));
   memset(heap_ptr, 0, sizeof(mem_heap_type));
 }
 else
 {
	MEMHEAP_ASSERT(heap_ptr->used_bytes == 0);
 }
#endif
}
/*===========================================================================
FUNCTION MEM_INIT_BLOCK_HEADER

DESCRIPTION
  Initializes a memory block header to control a block of memory in the
  heap.  The header may still need to some of its fields adjusted after
  this call if it will be a used block or the last block in the heap.
===========================================================================*/
void mem_init_block_header(
   mem_block_header_type *block_ptr,
     /* Memory header block to be initialized
     */
   unsigned long          size,
     /* The size of the block of memory controlled by this
        memory header block INCLUDING the size of the
        header block itself
     */
     mem_heap_type *heap_ptr
)
{
  char *p_temp;

  (void)heap_ptr; //Unused variable

  MEMHEAP_ASSERT(block_ptr);
  p_temp = ((char*)block_ptr);
  memset(p_temp, 0 , sizeof(mem_block_header_type));
  block_ptr->free_flag   = (char) kBlockFree;
  block_ptr->forw_offset = size;

  return ;
} /* END mem_init_block_header */


/*===========================================================================
FUNCTION nt_mem_malloc

DESCRIPTION
  Allocates a block of size bytes from the heap.  If heap_ptr is NULL
  or size is 0, the NULL pointer will be silently returned.

  Returns a pointer to the newly allocated block, or NULL if the block
  could not be allocated.
===========================================================================*/
/*lint -sem(nt_mem_malloc,1p,2n>=0&&(@p==0||@P==2n)) */
void* nt_mem_malloc(
  mem_heap_type *heap_ptr,
     /* Heap from which to allocate
     */

  unsigned int         size
     /* Number of bytes to allocate
     */
)
{

  unsigned long chunks;
    /* the computed minimum size of the memory block in chunks needed
       to satisfy the request */

  unsigned long actualSize;
    /* the computed minimum size of the memory block in bytes needed
       to satisfy the request */

  unsigned char bonusBytes;
    /* the computed number of unused bytes at the end of the allocated
       memory block.  Will always be < kMinChunkSize */

  mem_block_header_type *freeBlock = NULL;
    /* the free block found of size >= actualSize */

  void *answer = NULL;
    /* the address of memory to be returned to the caller */

  uint16 * pblk = NULL;
  uint32 blockHeaderSize=sizeof(mem_block_header_type);
#ifdef NT_TU_HEAP_STATS
  uint32 malloc_size;
#endif

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  if(heap_ptr->first_block->header_guard == 0)
  {
	  assert(0);
  }

  MEMHEAP_ASSERT(heap_ptr != NULL);



  if (!size) return NULL;



  /* quick check if requested size of memory is available */
  if( (unsigned long) size > heap_ptr->total_bytes ) return NULL;

  /* chunks overflow check : check max memory that can be malloc'd at a time */
  if( (0xFFFFFFFF - ( kMinChunkSize + sizeof(mem_block_header_type)) )
  	    < ((unsigned long) size)) return NULL;




  chunks = ((unsigned long) size + blockHeaderSize
            + kMinChunkSize - 1) / kMinChunkSize;
  actualSize = chunks * kMinChunkSize;
  bonusBytes = (unsigned char)
                (actualSize - size - blockHeaderSize);
  BEGIN_CRITICAL_SECTION(heap_ptr);

  MEMHEAP_ASSERT(heap_ptr->magic_num == magic_num[heap_ptr->magic_num_index]);
  freeBlock = mem_find_free_block(heap_ptr, actualSize);

  if (freeBlock)
  {
      /* split the block (if necessary) and return the new block */

      MEMHEAP_ASSERT(freeBlock->forw_offset > 0);
      // frd offset and actual size are chunk aligned
      if (freeBlock->forw_offset > actualSize && ((freeBlock->forw_offset - actualSize) >= blockHeaderSize))
      {
        /* must split into two free blocks */


        mem_block_header_type *newBlock = (mem_block_header_type *)
                                          ((char *) freeBlock + actualSize);
        mem_init_block_header(newBlock, freeBlock->forw_offset - actualSize, heap_ptr);
        newBlock->last_flag = freeBlock->last_flag;
        freeBlock->forw_offset = actualSize;
        freeBlock->last_flag = 0;


        ++heap_ptr->total_blocks;
         pblk = (uint16*)newBlock;
        ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);
      }


      /* mark the block as used and return it */

      freeBlock->free_flag = kBlockUsed;
      freeBlock->extra = bonusBytes;


      /*  set up next block to search for
          next allocation request */
      heap_ptr->next_block = mem_get_next_block(heap_ptr, freeBlock);


      heap_ptr->used_bytes += freeBlock->forw_offset;
      MEMHEAP_ASSERT(heap_ptr->total_bytes >= heap_ptr->used_bytes);

      if (heap_ptr->used_bytes > heap_ptr->max_used) {
        heap_ptr->max_used = heap_ptr->used_bytes;
      }
      if (size > heap_ptr->max_request) {
        heap_ptr->max_request = size;
      }

      pblk = (uint16*)freeBlock;
      ADD_GUARD_BYTES_TO_USED_HEADER(heap_ptr->magic_num_used, pblk);
      answer = (char *) freeBlock + blockHeaderSize;
      memheap_clear_footer(freeBlock, heap_ptr);

#ifdef NT_TU_HEAP_STATS
     malloc_size = freeBlock->forw_offset;
     curr_total_usage += malloc_size; //Updating total heap usage

     if(max_total_usage < curr_total_usage)  //Updating highest heap usage
     {
      max_total_usage = curr_total_usage;
     }

     CurrTotalUsagePerStep += malloc_size;

     if(MaxTotalUsagePerStep < CurrTotalUsagePerStep)
     {
      MaxTotalUsagePerStep = CurrTotalUsagePerStep;
     }

		/* Get the Task ID and update in the heap structure before updating the heap utility table */
		ulTaskId = xTaskGetCurrentTaskId();
		SchedulerState = xTaskGetSchedulerState();
		/* Retrieving the Task Id from the heap structure to free the memory allocated for the respective task */
		if(SchedulerState == taskSCHEDULER_NOT_STARTED)
		{
			ulTaskId = 0;
			nt_table_update_malloc(malloc_size,SchedulerState,ulTaskId);
		}
		else
		{
			freeBlock->tid = ulTaskId;
			nt_table_update_malloc(malloc_size,SchedulerState,ulTaskId);
		}
#endif

  }

#ifdef FEATURE_MEM_DEBUG
  if(answer != NULL && freeBlock != NULL)
  {
    freeBlock->caller_ptr=MEM_HEAP_CALLER_ADDRESS(MEM_HEAP_CALLER_ADDRESS_LEVEL);
    if (platform_isOSMode())
    {
    freeBlock->tid = qurt_thread_get_id();
    }
  }
#endif

  END_CRITICAL_SECTION(heap_ptr);

  if(heap_ptr->first_block->header_guard == 0)
  {
	  assert(0);
  }

#if defined(CONFIG_HEAP_STATISTIC)
  if (answer != NULL)
  {
    uint32 i;
	uint8 *pmem = (uint8 *)answer;
	for (i = 0; i < (actualSize - blockHeaderSize); i += 4)
	{
	  pmem[i] = 0xEF;
	  pmem[i+1] = 0xBE;
	  pmem[i+2] = 0xAD;
	  pmem[i+3] = 0xDE;
	}
  }
#endif

  return answer;
} /* END nt_mem_malloc */


/*===========================================================================
FUNCTION nt_mem_free

DESCRIPTION
  Deallocates the ptr block of memory.  If ptr is NULL, heap_ptr is NULL or
  ptr is outside the range of memory managed by heap_ptr, then this function
  call does nothing (and is guaranteed to be harmless).  This function will
  ASSERT if it can detect an attempt to free an already freed block.  (This
  is not always reliable though, so it might not catch it.)
===========================================================================*/
/*lint -sem(nt_mem_free,1p) */
void nt_mem_free(
  mem_heap_type *heap_ptr,
     /* Heap in which to free memory
     */

  void          *ptr
     /* Memory to free
     */
)
{
  mem_block_header_type *theBlock;
    /* The computed address of the memory header block in the heap that
       controls the memory referenced by ptr */
  frd_Offset_info *temp;
  uint32          sizeBlockHeader=sizeof(mem_block_header_type);
  uint16 *pblk = NULL;
#ifdef NT_TU_HEAP_STATS
  uint32 free_size = 0;
#endif

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
  if(heap_ptr->first_block->header_guard == 0)
  {
	  assert(0);
  }

  MEMHEAP_ASSERT(heap_ptr != NULL);



  /*commented out the below assert since NULL free occurances are found and once
   corrsponding fixes are in its need to be uncomment*/
  MEMHEAP_ASSERT(heap_ptr->magic_num == magic_num[heap_ptr->magic_num_index]);



  /*commented out the below assert since NULL free occurances are found and once
   corrsponding fixes are in its need to be uncomment*/
  if(ptr == NULL)
  {
     //MEMHEAP_ERROR(" NULL ptr occurenaces in nt_mem_free()",0,0,0);
    //MEMHEAP_ASSERT(0);

    return ;
  }


  /* free the passed in block */

  MEMHEAP_ASSERT(heap_ptr->first_block);
  BEGIN_CRITICAL_SECTION(heap_ptr);


  theBlock = (mem_block_header_type *)
                          ((char *) ptr - sizeBlockHeader);


  //check for block alignment to 16
  MEMHEAP_ASSERT((((uint32)theBlock)%kMinChunkSize) == 0);

  /* boundry check for the ptr passed to free */
  MEMHEAP_ASSERT(BOUNDARY_CHECK(theBlock, heap_ptr));




  /* Try to detect corruption. */


  MEMHEAP_ASSERT(!theBlock->free_flag);  /* Attempt to detect multiple
                                            frees of same block */
  /* Make sure forw_offset is reasonable */
  MEMHEAP_ASSERT(theBlock->forw_offset >= sizeBlockHeader);
  /* Make sure extra is reasonable */
  MEMHEAP_ASSERT(theBlock->extra < kMinChunkSize);

  /* Make sure forw_offset is not spiling over the heap boundry */
  MEMHEAP_ASSERT(FRD_OFFSET_CHECK(theBlock, heap_ptr));

 //check for heap canary
  pblk = (uint16*)(theBlock);
  MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_USED_HEADER(heap_ptr->magic_num_used, pblk));



  if (!theBlock->free_flag) /* Be intelligent about not munging the heap if
                              a multiple free of the same block is detected */
  {


    MEMHEAP_ASSERT((theBlock->forw_offset - sizeBlockHeader
                                  - theBlock->extra) <= heap_ptr->used_bytes);
    heap_ptr->used_bytes -= theBlock->forw_offset;

#ifdef NT_TU_HEAP_STATS
    free_size = theBlock->forw_offset;
    ulTaskId = theBlock->tid;
#endif

    MEMHEAP_ASSERT(heap_ptr->total_bytes >= heap_ptr->used_bytes);

    theBlock->free_flag = (char) kBlockFree;
	 /* try to do defragmentation if possible*/
    /*check if it can concatenate the previous block of the theBlock*/
	if(theBlock != heap_ptr->first_block)
    {
      mem_block_header_type *preFreeBlock;
      int validBlock = FALSE;
      temp = (frd_Offset_info*)((char*)theBlock - sizeof(frd_Offset_info));

      if(!((temp->freeBlock_frdOff)%kMinChunkSize)){

        if(MEMHEAP_MIN_BLOCK_SIZE == temp->freeBlock_frdOff){
           validBlock = TRUE;
        }
        else if( temp->pad == magic_num[heap_ptr->magic_num_index]
               &&FOOTER_FRD_OFFSET_CHECK(theBlock, heap_ptr, temp->freeBlock_frdOff))
        {
          validBlock = TRUE;
        }

        if(validBlock){
           preFreeBlock =  (mem_block_header_type *)((char*)theBlock - temp->freeBlock_frdOff);
           pblk = (uint16*)preFreeBlock;
           if(preFreeBlock >= heap_ptr->first_block){
              if((preFreeBlock->free_flag  == kBlockFree)
                 &&(preFreeBlock->forw_offset == temp->freeBlock_frdOff)
                 && BOUNDARY_CHECK(preFreeBlock, heap_ptr)
                 && FRD_OFFSET_CHECK(pblk, heap_ptr)
                 &&(!INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk))) /*we dont want to ASSERT if integrity check fails , just dont concatenate the block*/
              {

                 /* preFreeBlock is free and now we can join it with the new
                   free block theBlock so remove it from the list if apply*/

                  /* Set preFreeBlock's end bytes to 0 as the frd_offset has changed */
                 temp->freeBlock_frdOff = 0;
                 temp->pad = 0;
                 --heap_ptr->total_blocks;
                 preFreeBlock->forw_offset=preFreeBlock->forw_offset+theBlock->forw_offset;
                 preFreeBlock->last_flag = theBlock->last_flag;


                 /* now set the theBlock's header info. to NULL */
                 {
                   uint32 *temp = (uint32*)theBlock; /*did it like this for optimization purpose*/
                   temp[0] = 0;
                   temp[1] = 0;
                 }
                 theBlock = preFreeBlock;
              }
           }
        }/*if(validBlock)*/

      }/*if(!((temp->freeBlock_frdOff)%kMinChunkSize))*/

    }/*if(theBlock != heap_ptr->first_block)*/

    /*check if it can concatenate the next block of the theBlock*/
    if(theBlock->last_flag != kLastBlock){
      char *end_address=(char*)(heap_ptr->first_block) + heap_ptr->total_bytes;
      if(((char*)theBlock + theBlock->forw_offset) < end_address)
      {
         mem_block_header_type *nextFreeBlock =
           (mem_block_header_type *)((char *)theBlock + theBlock->forw_offset);
         if(nextFreeBlock->free_flag == kBlockFree)
         {
           pblk = (uint16*)nextFreeBlock;
           MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk));

           theBlock->forw_offset += nextFreeBlock->forw_offset;
           theBlock->last_flag = nextFreeBlock->last_flag;

           /* now set the nextFreeBlock's header info. to NULL */
           {
             uint32 *temp = (uint32*)nextFreeBlock; /*did it like this for optimization purpose*/
             temp[0] = 0;
             temp[1] = 0;
           }
           --heap_ptr->total_blocks;
         }else{
            /* Assert if the next block is used but fails the integrity check
               It is highly likely that the owner of the current block
               corrupted the next block and we should try to catch early. */
            pblk = (uint16*)nextFreeBlock;
            MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_USED_HEADER(heap_ptr->magic_num_used, pblk));
         }
      }
    }

    /* now backup the next pointer if applicable */
    //next_block = mem_get_next_block(heap_ptr, theBlock);

#ifdef NT_TU_HEAP_STATS
        curr_total_usage -= free_size; //Updating total heap usage

        CurrTotalUsagePerStep -= free_size;

        SchedulerState = xTaskGetSchedulerState();
 		/* Retrieving the Task Id from the heap structure to free the memory allocated for the respective task */
 		if(SchedulerState == taskSCHEDULER_NOT_STARTED)
 		{
 			ulTaskId = 0;
 			nt_table_update_free(free_size,SchedulerState,ulTaskId);
 		}
 		else
 		{
 			nt_table_update_free(free_size,SchedulerState,ulTaskId);
 		}
#endif

 	if (theBlock < heap_ptr->next_block) {
      /* Backup now to lessen possible fragmentation */
      heap_ptr->next_block = theBlock;
    }

    pblk = (uint16*)theBlock;
    ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);
    memheap_copy_frd_offset_at_end(theBlock, heap_ptr);

    /* reset heap to initial state if everything is now freed */
    if (!heap_ptr->used_bytes) {

      /* reset heap now, but retain statistics */
      heap_ptr->next_block = heap_ptr->first_block;
      mem_init_block_header(heap_ptr->first_block, heap_ptr->total_bytes, heap_ptr);
      heap_ptr->first_block->last_flag = (char) kLastBlock;
      heap_ptr->total_blocks = 1;
	  	  pblk = (uint16*)(heap_ptr->first_block);
      ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);
    }
	}

     END_CRITICAL_SECTION(heap_ptr);

     if(heap_ptr->first_block->header_guard == 0)
     {
   	  assert(0);
     }
     return;
} /* END nt_mem_free */

/*===========================================================================
FUNCTION MEM_FIND_FREE_BLOCK

DESCRIPTION
  Find a free block of at least inSizeNeeded total bytes.  Collapse
  adjacent free blocks along the way.

  Returns a pointer to a memory block header describing a free block
  of at least inSizeNeeded total bytes.  Returns NULL if no such free
  block exists or can be created by collapsing adjacent free blocks.
===========================================================================*/
static mem_block_header_type *mem_find_free_block(
   mem_heap_type *heap_ptr,
     /*  The heap to search for a free block
     */
   unsigned long  size_needed
     /*  The minimum size in bytes of the block needed (this size
         INCLUDES the size of the memory block header itself)
     */
)
{
  long searchBlocks;
    /* The maximum number of blocks to search.  After searching this
       many, we've been through the heap once and the allocation fails
       if we couldn't find/create a satisfactory free block */

  mem_block_header_type *followingBlock;
    /* a loop variable used to walk through the blocks of the heap */
  uint16 *pblk = NULL;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  MEMHEAP_ASSERT(heap_ptr);
  MEMHEAP_ASSERT(heap_ptr->first_block);
  /* sanity check for the  heap_ptr->next_block  */
  if((heap_ptr->next_block)->free_flag == kBlockFree){
      pblk = (uint16*)(heap_ptr->next_block);
      MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk));
   }
   else{
      pblk = (uint16*)(heap_ptr->next_block);
      MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_USED_HEADER(heap_ptr->magic_num_used, pblk));
   }

  searchBlocks = (long) heap_ptr->total_blocks;

  followingBlock = mem_get_next_block(heap_ptr, heap_ptr->next_block);

  for (; searchBlocks > 0; --searchBlocks, heap_ptr->next_block=followingBlock,
                  followingBlock=mem_get_next_block(heap_ptr, heap_ptr->next_block))
  {
    MEMHEAP_ASSERT(heap_ptr->next_block->forw_offset > 0);
    MEMHEAP_ASSERT(followingBlock->forw_offset > 0);

    if (heap_ptr->next_block->free_flag)
    {

      if (heap_ptr->next_block->forw_offset >= size_needed) {
        return heap_ptr->next_block;
      }
    }
  }
  return 0;       /* didn't find anything */
} /* END mem_find_free_block */

/*===========================================================================
FUNCTION mem_heap_init_lock_free_fns

DESCRIPTION
     init the lock and free functions of the heap , its called from amssheap.c
===========================================================================*/
void mem_heap_init_lock_free_fns(mem_heap_type  *heap_ptr)
{

   MEMHEAP_ASSERT (heap_ptr != NULL);


   if(heap_ptr != NULL){
      heap_ptr->lock_fnc_ptr = mem_heap_enter_crit_sect;
      heap_ptr->free_fnc_ptr = mem_heap_leave_crit_sect;
  }

}


/*===========================================================================

FUNCTION MALLOC_INIT

DESCRIPTION
  Initializes function pointers for NT_malloc and NT_free functionality

DEPENDENCIES
  None

RETURN VALUE
 boolean - TRUE if NT_malloc and NT_free function pointers were
           correctly initialized.
           FALSE if NT_malloc and NT_free function pointers are NULL

SIDE EFFECTS
  Will be called on the first NT_malloc call

ARGUMENTS
  None

===========================================================================*/
unsigned char malloc_init
(
  heap_config_type* heap_cfg
)
{
  boolean status = TRUE;
  heap_cfg->heap_size = (HEAP_END_ADDR) - (uint32_t)&_ln_RAM_addr_heap_start__;	//Calculating Heap Size

  if(NULL == heap_cfg)
  {
    return FALSE;
  }

  //do not reinit again the heap until the deinit is called
  if(FALSE == is_amss_heap_initialized && heap_cfg->heap_start_address)
  {
    mem_init_heap(&amss_mem_heap,heap_cfg->heap_start_address,heap_cfg->heap_size);
    is_amss_heap_initialized = TRUE;
    status = TRUE;
  }

  return status;
}

#ifdef DEBUG_MEM_LEAK
void *pvPortMallocWrapper(size_t size, const char *caller) {
    void *ptr = pvPortMalloc(size);
    if (ptr != NULL) {
      char pcWriteBuffer[200];
      snprintf((char *)pcWriteBuffer,sizeof(pcWriteBuffer)-strlen(pcWriteBuffer),"Allocated %u bytes from %s\r\n",size,caller);
		  nt_dbg_print(pcWriteBuffer);
    }
    return ptr;
}

void pvPortFreeWrapper(void *ptr , const char *caller) {
    extern mem_heap_type amss_mem_heap;
    mem_heap_type *amss_mem_heap_ptr = &amss_mem_heap;
    unsigned long used = amss_mem_heap_ptr->used_bytes;
    vPortFree(ptr);
    unsigned long delta = used - amss_mem_heap_ptr->used_bytes;
    if (ptr != NULL) {
      char pcWriteBuffer[200];
      snprintf((char *)pcWriteBuffer,sizeof(pcWriteBuffer)-strlen(pcWriteBuffer),"Free %u bytes from %s\r\n",delta,caller);
		  nt_dbg_print(pcWriteBuffer);
    }
}
#endif
/*===========================================================================

FUNCTION NT_malloc

DESCRIPTION
  Allocates from the either RTOS heap in FOM mode and internal heap in
  SOM mode.

DEPENDENCIES
  None

RETURN VALUE
  A a pointer to the newly allocated block, or NULL if the block
  could not be allocated.

SIDE EFFECTS
  Replaces C Std Library implementation.

ARGUMENTS
  size  - Size of allocation request.

===========================================================================*/
#ifndef CONFIG_HEAP_STATISTIC
void *pvPortMalloc( size_t size )
#else
unsigned int heap_statistics_index;
mem_heap_statistics_type heap_statistics[NT_HEAP_RCD_CNT];

void *__pvPortMalloc( size_t size )
#endif
{
    void *ptr;
    vTaskSuspendAll();
    if(is_amss_heap_initialized == FALSE){
    malloc_init(&heap_config);
    }
    if(!is_amss_heap_initialized)
    {
        return NULL;
    }
    ptr = nt_mem_malloc(&amss_mem_heap,size);
    ( void ) xTaskResumeAll();
    return ptr;
}

/*===========================================================================

FUNCTION NT_free

DESCRIPTION
  Deallocates a block of memory and returns it to the heap.

DEPENDENCIES
  None

RETURN VALUE
  None

SIDE EFFECTS
  Replaces C Std Library implementation.

ARGUMENTS
  ptr - A pointer to the memory block that needs to be deallocated.

===========================================================================*/

#ifndef CONFIG_HEAP_STATISTIC
void vPortFree(  void *ptr )
#else
unsigned int heap_statistics_free_index;
const char *heap_statistics_free[NT_HEAP_RCD_CNT];

void __vPortFree(  void *ptr )
#endif
{
	vTaskSuspendAll();
	nt_mem_free(&amss_mem_heap,ptr);
	( void ) xTaskResumeAll();
    return;
}

unsigned long mem_get_free_size(void)
{
  long searchBlocks;
    /* The maximum number of blocks to search.  After searching this
       many, we've been through the heap once and the allocation fails
       if we couldn't find/create a satisfactory free block */

  mem_block_header_type *followingBlock;
    /* a loop variable used to walk through the blocks of the heap */
  uint16 *pblk = NULL;
  mem_heap_type *heap_ptr = &amss_mem_heap;
  unsigned long free_size = 0;

  MEMHEAP_ASSERT(heap_ptr->first_block);
  /* sanity check for the  heap_ptr->next_block  */
  if((heap_ptr->next_block)->free_flag == kBlockFree){
      pblk = (uint16*)(heap_ptr->next_block);
      MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk));
   }
   else{
      pblk = (uint16*)(heap_ptr->next_block);
      MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_USED_HEADER(heap_ptr->magic_num_used, pblk));
   }

  searchBlocks = (long) heap_ptr->total_blocks;

  followingBlock = mem_get_next_block(heap_ptr, heap_ptr->next_block);

  for (; searchBlocks > 0; --searchBlocks, heap_ptr->next_block=followingBlock,
                  followingBlock=mem_get_next_block(heap_ptr, heap_ptr->next_block))
  {
    MEMHEAP_ASSERT(heap_ptr->next_block->forw_offset > 0);
    MEMHEAP_ASSERT(followingBlock->forw_offset > 0);

    if (heap_ptr->next_block->free_flag)
    {
      free_size += heap_ptr->next_block->forw_offset;
    }
  }
  return free_size;
} /* END mem_get_free_size */

#endif
