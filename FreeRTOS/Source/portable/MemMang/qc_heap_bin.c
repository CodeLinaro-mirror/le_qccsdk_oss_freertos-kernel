/*
 *Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =========================================================================

DESCRIPTION
  Implementation of a simple sub-allocator to manage memory allocations
  and deallocations using a Next Fit strategy.

============================================================================ */

/* =========================================================================

                             Edit History

$PVCSPath: O:/src/asw/COMMON/vcs/memheap.c_v   1.2   22 Mar 2002 16:54:42   rajeevg  $
$Header: //components/rel/core.ioe/1.0/services/utils/src/fom/umemheap_lite.c#7 $ $DateTime: 2020/03/03 02:35:49 $ $Author: pwbldsvc $

when       who     what, where, why
--------   ---     ---------------------------------------------------------
04/4/14    mg       Initial Release

============================================================================ */


/* ------------------------------------------------------------------------
** Includes
** ------------------------------------------------------------------------ */
#include "qc_heap_bin.h"
#include "assert.h"
#if (NT_FN_QC_HEAP == 2)

/* ------------------------------------------------------------------------
** Defines
** ------------------------------------------------------------------------ */
#define MEMHEAP2_BINSIZE0         (0x00000010)
#define MEMHEAP2_BINSIZE1         (0x00000020)
#define MEMHEAP2_BINSIZE2         (0x00000040)
#define MEMHEAP2_BINSIZE3         (0x00000060)
#define MEMHEAP2_BINSIZE4         (0x00000080)
#define MEMHEAP2_BINSIZE5         (0x000000A0)
#define MEMHEAP2_BINSIZE6         (0x000000C0)
#define MEMHEAP2_BINSIZE7         (0x00000100)
#define MEMHEAP2_BINSIZE8         (0x00000180)
#define MEMHEAP2_BINSIZE9         (0x00000200)
#define MEMHEAP2_BINSIZE10        (0x00000280)
#define MEMHEAP2_BINSIZE11        (0x00000300)
#define MEMHEAP2_BINSIZE12        (0x00000400)
#define MEMHEAP2_BINSIZE13        (0x00000800)
#define MEMHEAP2_BINSIZE14        (0x00000C00)
#define MEMHEAP2_BINSIZE15        (0xFFFFFFFF)


#define NEXT_BLOCK_SIZE(temp) ((mem_block_header_type*)(temp->nextPtr))->forw_offset

#define OVERFLOW_CHECK(elt_count, elt_size) (!(elt_count >= (1U<<10) || elt_size >= (1U<<22)) || ((((uint64)elt_count * (uint64)elt_size) >> 32) == 0))

#define BOUNDARY_CHECK(theBlock, heap_ptr) ((theBlock >= ((mem_heap_type*)heap_ptr)->first_block) && (((char*)(theBlock)) < ((char*)(((mem_heap_type*)heap_ptr)->first_block) + ((mem_heap_type*)(heap_ptr))->total_bytes)))

#define FRD_OFFSET_CHECK(block, heap_ptr) ((((mem_block_header_type *)block)->forw_offset + (char *)block) > (char *)block)\
  &&((((mem_block_header_type *)block)->forw_offset + (char *)block) <= ((((char*)((mem_heap_type*)heap_ptr)->first_block) + ((mem_heap_type*)heap_ptr)->total_bytes)))

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

#ifdef NT_TU_HEAP_STATS
uint32 ulTaskId = 0; //Used for updating/retrieving the Task ID into heap structure
static int curr_total_usage = 0; //current usage from total heap
 int max_total_usage = 0; //Maximum usage from total heap
 int CurrTotalUsagePerStep = 0;
 int MaxTotalUsagePerStep = 0;
UBaseType_t SchedulerState = NULL;
#endif //NT_TU_HEAP_STATS

mem_heap_type amss_mem_heap;
static boolean is_amss_heap_initialized = FALSE;
uint32 magic_num[];
static boolean isFOM = FALSE;


/* Beginning address of the heap is provided by the linker */
static uint8_t *heap __attribute__((section(".heap"))) = &_ln_RAM_addr_heap_start__;
heap_config_type heap_config = {
  .heap_type = 0,
  .heap_start_address = &heap,
  .heap_size = 0 /* heap size determined at the heap initialization time */
};
#define MAX_HEAP_INIT 3
uint32 umagic_num[MAX_HEAP_INIT] = {(uint32)-1,(uint32)-1,(uint32)-1};

#define MIN_HEAP_SIZE_FOR_BINS 0x8000
uint16 umagic_num_index_array[MAX_HEAP_INIT] = {0,1,2};
uint16 umagic_num_index = 0;



static mem_block_header_type *mem_find_free_block(
   mem_heap_type *heap_ptr,
     /*  The heap to search for a free block
     */
   uint32  size_needed
     /*  The minimum size in bytes of the block needed (this size
         INCLUDES the size of the memory block header itself)
     */
);

static void mem_heap_get_random_num(void*  random_ptr, int random_len);


