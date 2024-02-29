/*
 *Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef _QC_HEAP_H_
#define _QC_HEAP_H_

#include "nt_flags.h"
#if (NT_FN_QC_HEAP == 1)
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "qurt_internal.h"


#define RTOS_FREERTOS


//uint32 *Image__RAM_FOM_BSP_ZI_REGION_Base;

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

#define INTEGRITY_CHECK_ON_USED_HEADER(magic_num_used, block) \
            (block[0]^block[1]^block[2]^block[3]^magic_num_used)


#define INTEGRITY_CHECK_ON_FREE_HEADER(magic_num_free, block) \
            (block[0]^block[1]^block[2]^block[3]^magic_num_free)


#define ADD_GUARD_BYTES_TO_USED_HEADER(magic_num_used, block) \
            (block[0] = block[1]^block[2]^block[3]^magic_num_used)


#define ADD_GUARD_BYTES_TO_FREE_HEADER(magic_num_free, block) \
            (block[0] = block[1]^block[2]^block[3]^magic_num_free)

#define MEMHEAP_ASSERT( xx_exp ) \
       if( !(xx_exp) ) \
       { \
    	   configASSERT( xx_exp ); \
       }
#ifdef PLATFORM_FERMION
#define HEAP_END_ADDR   ( 0x9FFFF )
#else
#define HEAP_END_ADDR	( 0x7FFFF )
#endif //PLATFORM_FERMION

/* Starting address of heap provided by linker */
extern unsigned char _ln_RAM_addr_heap_start__;

//#define pdFALSE   0
//#define pdTRUE   1


#ifndef MEMHEAP_CRIT_SECT_SIZE
   #define MEMHEAP_CRIT_SECT_SIZE 64
#endif
#define MEMHEAP_MIN_BLOCK_SIZE 16

typedef struct frd_Offset_info_type{
  unsigned long pad;
  unsigned long freeBlock_frdOff;
}frd_Offset_info;


typedef enum
{
  HEAP_TYPE_RTOS,
  HEAP_TYPE_MEMHEAP,
}heap_type_enum;

typedef struct
{
 heap_type_enum heap_type;
 void* heap_start_address;
 uint32 heap_size;
}heap_config_type;

typedef struct mem_block_header_struct {
  uint16        header_guard;

  unsigned char extra;        /**< Extra bytes at the end of a block. */
  char          free_flag:4;  /**< Flag to indicate if this memory block
                                   is NT_free. */
  char          last_flag:4;/**< Flag to indicate if this is the last block
                                   in the allocated section. */
  uint32 forw_offset ; /**< Forward offset. The value of the offset
                 includes the size of the header and the
                                   allocated block. */
#ifdef NT_TU_HEAP_STATS
  uint32 tid;
#endif

} mem_block_header_type;

/**
  Provides an abstraction so each function can have its own function to
  lock the heap.

  @param[in] ptr Pointer to what is to be locked.

  @return
  None.

  @dependencies
  None.
*/
typedef void (*mem_lock_fnc_type)( void * ptr);

typedef void (*mem_free_fnc_type)( void * ptr);
/**
  Provides an abstraction so each function can have its own function to
  NT_free the heap.

  @param[in] ptr Pointer to what is to be freed.

  @return
  None.

  @dependencies
  None.
*/


typedef struct mem_heap_struct {
  uint32                         magic_num;
  mem_block_header_type         *first_block;
  /**< First block in the heap. */
  mem_block_header_type         *next_block;
  /**< Next free block in the heap. */
  unsigned long                  total_blocks;
  /**< Total blocks in the heap. */
  unsigned long                  total_bytes;
  /**< Total bytes available in the heap. */
  unsigned long                  used_bytes;
 // uint32 used_bytes;
  /**< Amount of bytes in use in the heap. */
  unsigned long                  max_used;
  /**< Maximum amount that has been used in the heap. */
  unsigned long                  max_request;
  /**< Pointer to the failed function. */
  mem_lock_fnc_type              lock_fnc_ptr;
  mem_free_fnc_type              free_fnc_ptr;
  uint8 memheap_crit_sect_[MEMHEAP_CRIT_SECT_SIZE];
   /**< this is random number used in XOR(guard_bytes)
    calculation of NT_free memory block  */
  uint16 magic_num_free;
/**< this is random number used in XOR(guard_bytes)
    calculation of used memory block  */
  uint16 magic_num_used;
  uint16 magic_num_index;

} mem_heap_type;

#ifndef CONFIG_HEAP_STATISTIC
void *pvPortMalloc( size_t xWantedSize );
void vPortFree( void *pv );
#else

#ifndef pvPortMalloc
#ifndef mem_heap_statistics_type
#define mem_heap_statistics_type mem_heap_statistics_type
typedef struct mem_heap_statistics {
  const char *function;
  unsigned int req_size;
}mem_heap_statistics_type;
#endif

extern unsigned int heap_statistics_index;
extern mem_heap_statistics_type heap_statistics[];

void *__pvPortMalloc( size_t xWantedSize );

#define pvPortMalloc(size) (({heap_statistics[heap_statistics_index%NT_HEAP_RCD_CNT].function=__FUNCTION__; \
                                            heap_statistics[heap_statistics_index%NT_HEAP_RCD_CNT].req_size=size; \
                                            heap_statistics_index++;}) , \
                                         (__pvPortMalloc(size)))
#endif

#ifndef vPortFree
extern unsigned int heap_statistics_free_index;
extern const char *heap_statistics_free[];
void __vPortFree(  void *ptr );
#define vPortFree(ptr) (({heap_statistics_free[heap_statistics_free_index%NT_HEAP_RCD_CNT]=__FUNCTION__; \
                                            heap_statistics_free_index++;}) , \
                                         (__vPortFree(ptr)))
#endif
#endif


#endif //CONFIG_HEAP_STATISTIC
#endif //_QC_HEAP_H_

