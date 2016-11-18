/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include "os_test_priv.h"

TEST_CASE(os_sem_test_case_4)
{
    os_error_t err;

    err = os_sem_init(&g_sem1, 1);
    TEST_ASSERT(err == OS_OK);

    os_task_init(&task1, "task1", sem_test_sleep_task_handler, NULL,
                 TASK1_PRIO, OS_WAIT_FOREVER, stack1,
                 sizeof(stack1));

    os_task_init(&task2, "task2", sem_test_4_task2_handler, NULL,
                 TASK2_PRIO, OS_WAIT_FOREVER, stack2,
                 sizeof(stack2));

    os_task_init(&task3, "task3", sem_test_4_task3_handler, NULL,
                 TASK3_PRIO, OS_WAIT_FOREVER, stack3,
                 sizeof(stack3));

    os_task_init(&task4, "task4", sem_test_4_task4_handler, NULL,
                 TASK4_PRIO, OS_WAIT_FOREVER, stack4,
                 sizeof(stack4));

#if MYNEWT_VAL(SELFTEST)
    os_start();
#endif
}
