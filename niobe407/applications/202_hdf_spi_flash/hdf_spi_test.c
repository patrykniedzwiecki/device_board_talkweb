/*
 * Copyright (c) 2021-2022 Talkweb Co., Ltd.
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

#include "hdf_log.h"
#include "spi_if.h"

#include "los_task.h"
#include "los_compiler.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include "samgr_lite.h"
#include "ohos_run.h"

#define HDF_SPI_STACK_SIZE 0x1000
#define HDF_SPI_TASK_NAME "hdf_spi_test_task"
#define HDF_SPI_TASK_PRIORITY 2
uint8_t txBuffer[] = "welcome to OpenHarmony\n";
#define WIP_FLAG       0x01
#define SPI_FLASH_IDx  0x4018
#define Countof(a)      (sizeof(a) / sizeof(*(a)))
#define bufferSize      (Countof(txBuffer) - 1)

uint8_t rxBuffer[bufferSize] = {0};
#define USE_TRANSFER_API 1

static uint8_t BufferCmp(uint8_t* pBuffer1, uint8_t* pBuffer2, uint16_t BufferLength)
{
    while(BufferLength--)
    {
        if(*pBuffer1 != *pBuffer2) {
            return 0;
        }

        pBuffer1++;
        pBuffer2++;
    }
    return 1;
}
#if (USE_TRANSFER_API == 1)
static uint16_t ReadDeviceId(DevHandle spiHandle)
{
    struct SpiMsg msg;                  /* 自定义传输的消息 */
    uint16_t deviceId = 0;
    uint8_t rbuff[5] = { 0 };
    uint8_t wbuff[5] = { 0xAB, 0xFF, 0xFF, 0xFF, 0xFF };
    int32_t ret = 0;
    msg.wbuf = wbuff;  /* 写入的数据 */
    msg.rbuf = rbuff;   /* 读取的数据 */
    msg.len = 5;        /* 读取写入数据的长度为4 */
    msg.csChange = 1;   /* 进行下一次传输前关闭片选 */
    msg.delayUs = 0;    /* 进行下一次传输前不进行延时 */
    //msg.speed = 115200; /* 本次传输的速度 */
    /* 进行一次自定义传输，传输的msg个数为1 */
    ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
    } else {
        deviceId = rbuff[4];
    }

    return deviceId;
}

static uint16_t ReadFlashId(DevHandle spiHandle)
{
    int32_t ret = 0;
    uint16_t flashId = 0;
    uint8_t rbuff1[4] = { 0 };
    uint8_t wbuff1[4] = { 0x9f, 0xFF, 0xFF, 0xFF };
    struct SpiMsg msg1 = {0};
    msg1.wbuf = wbuff1;
    msg1.rbuf = rbuff1;
    msg1.len = 4;
    msg1.csChange = 1;
    msg1.delayUs = 0;
    ret = SpiTransfer(spiHandle, &msg1, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
    } else {
        flashId = msg1.rbuf[2]<<8 | msg1.rbuf[3];
    }

    return flashId;
}

static void WaitForWriteEnd(DevHandle spiHandle)
{
    uint8_t FLASH_Status = 0;

    /* Send "Read Status Register" instruction */
    uint8_t wbuf[1] = {0x05};
    uint8_t wbuf1[1] = {0xff};
    uint8_t rbuf[1] = {0};
    struct SpiMsg msg = {0};
    msg.wbuf = wbuf;
    msg.rbuf = rbuf;
    msg.len = 1;
    msg.csChange = 0; // 不关片选
    msg.delayUs = 0;
    int32_t ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
    }
    
    /* Loop as long as the memory is busy with a write cycle */
    do
    {
      msg.wbuf = wbuf1;
      msg.rbuf = rbuf;
      msg.len = 1;
      // 等待写结束 不能关闭片选
      msg.csChange = 0;
      msg.delayUs = 0;

      ret = SpiTransfer(spiHandle, &msg, 1);
      if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
      }
      FLASH_Status = rbuf[0];
    }
    while ((FLASH_Status & WIP_FLAG) == 1); /* Write in progress */

    // 等待写结束后关闭片选
    msg.wbuf = wbuf1;
    msg.rbuf = rbuf;
    msg.len = 1;
    msg.csChange = 1;
    msg.delayUs = 0;
    
    ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
      HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
    }

}
static void WriteEnable(DevHandle spiHandle)
{
    uint8_t wbuf[1] = {0x06};
    uint8_t rbuf[1] = {0};
    struct SpiMsg msg = {0};
    msg.wbuf = wbuf;
    msg.rbuf = rbuf;
    msg.len = 1;
    msg.csChange = 1;
    msg.delayUs = 0;
    int32_t ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
    }
}