typedef struct frd_Offset_info_type{
  unsigned long pad;
  unsigned long freeBlock_frdOff;
}frd_Offset_info;

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

static void mem_init_block_header(mem_block_header_type *, uint32, mem_heap_type *hep_ptr);

#ifdef FEATURE_MEM_DEBUG
#ifndef MEM_HEAP_CALLER_ADDRESS_LEVEL
#define MEM_HEAP_CALLER_ADDRESS_LEVEL 1
#endif

#define  MEM_HEAP_CALLER_ADDRESS(level) ((void *)__return_address())

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
    vTaskSuspendAll();
} /* END mem_heap_enter_crit_sect */

/* Matching free function for mem_heap_lock_mutex().
*/
static void
mem_heap_leave_crit_sect( void * heap_ptr)
{
    xTaskResumeAll();
} /* END mem_heap_leave_crit_sect */




/*===========================================================================
FUNCTION memheap_copy_frd_offset_at_end

DESCRIPTION
      This function will copy the forward offset of the memory block at the end
      of the block(which will be used later while doing defragmentation)
===========================================================================*/

static void memheap_copy_frd_offset_at_end(mem_block_header_type *mem_block, mem_heap_type *heap_ptr)
{
   frd_Offset_info *temp = NULL;
   // store the frd_offset at the last bytes in the free block
   temp = (frd_Offset_info *)((char*)mem_block + (mem_block->forw_offset - sizeof(frd_Offset_info)));
   temp->pad = heap_ptr->magic_num;
   temp->freeBlock_frdOff = mem_block->forw_offset;
}

/*===========================================================================
FUNCTION mem_heap_get_random_num
DESCRIPTION

===========================================================================*/
static void mem_heap_get_random_num(void*  random_ptr, int random_len)
{
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
}

/*===========================================================================

FUNCTION MALLOC_INIT

DESCRIPTION
  Initializes function pointers for malloc and free functionality

DEPENDENCIES
  None

RETURN VALUE
 boolean - TRUE if malloc and free function pointers were
           correctly initialized.
           FALSE if malloc and free function pointers are NULL

SIDE EFFECTS
  Will be called on the first malloc call

ARGUMENTS
  None

===========================================================================*/
boolean malloc_init
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
    is_amss_heap_initialized = TRUE;
        umem_init_heap(&amss_mem_heap,heap_cfg->heap_start_address,heap_cfg->heap_size);
    status = TRUE;
  }

  return status;
}


/*===========================================================================*/
/*!
    @brief
    Determines a bin number for the given value.

    @detail
    This function is effectivly an opened up binary search algorithm hard
    coded for 32 values, with the exception that it looks for the
    closest match without going over instead of the exact match.  This
    function is large but extremely fast, speed is much more important here
    than size.
    With the given value, it finds a bin number that this value
    should fall into. It does that by:
    (1) compare the value with the BIN in the center, therefore BIN15
        out of BIN0 to BIN31,
    (2) if the value is less than (or equal to) BIN15, it compares the value
        with the center bin with lower half of the bins, therefore, BIN7
        out of BIN0 to BIN15.
    (3) if the value is greater than BIN15 in the step (1), it compares
        the value with the center bin with higher half of the bins,
        therefore, BIN23 outof BIN16 to BIN31.
    (4) this process of binary search continues until it determines the
        bin number.

    @return
    bin number (0 - 31)
*/
/*=========================================================================*/
static uint32 memheap_find_bin
(
  uint32 value   /*!< value to be determined for bin assignment */
)
{
  /*! @brief bin number to return
  */
  uint32 bin;

  /*-----------------------------------------------------------------------*/

    if (value <= MEMHEAP2_BINSIZE7)
    {
      if (value <= MEMHEAP2_BINSIZE3)
      {
        if (value <= MEMHEAP2_BINSIZE1)
        {
          if (value <= MEMHEAP2_BINSIZE0)
          {
            bin = 0;
          }
          else /* value > MEMHEAP2_BINSIZE0 */
          {
            bin = 1;
          }
        }
        else   /* value > MEMHEAP2_BINSIZE1 */
        {
          if (value <= MEMHEAP2_BINSIZE2)
          {
            bin = 2;
          }
          else /* value > MEMHEAP2_BINSIZE2 */
          {
            bin = 3;
          }
        }
      }
      else     /* value > MEMHEAP2_BINSIZE3 */
      {
        if (value <= MEMHEAP2_BINSIZE5)
        {
          if (value <= MEMHEAP2_BINSIZE4)
          {
            bin = 4;
          }
          else /* value > MEMHEAP2_BINSIZE4 */
          {
            bin = 5;
          }
        }
        else   /* value > MEMHEAP2_BINSIZE5 */
        {
          if (value <= MEMHEAP2_BINSIZE6)
          {
            bin = 6;
          }
          else /* value > MEMHEAP2_BINSIZE6 */
          {
            bin = 7;
          }
        }
      }
    }
    else       /* value > MEMHEAP2_BINSIZE7 */
    {
      if (value <= MEMHEAP2_BINSIZE11)
      {
        if (value <= MEMHEAP2_BINSIZE9)
        {
          if (value <= MEMHEAP2_BINSIZE8)
          {
            bin = 8;
          }
          else /* value > MEMHEAP2_BINSIZE8 */
          {
            bin = 9;
          }
        }
        else   /* value > MEMHEAP2_BINSIZE9 */
        {
          if (value <= MEMHEAP2_BINSIZE10)
          {
            bin = 10;
          }
          else /* value > MEMHEAP2_BINSIZE10 */
          {
            bin = 11;
          }
        }
      }
      else     /* value > MEMHEAP2_BINSIZE11 */
      {
        if (value <= MEMHEAP2_BINSIZE13)
        {
          if (value <= MEMHEAP2_BINSIZE12)
          {
            bin = 12;
          }
          else /* value > MEMHEAP2_BINSIZE12 */
          {
            bin = 13;
          }
        }
        else   /* value > MEMHEAP2_BINSIZE13 */
        {
          if (value <= MEMHEAP2_BINSIZE14)
          {
            bin = 14;
          }
          else /* value > MEMHEAP2_BINSIZE14 */
          {
            bin = 15;
          }
        }
      }
    }

  return bin;
} /* memheap_find_bin() */


