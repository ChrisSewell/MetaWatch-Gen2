//==============================================================================
//  Copyright 2011 Meta Watch Ltd. - http://www.MetaWatch.org/
// 
//  Licensed under the Meta Watch License, Version 1.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//  
//      http://www.MetaWatch.org/licenses/license-1.0.html
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//==============================================================================

/******************************************************************************/
/*! \file BufferPool.c
*
* This file does not conform to the coding standard.
*/
/******************************************************************************/

#include "FreeRTOS.h"
#include "queue.h"         
#include "task.h"

#include "hal_board_type.h"
#include "hal_lpm.h"

#include "Messages.h"
#include "MessageQueues.h"
#include "BufferPool.h" 
#include "Statistics.h"
#include "DebugUart.h"
#include "IdleTask.h"

/*! \param buffer is a buffer of HOST_MSG_BUFFER_LENGTH whose address goes
 * onto a queue
*/
typedef struct
{
  /* ~hack
   * Reserve header bytes to avoid mem-copy in _HostMsg_write
   */
  unsigned char header[HOST_MSG_HEADER_LENGTH];
  unsigned char buffer[HOST_MSG_BUFFER_LENGTH - HOST_MSG_HEADER_LENGTH];  

} tBufferPoolBuffer;    


/*! number of buffers in the message buffer pool */
#define NUM_MSG_BUFFERS 15 //20

// This memory is never accessed directly, pointers to the buffers are put on
// a queue at startup
static tBufferPoolBuffer BufferPool[NUM_MSG_BUFFERS];

// all pointers on/off the queue must be in this range, use these to check the
// buffer free to make sure we don't trash the pool
static const unsigned char* LOW_BUFFER_ADDRESS   = &(BufferPool[0].buffer[0]);
static const unsigned char* HIGH_BUFFER_ADDRESS  = &(BufferPool[NUM_MSG_BUFFERS - 1].buffer[0]);

static void SetBufferPoolFailureBit(void)
{
  PrintString2("@ BufPool Fail", CR);
  ForceWatchdogReset();  
}

void InitializeBufferPool( void )
{
  unsigned int  i;              // loop counter
  unsigned char* pMsgBuffer;    // holds the address of the msg buffer to add to the queue
  
  // create the queue to hold the free message buffers
  QueueHandles[FREE_QINDEX] =
    xQueueCreate( ( unsigned portBASE_TYPE ) NUM_MSG_BUFFERS,
                  ( unsigned portBASE_TYPE ) sizeof(unsigned char*) );
  
  // Add the address of each buffer's data section to the queue
  for (i = 0; i < NUM_MSG_BUFFERS; i++)
  {
    // use the address of the data section so that the header is only accessed
    // as needed
    pMsgBuffer = &(BufferPool[i].buffer[0]);

    // We only call this at initialization so it should never fail
    // params: queue handle, ptr to item to queue, ticks to wait if full
    if ( xQueueSend( QueueHandles[FREE_QINDEX], &pMsgBuffer, DONT_WAIT ) != pdTRUE )
    {
      PrintString2("@ Build Free Queue", CR);
      SetBufferPoolFailureBit();
    }
  }
}

unsigned char* BPL_AllocMessageBuffer(void)
{
  unsigned char * pBuffer = NULL;

  // params are: queue handle, ptr to the msg buffer, ticks to wait
  if (pdTRUE != xQueueReceive(QueueHandles[FREE_QINDEX], &pBuffer, DONT_WAIT))
  {
    PrintString2("@ No MsgBuf", CR);
    SetBufferPoolFailureBit();

  }
  else if (pBuffer < LOW_BUFFER_ADDRESS || pBuffer > HIGH_BUFFER_ADDRESS)
  {
    PrintString2("@ Invalid MsgBuf", CR);
    SetBufferPoolFailureBit();
  }

  return pBuffer;
}

void BPL_FreeMessageBuffer(unsigned char* pBuffer)
{
  // make sure the returned pointer is in range
  if (pBuffer < LOW_BUFFER_ADDRESS || pBuffer > HIGH_BUFFER_ADDRESS)
  {
    PrintString2("@ Free Buffer Corrupt", CR);
    SetBufferPoolFailureBit();
  }

  // params are: queue handle, ptr to item to queue, tick to wait if full
  // the queue can't be full unless there is a bug, so don't wait on full
  if(pdTRUE != xQueueSend(QueueHandles[FREE_QINDEX], &pBuffer, DONT_WAIT))
  {
    PrintString2("@ add buf to que", CR);
    SetBufferPoolFailureBit();
  }
}

void BPL_FreeMessageBufferFromIsr(unsigned char* pBuffer)
{
  // make sure the returned pointer is in range
  if (pBuffer < LOW_BUFFER_ADDRESS || pBuffer > HIGH_BUFFER_ADDRESS)
  {
    PrintString2("@ Free Buffer ISR Corrupt", CR);
    SetBufferPoolFailureBit();
  }

  signed portBASE_TYPE HigherPriorityTaskWoken;
  
  // params are: queue handle, ptr to item to queue, tick to wait if full
  // the queue can't be full unless there is a bug, so don't wait on full
  if(pdTRUE != xQueueSendFromISR(QueueHandles[FREE_QINDEX], &pBuffer, &HigherPriorityTaskWoken))
  {
    PrintString2("@ add buf to que", CR);
    SetBufferPoolFailureBit();
  }

#if 0
  /* this should never be true when freeing a message buffer */
  if ( HigherPriorityTaskWoken == pdTRUE )
  {
    portYIELD();
  }
#endif
  
}

#if 0
/* This is too slow to use */
unsigned char* BPL_AllocMessageBufferFromISR(void)
{  
  unsigned char * pBuffer = NULL;

  signed portBASE_TYPE HigherPriorityTaskWoken;
  
  // params are: queue handle, ptr to the msg buffer, ticks to wait
  if( pdTRUE != xQueueReceiveFromISR(QueueHandles[FREE_QINDEX], 
                                     &pBuffer, 
                                     &HigherPriorityTaskWoken ))
  {
    PrintString2("@ Alloc Buf frm Isr", CR);
    SetBufferPoolFailureBit();
  }
  
  if ( HigherPriorityTaskWoken == pdTRUE )
  {
    portYIELD();
  }
  
  return pBuffer;
}
#endif