static void BufferWrite(DevHandle spiHandle, const uint8_t* buf, uint32_t size)
{
    WriteEnable(spiHandle);
    uint8_t wbuf[4] = {0x02, 0x00, 0x00, 0x00};
    uint8_t rbuf[4] = {0};
    uint8_t *rbuf1 = NULL;
    int32_t ret = 0;

    struct SpiMsg msg = {0};
    msg.wbuf = wbuf;
    msg.rbuf = rbuf;
    msg.len = 4;
    msg.csChange = 0;
    msg.delayUs = 0;
    ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
    }

    rbuf1 = (uint8_t*)OsalMemAlloc(size);
    if (rbuf1 == NULL){
        HDF_LOGE("OsalMemAlloc failed.\n");
        return;
    }
    
    memset(rbuf1, 0, size);
    msg.wbuf = buf;
    msg.rbuf = rbuf1;
    msg.len = size;
    msg.csChange = 1;
    msg.delayUs = 0;
    ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
    }

    WaitForWriteEnd(spiHandle);
    
    OsalMemFree(rbuf1);
}

static void BufferRead(DevHandle spiHandle, uint8_t* buf, uint32_t size)
{
    int32_t ret = 0;

    uint8_t wbuf[4] = {0x03, 0x00, 0x00, 0x00};
    uint8_t rbuf[4] = {0};
    struct SpiMsg msg = {0};
    msg.wbuf = wbuf;
    msg.rbuf = rbuf;
    msg.len = 4;
    msg.csChange = 0;
    msg.delayUs = 0;
    ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
        return;
    }
    uint8_t *wbuf1 = (uint8_t*)OsalMemAlloc(size);
    if (wbuf1 == NULL){
        HDF_LOGE("OsalMemAlloc failed.\n");
        return;
    }
    memset(wbuf1, 0xff, size);
    msg.wbuf = wbuf1;
    msg.rbuf = buf;
    msg.len = size;
    msg.csChange = 1;
    msg.delayUs = 0;
    ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
        return;
    }

    OsalMemFree(wbuf1);
}

static void SectorErase(DevHandle spiHandle)
{
    /* 发送FLASH写使能命令 */
    WriteEnable(spiHandle);
    WaitForWriteEnd(spiHandle);
    uint8_t wbuf[4] = {0x20, 0x00, 0x00, 0x00};
    uint8_t rbuf[4] = {0};
    struct SpiMsg msg = {0};
    msg.wbuf = wbuf;
    msg.rbuf = rbuf;
    msg.len = 4;
    msg.csChange = 1;
    msg.delayUs = 0;
    int32_t ret = SpiTransfer(spiHandle, &msg, 1);
    if (ret != 0) {
        HDF_LOGE("SpiTransfer: failed, ret %d\n", ret);
        return;
    }
    /* 等待擦除完毕*/
    WaitForWriteEnd(spiHandle);
}
#else
static uint16_t ReadDeviceId(DevHandle spiHandle)
{
    struct SpiMsg msg;                  /* 自定义传输的消息 */
    uint16_t deviceId = 0;
    uint8_t rbuff1[2] = { 0 };
    uint8_t wbuff1[5] = {0x00, 0xAB, 0xff,0xff, 0xff};
    int32_t ret = 0;

    /* 发送命令：读取芯片型号ID */
    ret =SpiWrite(spiHandle, wbuff1, 5);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }
    rbuff1[0] = 0x01;
    ret = SpiRead(spiHandle, rbuff1, 2);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }

    deviceId = rbuff1[1];

    return deviceId;
}