/*===========================================================================
FUNCTION bin_active

DESCRIPTION
   this function will update the binToUseForMalloc of each applicable BIN of the heap,
   whenever new bin is active

===========================================================================*/
static void bin_active(
  mem_heap_type             *heap_ptr,
  uint32             binNo
)
{
  int i = binNo;
  /*do the first iteration for the newly active BIN as it has freeBlock (firstNode is not NULL)*/
  heap_ptr->BIN[i].binToUseForMalloc = binNo;
  i--;
  while( i >= 0)
  {
    if( !(heap_ptr->BIN[i].firstNode) )
    {
      heap_ptr->BIN[i].binToUseForMalloc = binNo;
    }
    else{
      break;
    }
    i--;
  }
}

/*===========================================================================
FUNCTION bin_deactive

DESCRIPTION
   this function will update the binToUseForMalloc of each applicable BIN of the heap,
   whenever new bin is deactivate , means there is no free block that the bin holds
   and hence we cannot use the bin to allocate memory.

===========================================================================*/
static void bin_deactive(
  mem_heap_type             *heap_ptr,
  uint32             binNo
)
{
  int i = binNo;
  if((binNo+1) < MEMHEAP2_BIN_COUNT)
  {
    while((i >= 0)&&(heap_ptr->BIN[i].binToUseForMalloc == binNo)){
      heap_ptr->BIN[i].binToUseForMalloc = heap_ptr->BIN[binNo+1].binToUseForMalloc;
      i--;
    }
  }
}

/*===========================================================================
FUNCTION init_binToUseForMalloc

DESCRIPTION
   this function will initialize all of the BINs in a heap to the default bin no.
   with the highest bin available in the heap at the time of mem_init_heap.
   So initially for malloc this is the bin its going to use.

===========================================================================*/
static void init_binToUseForMalloc(
  mem_heap_type             *heap_ptr,
  mem_block_header_type     *mem_block
)
{
  uint32 bin = memheap_find_bin(mem_block->forw_offset);
  int i;
  for( i = bin; i >= 0 ; i-- )
  {
     heap_ptr->BIN[i].binToUseForMalloc = bin;
  }
}

/*===========================================================================
FUNCTION memheap_addNodeToBin

DESCRIPTION:
     This function adds the free memory block into the apropriate bin

===========================================================================*/
static int memheap_addNodeToBin(mem_heap_type *heap_ptr, void *freeBlock)
{

  uint32  binNo = memheap_find_bin(((mem_block_header_type*)freeBlock)->forw_offset);
  uint16 * pblk = NULL;

  binStruct *binToUpdate = &heap_ptr->BIN[binNo];

  if(binToUpdate->firstNode){
    freeBlockList tempFreeBlock = {{0},0};
    freeBlockList *temp;

    tempFreeBlock.nextPtr = binToUpdate->firstNode;
    temp = &tempFreeBlock;

    // search the linked list for the right location
    while((temp->nextPtr != NULL)
       && (NEXT_BLOCK_SIZE(temp) < ((mem_block_header_type*)freeBlock)->forw_offset)){
      /*sanity check for the free block in the list*/
        MEMHEAP_ASSERT(BOUNDARY_CHECK((mem_block_header_type*)(temp->nextPtr), heap_ptr));
        temp = temp->nextPtr;
    }

    if(&tempFreeBlock != temp)
    {
      pblk =  (uint16*)temp;
      MEMHEAP_ASSERT(!(INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk)));
    }

    if(temp->nextPtr == binToUpdate->firstNode){
     //its the firstNode in the BIN to be updated
      binToUpdate->firstNode = (freeBlockList *)freeBlock;
    }
    if(!temp->nextPtr){
       // update the lastNode
       binToUpdate->lastNode = (freeBlockList *)freeBlock;
    }

    ((freeBlockList *)freeBlock)->nextPtr = temp->nextPtr;
    temp->nextPtr = (freeBlockList *)freeBlock;
    /*update the header_guard of the temp  since the next has  changed*/
    if(&tempFreeBlock != temp){
      pblk = (uint16*) temp;
      ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);
    }
    binToUpdate->totalNodes = binToUpdate->totalNodes + 1;
    bin_stats(binNo,0);
    return NULL;
  }else{
    binToUpdate->firstNode = (freeBlockList *)freeBlock;
    binToUpdate->lastNode = (freeBlockList *)freeBlock;
    ((freeBlockList *)freeBlock)->nextPtr = NULL;
    binToUpdate->totalNodes = 1;
    bin_active(heap_ptr, binNo);
    bin_stats(binNo,0);
    return NULL;
  }

}

