/*
 *Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *SPDX-License-Identifier: BSD-3-Clause-Clear
 */
/*
 * nt_heap_test.h
 *
 *  Created on: Mar 6, 2021
 *      Author: adityara
 */

#ifndef NT_HEAP_TEST_H_
#define NT_HEAP_TEST_H_

#include "nt_flags.h"

#ifdef NT_TST_HEAP

#include "nt_osal.h"

//TaskHandle_t handle[0] = {NULL};

//task_Handle_t heap_test_handle[0] = {NULL};

void nt_rand_malloc_free_task(void *pvParam );

void nt_1_malloc_1_free_task(void *pvParam );



void nt_rand_malloc_free_heap_test(int);
void nt_1_malloc_1_free_heap_test(int);

#endif //NT_TST_HEAP
void runHeapTest(uint8_t res1,uint16_t res2,uint8_t res3);
#endif /* NT_HEAP_TEST_H_ */