static uint16_t ReadFlashId(DevHandle spiHandle)
{
    int32_t ret = 0;
    uint16_t flashId = 0;
    uint8_t wbuff[2] = {0x00, 0x9f};
    uint8_t rbuff[4] = {0};
    ret =SpiWrite(spiHandle, wbuff, 2);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }

    rbuff[0] = 0x01;
    ret = SpiRead(spiHandle, rbuff, 4);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }

    flashId = rbuff[2] << 8 | rbuff[3];

    return flashId;
}

static void WaitForWriteEnd(DevHandle spiHandle)
{
    uint8_t FLASH_Status = 0;
    int32_t ret = 0;
    uint8_t wbuff[2] = {0x00, 0x05};
    uint8_t rbuff[2] = {0};
    ret =SpiWrite(spiHandle, wbuff, 2);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }
    do
    {
        rbuff[0] = 0;
        ret = SpiRead(spiHandle, rbuff, 2);
        if (ret != 0) {
            HDF_LOGE("SpiRead: failed, ret %d\n", ret);
        }
        FLASH_Status = rbuff[1];
    } while ((FLASH_Status & WIP_FLAG) == 1); /* Write in progress */

    uint8_t wbuff1[1] = {0x01};
    ret =SpiWrite(spiHandle, wbuff1, 1);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }

}
static void WriteEnable(DevHandle spiHandle)
{
    uint8_t FLASH_Status = 0;
    int32_t ret = 0;
    uint8_t wbuff[2] = {0x01, 0x06};
    ret =SpiWrite(spiHandle, wbuff, 2);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }

    return;
}

static void BufferWrite(DevHandle spiHandle, const uint8_t* buf, uint32_t size)
{
    WriteEnable(spiHandle);
    uint8_t wbuf[5] = {0x01, 0x02, 0x00, 0x00, 0x00};
    uint8_t rbuf[4] = {0};
    uint8_t *wbuf1 = NULL;
    int32_t ret = 0;
    wbuf1 = (uint8_t*)OsalMemAlloc(size + 5);

    strncpy(wbuf1, wbuf, 5);
    strncpy(wbuf1 + 5, buf, size);
    ret = SpiWrite(spiHandle, wbuf1, size + 5);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }

    WaitForWriteEnd(spiHandle);
    if (wbuf1!= NULL) {
        OsalMemFree(wbuf1);
    }

}

static void BufferRead(DevHandle spiHandle, uint8_t* buf, uint32_t size)
{
    WriteEnable(spiHandle);
    uint8_t wbuf[5] = {0x00, 0x03, 0x00, 0x00, 0x00};
    uint8_t *rbuf = NULL;
    int32_t ret = 0;
    rbuf = (uint8_t*)OsalMemAlloc(size + 1);

    ret = SpiWrite(spiHandle, wbuf, 5);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }
    rbuf[0] = 0x01;
    ret = SpiRead(spiHandle, rbuf, size + 1);
    if (ret != 0) {
        HDF_LOGE("SpiRead: failed, ret %d\n", ret);
    }

    strncpy(buf, rbuf + 1, size);

    if (rbuf!= NULL) {
        OsalMemFree(rbuf);
    }

    return;
}

static void SectorErase(DevHandle spiHandle)
{
    /* 发送FLASH写使能命令 */
    WriteEnable(spiHandle);
    WaitForWriteEnd(spiHandle);
    uint8_t wbuf[5] = {0x01, 0x20, 0x00, 0x00, 0x00};
    uint8_t rbuf[5] = {0};
    int32_t ret = 0;
    ret = SpiWrite(spiHandle, wbuf, 5);
    if (ret != 0) {
        HDF_LOGE("SpiWrite: failed, ret %d\n", ret);
    }
    /* 等待擦除完毕*/
    WaitForWriteEnd(spiHandle);
}

#endif