/*===========================================================================
FUNCTION findNRemoveFreeBlockInBINs

DESCRIPTION:
     This function find a free memory block of requested size in case if
     size_needed is not NULL else if mem_ptr is not NULL then will search for the
     memory block in the bins list ,remove it from the list and return it

===========================================================================*/
static mem_block_header_type * findNRemoveFreeBlockInBINs(
  mem_heap_type *heap_ptr,
  void *mem_ptr,
  size_t size_needed
)
{
  mem_block_header_type *answer = NULL;
  uint16 *pblk = NULL;


  if(size_needed){
    uint32 bin = memheap_find_bin(size_needed);
    binStruct *binToSearch;
    freeBlockList tempFreeBlock = {{0},0};
    freeBlockList *temp;

    bin = heap_ptr->BIN[bin].binToUseForMalloc;
    binToSearch = &heap_ptr->BIN[bin];

    if(binToSearch->firstNode == NULL)
    {
       return NULL;
    }

    pblk = (uint16 *)(&((binToSearch->lastNode)->freeBlock));
    MEMHEAP_ASSERT(!(INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk)));

    if(((binToSearch->lastNode)->freeBlock).forw_offset < size_needed){
      if(bin < MEMHEAP2_BIN_COUNT - 1)
      {
        bin = heap_ptr->BIN[bin + 1].binToUseForMalloc;
        binToSearch = &heap_ptr->BIN[bin];
      }
    }
    tempFreeBlock.nextPtr = binToSearch->firstNode;
    temp = &tempFreeBlock;

    while((temp->nextPtr != NULL) && (NEXT_BLOCK_SIZE(temp) < size_needed)){
     /*sanity check for the free block in the list*/
      MEMHEAP_ASSERT(BOUNDARY_CHECK((mem_block_header_type*)(temp->nextPtr), heap_ptr));
      temp = temp->nextPtr;
    }

    if(temp->nextPtr != NULL){

      answer = (mem_block_header_type*)(temp->nextPtr);
      /* Output sanitization */
      pblk = (uint16 *)answer;
      MEMHEAP_ASSERT(!(INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk)));
      if(answer == (mem_block_header_type*)(binToSearch->firstNode))
        binToSearch->firstNode = temp->nextPtr->nextPtr;

      if(answer == (mem_block_header_type*)(binToSearch->lastNode)){
        if((temp->freeBlock).forw_offset)
          binToSearch->lastNode = temp;
        else
          binToSearch->lastNode = NULL;
      }


      temp->nextPtr = temp->nextPtr->nextPtr;
      /*update the header_guard of the temp  since the next has  changed*/
      if(&tempFreeBlock != temp){
        pblk = (uint16*) temp;
        ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);
       }
      binToSearch->totalNodes = binToSearch->totalNodes - 1;

      if(!(binToSearch->totalNodes)){
        bin_deactive(heap_ptr, bin);
      }
      /*for safe-unlinking and Output sanitization  */
     ((freeBlockList*)answer)->nextPtr = NULL;
     bin_stats(bin,1);
      return answer;
    }
    else{
      return NULL;
    }


  }
  else if(mem_ptr){

    uint32 bin = memheap_find_bin(((mem_block_header_type*)mem_ptr)->forw_offset);
    binStruct *binToSearch = &heap_ptr->BIN[bin];
    freeBlockList tempFreeBlock = {{0},0};
    freeBlockList *temp;
    tempFreeBlock.nextPtr = binToSearch->firstNode;
    temp = &tempFreeBlock;

    while((temp->nextPtr != NULL) && ((void *)temp->nextPtr != mem_ptr)){
     /*sanity check for the free block in the list*/
     MEMHEAP_ASSERT(BOUNDARY_CHECK((mem_block_header_type*)(temp->nextPtr), heap_ptr));
       temp = temp->nextPtr;
    }

    if(temp->nextPtr != NULL){
      /* Output sanitization */
      pblk = (uint16*)(temp->nextPtr);
      MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk));
      if(mem_ptr == (mem_block_header_type*)(binToSearch->firstNode))
        binToSearch->firstNode = temp->nextPtr->nextPtr;

      if(mem_ptr == (mem_block_header_type*)(binToSearch->lastNode)){
        if((temp->freeBlock).forw_offset)
          binToSearch->lastNode = temp;
        else
          binToSearch->lastNode = NULL;
      }

      temp->nextPtr = temp->nextPtr->nextPtr;
      /*update the header_guard of the temp  since the next has  changed*/
      if(&tempFreeBlock != temp){
        pblk = (uint16*) temp;
        ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);
      }
      binToSearch->totalNodes = binToSearch->totalNodes - 1;

      if(!(binToSearch->totalNodes)){
        bin_deactive(heap_ptr, bin);
      }
      /*for safe-unlinking */
      ((freeBlockList*)mem_ptr)->nextPtr = NULL;
      bin_stats(bin,1);
      return mem_ptr;
    }
    else{
      // If the address of the freeblock is not found then it must assert
       MEMHEAP_ASSERT(0);
    }
  }
  return NULL;
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
   uint16 *pblk = NULL;
   mem_block_header_type *nextBlkPtr = NULL;
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
} /* END mem_get_next_block */



