/*
 * Copyright (c) 2022 Talkweb Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ohos_run.h"
#include "cmsis_os2.h"

#define FLAGS_MSK1 0x00000001U

osEventFlagsId_t g_event_flags_id; // event flags id

void OS_Thread_EventSender(void *argument)
{
    (void *)argument;
    osEventFlagsId_t flags;
    printf("Start OS_Thread_EventSender.\n");
    while (1) {
        flags = osEventFlagsSet(g_event_flags_id, FLAGS_MSK1);
        printf("Send Flags is %d\n", flags);
        osThreadYield();
        osDelay(1000);
    }
}

void OS_Thread_EventReceiver(void *argument)
{
    (void *)argument;
    printf("Start OS_Thread_EventSender.\n");
    while (1) {
        uint32_t flags;
        flags = osEventFlagsWait(g_event_flags_id, FLAGS_MSK1, osFlagsWaitAny, osWaitForever);
        printf("Receive Flags is %u\n", flags);
    }
}

void OS_Event_example(void)
{
    printf("Start OS_Event_example.\n");
    g_event_flags_id = osEventFlagsNew(NULL);
    if (g_event_flags_id == NULL) {
        printf("Falied to create EventFlags!\n");
        return;
    }

    osThreadAttr_t attr;

    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024 * 4;
    attr.priority = 25;

    attr.name = "Thread_EventSender";
    if (osThreadNew(OS_Thread_EventSender, NULL, &attr) == NULL) {
        printf("Falied to create Thread_EventSender!\n");
        return;
    }
    
    attr.name = "Thread_EventReceiver";
    if (osThreadNew(OS_Thread_EventReceiver, NULL, &attr) == NULL) {
        printf("Falied to create Thread_EventReceiver!\n");
        return;
    }
}

OHOS_APP_RUN(OS_Event_example);
