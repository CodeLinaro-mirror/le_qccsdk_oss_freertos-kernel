/*
 *Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *SPDX-License-Identifier: BSD-3-Clause-Clear
 */
/*
 * nt_heap_test.c
 *
 *  Created on: Mar 6, 2021
 *      Author: adityara
 */

#include "nt_flags.h"

#ifdef NT_TST_HEAP

#include "nt_heap_test.h"
#include "FreeRTOSConfig.h"
#include "qc_heap.h"
#include "stdio.h"
#include "stdlib.h"
#include "uart.h"
#include "wlan_dev.h"
nt_osal_task_handle_t heap_test_handle[4] = {NULL};

typedef struct heap_test_s {
	uint8_t type;
	uint16_t value;
} heap_test_t;

unsigned int total_num_malloc =0; //for count the number of malloc for all task
unsigned int total_num_free =0; //for count the number of free for all task
int Total_task_size =0;
int total_size_task3 =0;
int *arr1[100] = {NULL}; //for address
unsigned int arr2[100] ={0};      // for storing the wanted size
int num_malloc = 0; //for storing the address in array
int num_free =0;    //for removing address from array
unsigned int malloc_cnt =0; //for counting the number of pvPortMalloc function call
unsigned int free_cnt =0;   //for counting the number of vPortFree function call
int Max_size = 0;
unsigned int malloc_fail =0; // for memory fragmentation
unsigned int malloc_fail1=0; //for requested size not available
unsigned int Total_size_check =0;
unsigned int Total_size = 0; //Total allocated size for task 1
unsigned int Total_size_ = 0; //Total allocated size for task 4
int total_size_ = 0; //total allocated size for task 2
int result = 0;

int Find_Available_slot(int param){ // 0-> for finding free slot to store the address , 1-> Finding available slot to free
	if(param == 0){
		for(int free_slot =0; free_slot<100; free_slot++){
			if(arr1[free_slot]== 0){
				return free_slot;
			}
		}
	}
	else if(param == 1){
		for(int available_slot =0; available_slot<100; available_slot++){
			if(arr1[available_slot] != 0){
				return available_slot;
			}
		}
	}
	return -1;
}

void nt_rand_malloc_free_task(void *pvParam)
{
	result = 0;

	heap_test_t *test = (heap_test_t *)pvParam;
	uint16_t value = test->value;
	uint32_t timestamp = 0;



	nt_dbg_print("Random Malloc/Free\r\n");

	/** */
	switch(test->type)
	{
	case 1:
		/** timing handling */
		for(;;)
		{
			if(timestamp == 0)
			{
				timestamp = nt_hal_get_curr_time();
			}
			else
			{
#ifdef NT_DEBUG
				//NT_LOG_SYSTEM_INFO("HAL (ms):",nt_hal_calc_time_elapsed(timestamp),0,0);
				//NT_LOG_SYSTEM_INFO("USER (ms):",(test->value*1000),0,0);
#endif
				if(nt_hal_calc_time_elapsed(timestamp) <  (test->value*1000))
				{
					nt_rand_malloc_free_heap_test(0);
				}
				else
				{
					timestamp = 0;
					nt_dbg_print("\r\n>Timing Test Complete\r\n");
					break;
				}
			}
		}
		break;

	case 2:
		/** iteration handling */
		for(uint16_t iter = 1;iter<=value;iter++){

			nt_rand_malloc_free_heap_test(0);

		}

		nt_dbg_print("\r\n>Iteration Test Complete\r\n");
		break;

	default :
		nt_dbg_print("Incorrect parameter\r\n");
	}
	nt_osal_free_memory(pvParam);
	nt_osal_thread_delete(NULL);
}

int malloc_cnt1 =0;
int free_cnt1 =0;