/*===========================================================================
FUNCTION MEM_INIT_HEAP

DESCRIPTION
  Initializes the heap_ptr object and sets up inMemoryChunk for use with the
  heap_ptr object.  inMemoryChunk may be aligned on any boundary.  Beginning
  bytes will be skipped until a paragraph boundary is reached.  Do NOT pass
  in NULL pointers.  infail_fnc_ptr may be NULL in which case no function will
  be called if mem_malloc or mem_calloc is about to fail.  If infail_fnc_ptr
  is provided, it will be called once and then the allocation will be
  attempted again.  See description of my_allocator_failed_proc for details.
  There is no protection for initializing a heap more than once.  If a heap
  is re-initialized, all pointers previously allocated from the heap are
  immediately invalidated and their contents possibly destroyed.  If that's
  the desired behavior, a heap may be initialized more than once.
===========================================================================*/
/*lint -sem(mem_init_heap,1p,2p,2P>=3n) */
void umem_init_heap(
   mem_heap_type                 *heap_ptr,
      /* Statically allocated heap structure
      */
    void                          *heap_mem_ptr,
      /* Pointer to contiguous block of memory used for this heap
      */
   uint32                  heap_mem_size
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
  uint32 chunks;
    /* How many whole blocks of size kMinChunkSize fit in the area of
       memory starting at memory_start_ptr and ending at (memory_end_ptr-1)
    */
  uint16 * pblk = NULL;

  MEMHEAP_ASSERT(heap_ptr);


  MEMHEAP_ASSERT(umagic_num_index < MAX_HEAP_INIT); /* support at the most 3 heaps*/

  if( (heap_ptr->magic_num) &&
      (heap_ptr->magic_num == umagic_num[heap_ptr->magic_num_index])){
  /* heap is already initialized so just return */
  return;
  }

  memset(heap_ptr, 0, sizeof(mem_heap_type));

  MEMHEAP_ASSERT(heap_mem_ptr);
  MEMHEAP_ASSERT(heap_mem_size);
  MEMHEAP_ASSERT(heap_mem_size >= (2*kMinChunkSize-1));

  memory_start_ptr = (char *)heap_mem_ptr;
  memory_end_ptr   = memory_start_ptr + heap_mem_size;

    /* by default it is critical section */
  heap_ptr->lock_fnc_ptr = mem_heap_enter_crit_sect;
  heap_ptr->free_fnc_ptr = mem_heap_leave_crit_sect;

  /*move the memory start pointer by sizeof(osal_crit_sect_t) as we have used that much out of the heap;*/
  //memory_start_ptr = (char *)memory_start_ptr + sizeof(osal_crit_sect_t);

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

  chunks = (uint32) ((memory_end_ptr - memory_start_ptr) / kMinChunkSize);

  heap_ptr->first_block            = (mem_block_header_type *) memory_start_ptr;
  heap_ptr->next_block             = heap_ptr->first_block;

  if( heap_mem_size < MIN_HEAP_SIZE_FOR_BINS){
     heap_ptr->legacy_check = kUseLegacyImpl;
  }else{
     heap_ptr->legacy_check = kUseBINsImpl;
  }

  mem_heap_get_random_num((&umagic_num[umagic_num_index_array[umagic_num_index]]), 4);
  heap_ptr->magic_num = umagic_num[umagic_num_index_array[umagic_num_index]];
  heap_ptr->magic_num_index = umagic_num_index_array[umagic_num_index];
  mem_heap_get_random_num(&(heap_ptr->magic_num_free), 2);
  mem_heap_get_random_num(&(heap_ptr->magic_num_used), 2);
  umagic_num_index++;
  mem_init_block_header(heap_ptr->first_block, chunks * kMinChunkSize, heap_ptr);
  heap_ptr->first_block->last_flag = (char) kLastBlock;
  heap_ptr->total_blocks           = 1;
  heap_ptr->max_used               = 0;
  heap_ptr->max_request            = 0;
  heap_ptr->used_bytes             = 0;
  heap_ptr->total_bytes            = chunks * kMinChunkSize;

  if(heap_ptr->legacy_check & kUseBINsImpl){
    init_binToUseForMalloc(heap_ptr, heap_ptr->first_block);
    (void)memheap_addNodeToBin(heap_ptr, heap_ptr->first_block);
  }
  pblk = (uint16*)(heap_ptr->first_block);
  ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);