static void* HdfSpiTestEntry(void* arg)
{
#ifdef USE_SET_CFG
    int32_t ret;
    struct SpiCfg cfg;                  /* SPI配置信息 */
#endif
    uint16_t flashId = 0;
    uint16_t deviceId = 0;
    struct SpiDevInfo spiDevinfo;       /* SPI设备描述符 */
    DevHandle spiHandle;                /* SPI设备句柄 */
    spiDevinfo.busNum = 0;              /* SPI设备总线号 */
    spiDevinfo.csNum = 0;               /* SPI设备片选号 */
    spiHandle = SpiOpen(&spiDevinfo);   /* 根据spiDevinfo获取SPI设备句柄 */
    if (spiHandle == NULL) {
        HDF_LOGE("SpiOpen: failed\n");
        return;
    }
#ifdef USE_SET_CFG
    /* 获取SPI设备属性 */
    ret = SpiGetCfg(spiHandle, &cfg);
    if (ret != 0) {
        HDF_LOGE("SpiGetCfg: failed, ret %d\n", ret);
        goto err;
    }
    HDF_LOGI("speed:%d, bitper:%d, mode:%d, transMode:%d\n", cfg.maxSpeedHz, cfg.bitsPerWord, cfg.mode, cfg.transferMode);
    cfg.maxSpeedHz = 1;                /* spi2，spi3 最大频率为42M, spi1  频率为84M， 此处的值为分频系数，0:1/2 1:1/4， 2:1/8     . */
                                         /* 3: 1/16, 4: 1/32 5:1/64, 6:1/128  7:1/256 */
    cfg.bitsPerWord = 8;                    /* 传输位宽改为8比特 */
    cfg.mode = 0;
    cfg.transferMode = 1;              /* 0:dma  1:normal */
    /* 配置SPI设备属性 */
    ret = SpiSetCfg(spiHandle, &cfg);
    if (ret != 0) {
        HDF_LOGE("SpiSetCfg: failed, ret %d\n", ret);
        goto err;
    }
    SpiClose(spiHandle);
    spiHandle = SpiOpen(&spiDevinfo);   /* 根据spiDevinfo获取SPI设备句柄 */
    if (spiHandle == NULL) {
        HDF_LOGE("SpiOpen: failed\n");
        return;
    }
#endif
    /* 向SPI设备写入指定长度的数据 */
    deviceId = ReadDeviceId(spiHandle);
    HDF_LOGI("read device id is 0x%02x\n", deviceId);
    flashId = ReadFlashId(spiHandle);
    HDF_LOGI("read flash id is 0x%02x\n", flashId);

    if (flashId == SPI_FLASH_IDx)  /* #define  sFLASH_ID  0XEF4018 */
    {
        SectorErase(spiHandle);
        BufferWrite(spiHandle, txBuffer, bufferSize);
        HDF_LOGI("send buffer is %s\n", txBuffer);
        BufferRead(spiHandle, rxBuffer, bufferSize);
        HDF_LOGI("recv send buffer is %s\n", rxBuffer);

        if (BufferCmp(txBuffer, rxBuffer, bufferSize)) {
            HDF_LOGI("hdf spi write read flash success\n");
        } else {
            HDF_LOGI("hdf spi write read flash failed\n");
        }
    }
#ifdef USE_SET_CFG
err:
#endif
    /* 销毁SPI设备句柄 */
    SpiClose(spiHandle);

}

void StartHdfSpiTest(void)
{
    UINT32 uwRet;
    UINT32 taskID;
    TSK_INIT_PARAM_S stTask = {0};

    stTask.pfnTaskEntry = (TSK_ENTRY_FUNC)HdfSpiTestEntry;
    stTask.uwStackSize = HDF_SPI_STACK_SIZE;
    stTask.pcName = HDF_SPI_TASK_NAME;
    stTask.usTaskPrio = HDF_SPI_TASK_PRIORITY; /* Os task priority is 2 */
    uwRet = LOS_TaskCreate(&taskID, &stTask);
    if (uwRet != LOS_OK) {
        printf("Task1 create failed\n");
    }
}

OHOS_APP_RUN(StartHdfSpiTest);