void nt_1_malloc_1_free_task(void *pvParam)
{
	result = 0;

	heap_test_t *test = (heap_test_t *)pvParam;
	uint16_t value = test->value;
	uint32_t timestamp = 0;



	nt_dbg_print("1-Malloc\\1-Free\r\n");

	/** */
	switch(test->type)
	{
	case 1:
		/** timing handling */
		for(;;)
		{
			if(timestamp == 0)
			{
				timestamp = nt_hal_get_curr_time();
			}
			else
			{
#ifdef NT_DEBUG
				//NT_LOG_SYSTEM_INFO("HAL (ms):",nt_hal_calc_time_elapsed(timestamp),0,0);
				//NT_LOG_SYSTEM_INFO("USER (ms):",(test->value*1000),0,0);
#endif
				if(nt_hal_calc_time_elapsed(timestamp) <  (test->value*1000))
				{
					nt_1_malloc_1_free_heap_test(0);
				}
				else
				{
					timestamp = 0;
					nt_dbg_print("\r\n>Timing Test Complete\r\n");
					break;
				}
			}
		}
		break;

	case 2:
		/** iteration handling */
		for(uint16_t iter = 1;iter<=value;iter++){

			nt_1_malloc_1_free_heap_test(0);

		}

		nt_dbg_print("\r\n>Iteration Test Complete\r\n");
		break;

	default :
		nt_dbg_print("\r\nIncorrect parameter\r\n");
	}
	nt_osal_free_memory(pvParam);
	nt_osal_thread_delete(NULL);
}


void nt_1_malloc_1_free_heap_test(int temp){
	char pcWriteBuffer[200];
	int wanted_size = 0;
	unsigned int check_malloc = 0;


	int *ptr_ret_val;
	snprintf((char *)pcWriteBuffer,sizeof(pcWriteBuffer),"\r\n>Iteration Count : %d",temp);
	nt_dbg_print(pcWriteBuffer);

	for(check_malloc = 0; check_malloc < 10000;check_malloc++)
	{

		wanted_size = (rand()%100000)+10;
		ptr_ret_val = pvPortMalloc(wanted_size);
		//nt_dbg_print("\r\nMalloc");
		if(ptr_ret_val){
			total_size_ += wanted_size;
			//Total_task_size += wanted_size;
			malloc_cnt1++;
			total_num_malloc++;
		}
		vPortFree(ptr_ret_val);
		//nt_dbg_print("\r\nFree");
		if(ptr_ret_val){
			total_size_ -= wanted_size;
			//Total_task_size -= wanted_size;
			free_cnt1++;
			total_num_free++;
		}
	}

	Total_task_size = Total_size + Total_size_ + total_size_ + total_size_task3;
	if(total_num_malloc == total_num_free){
		nt_dbg_print("\r\nUsed byte for tasks is Zero\r\n");
	}
	snprintf((char *)pcWriteBuffer,sizeof(pcWriteBuffer)-strlen(pcWriteBuffer),"Total Size: %u\r\nMalloc Count: %u\r\nFree Count: %u\r\nTotal size allocated by all task: %d\r\n",total_size_,malloc_cnt1,free_cnt1,Total_task_size);
	nt_dbg_print(pcWriteBuffer);
	snprintf((char *)pcWriteBuffer,sizeof(pcWriteBuffer)-strlen(pcWriteBuffer),"Total number of Mallocs: %u\r\nTotal number of Frees: %u\r\n",total_num_malloc,total_num_free);
	nt_dbg_print(pcWriteBuffer);
}

/*****************************************************************************************/

int *address[100] = {NULL}; //for address
unsigned int value_1[100] ={0};      // for storing the wanted size
int num_malloc_ = 0; //for storing the address in array
int num_free_ =0;    //for removing address from array
unsigned int malloc_cnt_ =0; //for counting the number of pvPortMalloc function call
unsigned int free_cnt_ =0;   //for counting the number of vPortFree function call
int Max_size_ = 0;
unsigned int malloc_fail_ =0; // for memory fragmentation
unsigned int malloc_fail1_=0; //for requested size not available
unsigned int Total_size_check_ =0;
int result_ =0;

int Find_Available_slot_task2(int param){ // 0-> for finding free slot to store the address , 1-> Finding available slot to free
	if(param == 0){
		for(int free_slot =0; free_slot<100; free_slot++){
			if(address[free_slot]== 0){
				return free_slot;
			}
		}
	}
	else if(param == 1){
		for(int available_slot =0; available_slot<100; available_slot++){
			if(address[available_slot] != 0){
				return available_slot;
			}
		}
	}
	return -1;
}