//  heap_ptr->memheap_crit_sect = (void *)platform_force_mutex_init((void *)(heap_ptr->memheap_crit_sect_mem));

  return;
} /* END mem_init_heap */


/*===========================================================================
FUNCTION MEM_DEINIT_HEAP

DESCRIPTION
  De-Initializes the heap_ptr object only if the heap is in reset state.
  User is responsible for freeing all the allocated pointers before  calling
  into this function.
===========================================================================*/
void umem_deinit_heap(
   mem_heap_type                 *heap_ptr
      /* Statically allocated heap structure
      */

)
{
 // return the magic number
  umagic_num_index--;
  if(umagic_num_index < MAX_HEAP_INIT)
  {
umagic_num_index_array[umagic_num_index]=heap_ptr->magic_num_index;
  }

#if 1 //support OM transitions where clients are not forced to de-allocate their memory
   memset(heap_ptr, 0, sizeof(mem_heap_type));
#else
 /* De-initialize heap only if all the allocated blocks are freed */
 if(heap_ptr->used_bytes == 0)
 {
   qurt_rmutex_destroy((qurt_mutex_t*)&(heap_ptr->memheap_crit_sect));
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
   uint32          size,
     /* The size of the block of memory controlled by this
        memory header block INCLUDING the size of the
        header block itself
     */
     mem_heap_type *heap_ptr
)
{
  char *p_temp;
  MEMHEAP_ASSERT(block_ptr);
  p_temp = ((char*)block_ptr);
  memset(p_temp, 0 , sizeof(mem_block_header_type));
  block_ptr->free_flag   = (char) kBlockFree;
  block_ptr->forw_offset = size;

  return ;
} /* END mem_init_block_header */

volatile uint32 umem_malloc_counter = 0;

/*===========================================================================
FUNCTION MEM_MALLOC

DESCRIPTION
  Allocates a block of size bytes from the heap.  If heap_ptr is NULL
  or size is 0, the NULL pointer will be silently returned.

  Returns a pointer to the newly allocated block, or NULL if the block
  could not be allocated.
===========================================================================*/
/*lint -sem(mem_malloc,1p,2n>=0&&(@p==0||@P==2n)) */
void* umem_malloc(
  mem_heap_type *heap_ptr,
     /* Heap from which to allocate
     */

  unsigned int         size
     /* Number of bytes to allocate
     */
//  void *ra_of_caller
     /* Return address of caller
     */
)
{

  uint32 chunks;
    /* the computed minimum size of the memory block in chunks needed
       to satisfy the request */

  uint32 actualSize;

    /* the computed number of unused bytes at the end of the allocated
       memory block.  Will always be < kMinChunkSize */

  mem_block_header_type *freeBlock = NULL;
    /* the free block found of size >= actualSize */

  void *answer = NULL;
    /* the address of memory to be returned to the caller */

  uint16 * pblk = NULL;
  uint32 blockHeaderSize=sizeof(mem_block_header_type);

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
#ifdef NT_TU_HEAP_STATS
  uint32 malloc_size = 0;
#endif
  MEMHEAP_ASSERT(heap_ptr != NULL);



  if (!size) return NULL;

  umem_malloc_counter++;


  /* quick check if requested size of memory is available */
  if( (uint32) size > heap_ptr->total_bytes ) return NULL;

  /* chunks overflow check : check max memory that can be malloc'd at a time */
  if( (0xFFFFFFFF - ( kMinChunkSize + sizeof(mem_block_header_type)) )
     < ((uint32) size)) return NULL;




  chunks = ((uint32) size + blockHeaderSize
            + kMinChunkSize - 1) / kMinChunkSize;
 actualSize = chunks * kMinChunkSize;
//#ifdef NT_TU_HEAP_STATS
//  malloc_size = actualSize;
//#endif
 /*min block size is kMinBlockSize */
  if (actualSize < kMinBlockSize)
  {
actualSize = kMinBlockSize;
  }

  BEGIN_CRITICAL_SECTION(heap_ptr);

  MEMHEAP_ASSERT(heap_ptr->magic_num == umagic_num[heap_ptr->magic_num_index]);
    if(heap_ptr->legacy_check & kUseBINsImpl){
      freeBlock = findNRemoveFreeBlockInBINs(heap_ptr,NULL, actualSize);
    }
    else{
      freeBlock = mem_find_free_block(heap_ptr, actualSize);
    }

  if (freeBlock)
  {
      /* split the block (if necessary) and return the new block */

      MEMHEAP_ASSERT(freeBlock->forw_offset > 0);
      // frd offset and actual size are chunk aligned
      if (freeBlock->forw_offset >= (actualSize+kMinBlockSize))
      {
        /* must split into two free blocks */


        mem_block_header_type *newBlock = (mem_block_header_type *)
                                          ((char *) freeBlock + actualSize);
        mem_init_block_header(newBlock, freeBlock->forw_offset - actualSize, heap_ptr);
        newBlock->last_flag = freeBlock->last_flag;
        freeBlock->forw_offset = actualSize;
        freeBlock->last_flag = 0;


         ++heap_ptr->total_blocks;
        /*Add this new remaining free block after spliting into the free Block list*/
        if(heap_ptr->legacy_check & kUseBINsImpl){
          (void)memheap_addNodeToBin(heap_ptr, newBlock);
        }
        /*dont have footer for 16 bytes memory block due to size limitation*/
        if(newBlock->forw_offset > kMinBlockSize ){
            memheap_copy_frd_offset_at_end(newBlock, heap_ptr);
        }
        pblk = (uint16*)newBlock;

        ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);
      }


      /* mark the block as used and return it */

      freeBlock->free_flag = kBlockUsed;
      freeBlock->extra = (unsigned char)(freeBlock->forw_offset - size - blockHeaderSize);;
      /*  set up next block to search for
          next allocation request */
      heap_ptr->next_block = mem_get_next_block(heap_ptr, freeBlock);

#ifdef NT_TU_HEAP_STATS
  malloc_size = freeBlock->forw_offset;
#endif
      heap_ptr->used_bytes += freeBlock->forw_offset;
      MEMHEAP_ASSERT(heap_ptr->total_bytes >= heap_ptr->used_bytes);
 #ifdef NT_TU_HEAP_STATS
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
      if (heap_ptr->used_bytes > heap_ptr->max_used) {
        heap_ptr->max_used = heap_ptr->used_bytes;
      }
      if (size > heap_ptr->max_request) {
        heap_ptr->max_request = size;
      }

     /*set the frd_Offset_info in the free block to NULL for security */
      {
        frd_Offset_info *temp;
        temp = (frd_Offset_info*)((char*)freeBlock + freeBlock->forw_offset - sizeof(frd_Offset_info));
        temp->freeBlock_frdOff = 0;
        temp->pad = 0;
      }

      pblk = (uint16*)freeBlock;
      ADD_GUARD_BYTES_TO_USED_HEADER(heap_ptr->magic_num_used, pblk);
      answer = (char *) freeBlock + blockHeaderSize;
  }

#ifdef FEATURE_MEM_DEBUG
    if(answer != NULL)
    {
      freeBlock->caller_ptr=MEM_HEAP_CALLER_ADDRESS(MEM_HEAP_CALLER_ADDRESS_LEVEL);
      freeBlock->caller_ptr_2 = ra_of_caller;
    }
#endif
    END_CRITICAL_SECTION(heap_ptr);

  return answer;
} /* END mem_malloc */


/*===========================================================================
FUNCTION MEM_FREE

DESCRIPTION
  Deallocates the ptr block of memory.  If ptr is NULL, heap_ptr is NULL or
  ptr is outside the range of memory managed by heap_ptr, then this function
  call does nothing (and is guaranteed to be harmless).  This function will
  ASSERT if it can detect an attempt to free an already freed block.  (This
  is not always reliable though, so it might not catch it.)
===========================================================================*/
/*lint -sem(mem_free,1p) */



void umem_free(
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


  MEMHEAP_ASSERT(heap_ptr != NULL);


  /*commented out the below assert since NULL free occurances are found and once
   corrsponding fixes are in its need to be uncomment*/
  MEMHEAP_ASSERT(heap_ptr->magic_num == umagic_num[heap_ptr->magic_num_index]);



  /*commented out the below assert since NULL free occurances are found and once
   corrsponding fixes are in its need to be uncomment*/
  if(ptr == NULL)
  {
     //MEMHEAP_ERROR(" NULL ptr occurenaces in mem_free()",0,0,0);
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
  MEMHEAP_ASSERT(theBlock->extra < kMinBlockSize);

  /* Make sure forw_offset is not spiling over the heap boundry */
  MEMHEAP_ASSERT(FRD_OFFSET_CHECK(theBlock, heap_ptr));

 //check for heap canary
  pblk = (uint16*)(theBlock);
  MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_USED_HEADER(heap_ptr->magic_num_used, pblk));



  if (!theBlock->free_flag) /* Be intelligent about not munging the heap if
                              a multiple free of the same block is detected */
  {


    MEMHEAP_ASSERT((theBlock->forw_offset) <= heap_ptr->used_bytes);
    heap_ptr->used_bytes -= theBlock->forw_offset;
#ifdef NT_TU_HEAP_STATS
    free_size = theBlock->forw_offset;
#endif
    MEMHEAP_ASSERT(heap_ptr->total_bytes >= heap_ptr->used_bytes);
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
        	ulTaskId = theBlock->tid;
        	nt_table_update_free(free_size,SchedulerState,ulTaskId);
        }
#endif

    theBlock->free_flag = (char) kBlockFree;
    /* try to do defragmentation if possible*/
    /*check if it can concatenate the previous block of the theBlock*/
   if(theBlock > heap_ptr->first_block)
    {
      mem_block_header_type *preFreeBlock;
      int validBlock = FALSE;

      temp = (frd_Offset_info*)((char*)theBlock - sizeof(frd_Offset_info));

      if((!((temp->freeBlock_frdOff)%kMinChunkSize))
         &&(temp->pad  == umagic_num[heap_ptr->magic_num_index])
         &&(FOOTER_FRD_OFFSET_CHECK(theBlock, heap_ptr, temp->freeBlock_frdOff)))
      {
          validBlock = TRUE;
      }

      if(validBlock){
         preFreeBlock =  (mem_block_header_type *)((char*)theBlock - temp->freeBlock_frdOff);
         pblk = (uint16*)preFreeBlock;
         if((preFreeBlock->forw_offset == temp->freeBlock_frdOff)
            && BOUNDARY_CHECK(preFreeBlock, heap_ptr)
            && FRD_OFFSET_CHECK(pblk, heap_ptr)
            &&(!INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk)) /*we don’t want to ASSERT if integrity check fails , just don’t concatenate the block*/
            &&(preFreeBlock->free_flag == kBlockFree))
         {

            /* preFreeBlock is free and now we can join it with the new
            free block theBlock so remove it from the list if apply*/
            if(heap_ptr->legacy_check & kUseBINsImpl)
            {
               (void)findNRemoveFreeBlockInBINs(heap_ptr, preFreeBlock, 0);
            }
            /* Set preFreeBlock's end bytes to 0 as the frd_offset has changed */
            temp->freeBlock_frdOff = 0;
            temp->pad = 0;
            --heap_ptr->total_blocks;
            preFreeBlock->forw_offset += theBlock->forw_offset;
            preFreeBlock->last_flag = theBlock->last_flag;

            /* now set the theBlock's header info. to NULL */
            {
               uint32 *temp = (uint32*)theBlock; /*did it like this for optimization purpose*/
               temp[0] = 0;
               temp[1] = 0;
            }
            theBlock = preFreeBlock;
         }
      }/*if(validBlock)*/

   }/*if(theBlock != heap_ptr->first_block)*/

   /*check if it can concatenate the next block of the theBlock*/
   if(theBlock->last_flag != kLastBlock)
   {
      mem_block_header_type *nextFreeBlock =
      (mem_block_header_type *)((char *)theBlock + theBlock->forw_offset);

      if(nextFreeBlock->free_flag == kBlockFree)
      {
         pblk = (uint16*)nextFreeBlock;
         MEMHEAP_ASSERT(!INTEGRITY_CHECK_ON_FREE_HEADER(heap_ptr->magic_num_free, pblk));
         MEMHEAP_ASSERT(BOUNDARY_CHECK(nextFreeBlock, heap_ptr));
         MEMHEAP_ASSERT(FRD_OFFSET_CHECK(pblk,heap_ptr));
         if(heap_ptr->legacy_check & kUseBINsImpl){
           (void)findNRemoveFreeBlockInBINs(heap_ptr, nextFreeBlock, 0);
         }
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

   if(heap_ptr->legacy_check & kUseBINsImpl){
      (void)memheap_addNodeToBin(heap_ptr, (void*)theBlock);
   }
   else{
      /* Backup now to lessen possible fragmentation, useful for Legacy impl. */
      heap_ptr->next_block = theBlock;
   }
   pblk = (uint16*)theBlock;
   ADD_GUARD_BYTES_TO_FREE_HEADER(heap_ptr->magic_num_free, pblk);

   /*dont have footer for 16 bytes memory block due to size limitation*/
   if(theBlock->forw_offset > kMinBlockSize ){
      memheap_copy_frd_offset_at_end(theBlock, heap_ptr);
   }


   /* reset heap to initial state if everything is now freed */
   if (!heap_ptr->used_bytes) {
      /* reset heap now, but retain statistics */
      heap_ptr->next_block = heap_ptr->first_block;
   }
  }
     END_CRITICAL_SECTION(heap_ptr);
     return;
} /* END mem_free */

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
   uint32  size_needed
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

void *pvPortMalloc
(
size_t size
)
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
      ptr = umem_malloc(&amss_mem_heap,size);
    (void) xTaskResumeAll();
    return ptr;
}

/*===========================================================================

FUNCTION free

DESCRIPTION
  Deallocates a block of memory and returns it to the heap. This function also
  handles a case when an allocation has user data equivalent to 'magic number'
  in 'pad' field in its footer. This is only for M4.

DEPENDENCIES
  None

RETURN VALUE
  None

SIDE EFFECTS
  Replaces C Std Library implementation.

ARGUMENTS
  ptr - A pointer to the memory block that needs to be deallocated.

===========================================================================*/
void vPortFree
(
  void *ptr
)
{
	vTaskSuspendAll();
    umem_free(&amss_mem_heap,ptr);
    (void) xTaskResumeAll();
    return;
}
#endif