void runHeapTest(uint8_t res1,uint16_t res2,uint8_t res3){

	heap_test_t *test_case = (heap_test_t *)nt_osal_calloc(1,sizeof(heap_test_t));
	test_case->type = res1;
	test_case->value = res2;

	if (res1 == 1){ //time
		nt_dbg_print("Time Conf. Selected\r\n");
		if (res3 == 1){
			xTaskCreate( nt_rand_malloc_free_task , (const char* const )"Malloc_Random", 200,test_case,2, &heap_test_handle[0]);
		}
		else if(res3 ==2 ){
			xTaskCreate( nt_1_malloc_1_free_task , (const char* const )"1_Malloc_1_Free", 200,test_case,2, &heap_test_handle[1]);
		}
	}


	else if (res1 == 2){ //iter
		nt_dbg_print("Iteration Conf. Selected\r\n");
		if (res3 == 1){
			xTaskCreate( nt_rand_malloc_free_task , (const char* const )"Malloc_Random", 200,test_case,2, &heap_test_handle[0]);
		}
		else if(res3 ==2 ){
			xTaskCreate( nt_1_malloc_1_free_task , (const char* const )"1_Malloc_1_Free", 200,test_case,2, &heap_test_handle[1]);

		}
	}
}

void nt_rand_malloc_free_heap_test(int temp){

	unsigned int wanted_size = 0;
	char pcWriteBuffer[200];

	result = (rand()%2); //for malloc or free
	if(result == 1){
		for(int result1 = 0;result1 < (rand()%100);result1++){
			wanted_size = (rand()%100000)+10;
			if(wanted_size > Max_size){
				Max_size = wanted_size;
			}
			num_malloc = Find_Available_slot(0);
			if(num_malloc >= 0){
				arr1[num_malloc] = pvPortMalloc(wanted_size);
				if(arr1[num_malloc] != NULL){
					arr2[num_malloc] = wanted_size;
					Total_size += wanted_size;
					//Total_task_size += wanted_size;
					malloc_cnt++;
					total_num_malloc++;
				}
				else if(arr1[num_malloc] == NULL){
					Total_size_check = Total_size + wanted_size ;
					malloc_fail++;
					if(Total_size_check >= configTOTAL_HEAP_SIZE){
						malloc_fail1++;
						Total_size_check = 0;
					}
				}
				//sprintf((char *)pcWriteBuffer,"%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\r\n",configTOTAL_HEAP_SIZE,Total_size,malloc_cnt,free_cnt,malloc_fail,malloc_fail1,Max_size,wanted_size);
				//nt_dbg_print(pcWriteBuffer);
			}
		}
	}
	else if((result == 0)/* && (cnt%4 == 0)*/){
		for(result = 0;result < (rand()%100);result++){
			num_free = Find_Available_slot(1);
			if(num_free >= 0){
				vPortFree(arr1[num_free]);
				arr1[num_free] = 0;
				Total_size -= arr2[num_free];
				//Total_task_size -= arr2[num_free];
				arr2[num_free] = 0;
				free_cnt++;
				total_num_free++;
				//sprintf((char *)pcWriteBuffer,"%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\r\n",configTOTAL_HEAP_SIZE,Total_size,malloc_cnt,free_cnt,malloc_fail,malloc_fail1,Max_size,wanted_size);
				//nt_dbg_print(pcWriteBuffer);
			}
			//Max_size =0;
		}
	}
	if(temp % 100 == 0 || temp == 0){
		snprintf((char *)pcWriteBuffer,sizeof(pcWriteBuffer)-strlen(pcWriteBuffer),"\r\n>Iteration Count : %d",temp);
		nt_dbg_print(pcWriteBuffer);

		if(total_num_malloc == total_num_free){
			nt_dbg_print("\r\nUsed byte for tasks is zero");
		}
		Total_task_size = Total_size + Total_size_ + total_size_ + total_size_task3;

		snprintf((char *)pcWriteBuffer,sizeof(pcWriteBuffer)-strlen(pcWriteBuffer),"\r\nTotal Size : %u\r\nMalloc Count : %u\r\nFree Count : %u\r\nMalloc Fail : %u\r\nTotal size allocated by all task : %d",Total_size,malloc_cnt,free_cnt,malloc_fail,Total_task_size);
		nt_dbg_print(pcWriteBuffer);
		snprintf((char *)pcWriteBuffer,sizeof(pcWriteBuffer)-strlen(pcWriteBuffer),"\r\nTotal number of Malloc : %u\r\nTotal number of free : %u\r\n",total_num_malloc,total_num_free);
		nt_dbg_print(pcWriteBuffer);
		//cnt = 0;
		Max_size =0;
	}
}

#endif //NT_TST_HEAP

