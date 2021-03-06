/*****************************************************************************
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The following copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2010-2011, Citrix, Inc.
 *
 * Ported from lis21 code drop
 *
 * HyperV vmbus ring buffer code
 *
 *****************************************************************************/

/*
 * Copyright (c) 2009, Microsoft Corporation - All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */

/* Fixme:  Added these includes to get memset, printf */
#include <sys/param.h>
//#include <sys/systm.h>
//#include <sys/sockio.h>
#include <sys/mbuf.h>
//#include <sys/malloc.h>
//#include <sys/module.h>
//#include <sys/kernel.h>
//#include <sys/socket.h>
//#include <sys/queue.h>
//#include <sys/lock.h>
//#include <sys/sx.h>

//#include <net/if.h>
//#include <net/if_arp.h>



#ifdef REMOVED
/* Fixme:  Removed */
#include "logging.h"
#include "RingBuffer.h"
#endif

/* Fixme:  Not all these are likely needed */
#include <dev/hyperv/include/hv_osd.h>
#include <dev/hyperv/include/hv_logging.h>
#include "hv_hv.h"
#include "hv_vmbus_var.h"
#include "hv_vmbus_api.h"
#include <dev/hyperv/include/hv_list.h>
#include "hv_ring_buffer.h"
#include <dev/hyperv/include/hv_vmbus_channel_interface.h>
#include <dev/hyperv/include/hv_vmbus_packet_format.h>
#include <dev/hyperv/include/hv_channel_messages.h>
#include "hv_channel_mgmt.h"
#include "hv_channel.h"
#include "hv_channel_interface.h"
#include "hv_ic.h"
// Fixme:  need this?  Was in hv_vmbus_private.h
#include "hv_timesync_ic.h"
#include "hv_vmbus_private.h"


//
// #defines
//

// Amount of space to write to
#define BYTES_AVAIL_TO_WRITE(r, w, z) ((w) >= (r))?((z) - ((w) - (r))):((r) - (w)) 


/*++

Name: 
	GetRingBufferAvailBytes()

Description:
	Get number of bytes available to read and to write to
	for the specified ring buffer

--*/
static inline void 
GetRingBufferAvailBytes(RING_BUFFER_INFO *rbi, UINT32 *read, UINT32 *write)
{
	UINT32 read_loc,write_loc;

	// Capture the read/write indices before they changed
	read_loc = rbi->RingBuffer->ReadIndex;
	write_loc = rbi->RingBuffer->WriteIndex;

	*write = BYTES_AVAIL_TO_WRITE(read_loc, write_loc, rbi->RingDataSize);
	*read = rbi->RingDataSize - *write;
}

/*++

Name: 
	GetNextWriteLocation()

Description:
	Get the next write location for the specified ring buffer

--*/
static inline UINT32
GetNextWriteLocation(RING_BUFFER_INFO* RingInfo)
{
	UINT32 next = RingInfo->RingBuffer->WriteIndex;

	ASSERT(next < RingInfo->RingDataSize);

	return next;
}

/*++

Name: 
	SetNextWriteLocation()

Description:
	Set the next write location for the specified ring buffer

--*/
static inline void 
SetNextWriteLocation(RING_BUFFER_INFO* RingInfo, UINT32 NextWriteLocation)
{
	RingInfo->RingBuffer->WriteIndex = NextWriteLocation;
}

/*++

Name: 
	GetNextReadLocation()

Description:
	Get the next read location for the specified ring buffer

--*/
static inline UINT32
GetNextReadLocation(RING_BUFFER_INFO* RingInfo)
{
	UINT32 next = RingInfo->RingBuffer->ReadIndex;

	ASSERT(next < RingInfo->RingDataSize);

	return next;
}

/*++

Name: 
	GetNextReadLocationWithOffset()

Description:
	Get the next read location + offset for the specified ring buffer.
	This allows the caller to skip

--*/
static inline UINT32
GetNextReadLocationWithOffset(RING_BUFFER_INFO* RingInfo, UINT32 Offset)
{
	UINT32 next = RingInfo->RingBuffer->ReadIndex;
	
	ASSERT(next < RingInfo->RingDataSize);
	next += Offset;
	next %= RingInfo->RingDataSize;
	
	return next;
}

/*++

Name: 
	SetNextReadLocation()

Description:
	Set the next read location for the specified ring buffer

--*/
static inline void 
SetNextReadLocation(RING_BUFFER_INFO* RingInfo, UINT32 NextReadLocation)
{
	RingInfo->RingBuffer->ReadIndex = NextReadLocation;
}


/*++

Name: 
	GetRingBuffer()

Description:
	Get the start of the ring buffer

--*/
static inline PVOID
GetRingBuffer(RING_BUFFER_INFO* RingInfo)
{
	return (PVOID)RingInfo->RingBuffer->Buffer;
}


/*++

Name: 
	GetRingBufferSize()

Description:
	Get the size of the ring buffer

--*/
static inline UINT32
GetRingBufferSize(RING_BUFFER_INFO* RingInfo)
{
	return RingInfo->RingDataSize;
}

/*++

Name: 
	GetRingBufferIndices()

Description:
	Get the read and write indices as UINT64 of the specified ring buffer

--*/
static inline UINT64
GetRingBufferIndices(RING_BUFFER_INFO* RingInfo)
{
	return (UINT64)RingInfo->RingBuffer->WriteIndex << 32;
}


/*++

Name: 
	DumpRingInfo()

Description:
	Dump out to console the ring buffer info

--*/
void
DumpRingInfo(RING_BUFFER_INFO* RingInfo, char *Prefix)
{
	UINT32 bytesAvailToWrite;
	UINT32 bytesAvailToRead;
	
	GetRingBufferAvailBytes(RingInfo, &bytesAvailToRead, &bytesAvailToWrite);

	DPRINT(VMBUS, DEBUG_RING_LVL, "%s <<ringinfo %p buffer %p avail write %u avail read %u read idx %u write idx %u>>",
		Prefix,
		RingInfo,
		RingInfo->RingBuffer->Buffer,
		bytesAvailToWrite,
		bytesAvailToRead,
		RingInfo->RingBuffer->ReadIndex,
		RingInfo->RingBuffer->WriteIndex);
}

//
// Internal routines
//
static UINT32
CopyToRingBuffer(
	RING_BUFFER_INFO	*RingInfo,	
	UINT32				StartWriteOffset, 
	PVOID				Src, 
	UINT32				SrcLen);

static UINT32
CopyFromRingBuffer(
	RING_BUFFER_INFO	*RingInfo,	
	PVOID				Dest, 
	UINT32				DestLen, 
	UINT32				StartReadOffset);



/*++

Name: 
	RingBufferGetDebugInfo()

Description:
	Get various debug metrics for the specified ring buffer

--*/
void
RingBufferGetDebugInfo(
	RING_BUFFER_INFO		*RingInfo,
	RING_BUFFER_DEBUG_INFO	*DebugInfo
	)
{
	UINT32 bytesAvailToWrite;
	UINT32 bytesAvailToRead;
	
	if (RingInfo->RingBuffer)
	{
		GetRingBufferAvailBytes(RingInfo, &bytesAvailToRead, &bytesAvailToWrite);

		DebugInfo->BytesAvailToRead = bytesAvailToRead;
		DebugInfo->BytesAvailToWrite = bytesAvailToWrite;
		DebugInfo->CurrentReadIndex = RingInfo->RingBuffer->ReadIndex;
		DebugInfo->CurrentWriteIndex = RingInfo->RingBuffer->WriteIndex;

		DebugInfo->CurrentInterruptMask = RingInfo->RingBuffer->InterruptMask;
	}
}


/*++

Name: 
	GetRingBufferInterruptMask()

Description:
	Get the interrupt mask for the specified ring buffer

--*/
UINT32 
GetRingBufferInterruptMask(
	RING_BUFFER_INFO *rbi
	)
{
	return rbi->RingBuffer->InterruptMask;
}

/*++

Name: 
	RingBufferInit()

Description:
	Initialize the ring buffer

--*/
int
RingBufferInit(
	RING_BUFFER_INFO	*RingInfo,
	VOID				*Buffer,
	UINT32				BufferLen
	)
{
	ASSERT(sizeof(RING_BUFFER) == PAGE_SIZE);

	memset(RingInfo, 0, sizeof(RING_BUFFER_INFO));

	RingInfo->RingBuffer = (RING_BUFFER*)Buffer;
	RingInfo->RingBuffer->ReadIndex = RingInfo->RingBuffer->WriteIndex = 0;

	RingInfo->RingSize = BufferLen;
	RingInfo->RingDataSize = BufferLen - sizeof(RING_BUFFER);

	RingInfo->RingLock = SpinlockCreate();

	return 0;
}

/*++

Name: 
	RingBufferCleanup()

Description:
	Cleanup the ring buffer

--*/
void
RingBufferCleanup(
	RING_BUFFER_INFO* RingInfo
	)
{
	SpinlockClose(RingInfo->RingLock);
}

/*++

Name: 
	RingBufferWrite()

Description:
	Write to the ring buffer

--*/
int
RingBufferWrite(
	RING_BUFFER_INFO*	OutRingInfo,
	SG_BUFFER_LIST		SgBuffers[],
	UINT32				SgBufferCount
	)
{
	int i=0;
	UINT32 byteAvailToWrite;
	UINT32 byteAvailToRead;
	UINT32 totalBytesToWrite=0;

	volatile UINT32 nextWriteLocation;
	UINT64 prevIndices=0;

	DPRINT_ENTER(VMBUS);

	for (i=0; i < SgBufferCount; i++)
	{
		totalBytesToWrite += SgBuffers[i].Length;
	}

	totalBytesToWrite += sizeof(UINT64);

	SpinlockAcquire(OutRingInfo->RingLock);

	GetRingBufferAvailBytes(OutRingInfo, &byteAvailToRead, &byteAvailToWrite);
	
	DPRINT_DBG(VMBUS, "Writing %u bytes...", totalBytesToWrite);

	//DumpRingInfo(OutRingInfo, "BEFORE ");
	
	// If there is only room for the packet, assume it is full. Otherwise, the next time around, we think the ring buffer
	// is empty since the read index == write index
	if (byteAvailToWrite <= totalBytesToWrite)
	{
		DPRINT_DBG(VMBUS, "No more space left on outbound ring buffer (needed %u, avail %u)", totalBytesToWrite, byteAvailToWrite);

		SpinlockRelease(OutRingInfo->RingLock);

		DPRINT_EXIT(VMBUS);

		return -1;
	}

	// Write to the ring buffer
	nextWriteLocation = GetNextWriteLocation(OutRingInfo);

	for (i=0; i < SgBufferCount; i++)
	{
		 nextWriteLocation = CopyToRingBuffer(OutRingInfo,
												nextWriteLocation,
												SgBuffers[i].Data,
												SgBuffers[i].Length);
	}

	// Set previous packet start
	prevIndices = GetRingBufferIndices(OutRingInfo);

	nextWriteLocation = CopyToRingBuffer(OutRingInfo,
												nextWriteLocation,
												&prevIndices,
												sizeof(UINT64));

	// Make sure we flush all writes before updating the writeIndex
	MemoryFence();

	// Now, update the write location
	SetNextWriteLocation(OutRingInfo, nextWriteLocation);

	//DumpRingInfo(OutRingInfo, "AFTER ");

	SpinlockRelease(OutRingInfo->RingLock);
		
	DPRINT_EXIT(VMBUS);

	return 0;
}


/*++

Name: 
	RingBufferPeek()

Description:
	Read without advancing the read index

--*/
int
RingBufferPeek(
	RING_BUFFER_INFO*	InRingInfo,
	void*				Buffer,
	UINT32				BufferLen
	)
{
	UINT32 bytesAvailToWrite;
	UINT32 bytesAvailToRead;
	UINT32 nextReadLocation=0;

	SpinlockAcquire(InRingInfo->RingLock);

	GetRingBufferAvailBytes(InRingInfo, &bytesAvailToRead, &bytesAvailToWrite);

	// Make sure there is something to read
	if (bytesAvailToRead < BufferLen )
	{
		//DPRINT_DBG(VMBUS, "got callback but not enough to read <avail to read %d read size %d>!!", bytesAvailToRead, BufferLen);

		SpinlockRelease(InRingInfo->RingLock);

		return -1;
	}

	// Convert to byte offset
	nextReadLocation = GetNextReadLocation(InRingInfo);

	nextReadLocation = CopyFromRingBuffer(InRingInfo,
											Buffer,
											BufferLen,
											nextReadLocation);

	SpinlockRelease(InRingInfo->RingLock);

	return 0;
}


/*++

Name: 
	RingBufferRead()

Description:
	Read and advance the read index

--*/
int
RingBufferRead(
	RING_BUFFER_INFO*	InRingInfo,
	PVOID				Buffer,
	UINT32				BufferLen,
	UINT32				Offset
	)
{
	UINT32 bytesAvailToWrite;
	UINT32 bytesAvailToRead;
	UINT32 nextReadLocation=0;
	UINT64 prevIndices=0;

	ASSERT(BufferLen > 0);
	
	SpinlockAcquire(InRingInfo->RingLock);

	GetRingBufferAvailBytes(InRingInfo, &bytesAvailToRead, &bytesAvailToWrite);

	DPRINT_DBG(VMBUS, "Reading %u bytes...", BufferLen);

	//DumpRingInfo(InRingInfo, "BEFORE ");

	// Make sure there is something to read
	if (bytesAvailToRead < BufferLen )
	{
		DPRINT_DBG(VMBUS, "got callback but not enough to read <avail to read %d read size %d>!!", bytesAvailToRead, BufferLen);

		SpinlockRelease(InRingInfo->RingLock);

		return -1;
	}

	nextReadLocation = GetNextReadLocationWithOffset(InRingInfo, Offset);

	nextReadLocation = CopyFromRingBuffer(InRingInfo,
											Buffer,
											BufferLen,
											nextReadLocation);

	nextReadLocation = CopyFromRingBuffer(InRingInfo,
											&prevIndices,
											sizeof(UINT64),
											nextReadLocation);

	// Make sure all reads are done before we update the read index since 
	// the writer may start writing to the read area once the read index is updated
	MemoryFence();

	// Update the read index
	SetNextReadLocation(InRingInfo, nextReadLocation);
	
	//DumpRingInfo(InRingInfo, "AFTER ");

	SpinlockRelease(InRingInfo->RingLock);

	return 0;
}


/*++

Name: 
	CopyToRingBuffer()

Description:
	Helper routine to copy from source to ring buffer.
	Assume there is enough room. Handles wrap-around in dest case only!!

--*/
UINT32
CopyToRingBuffer(
	RING_BUFFER_INFO	*RingInfo,	
	UINT32				StartWriteOffset, 
	PVOID				Src, 
	UINT32				SrcLen)
{
	/* Fixme:  This should not be a void pointer! */
	PVOID ringBuffer=GetRingBuffer(RingInfo); 
	UINT32 ringBufferSize=GetRingBufferSize(RingInfo); 
	UINT32 fragLen;

	if (SrcLen > ringBufferSize - StartWriteOffset) // wrap-around detected!
	{
		DPRINT_DBG(VMBUS, "wrap-around detected!");

		fragLen = ringBufferSize - StartWriteOffset;
		/* Fixme:  Cast needed due to void pointer */
		memcpy((UCHAR *)ringBuffer + StartWriteOffset, Src, fragLen);
		/* Fixme:  Cast needed due to void pointer */
		memcpy(ringBuffer, (UCHAR *)Src + fragLen, SrcLen - fragLen);
	}
	else
	{
		/* Fixme:  Cast needed due to void pointer */
		memcpy((unsigned char *)ringBuffer + StartWriteOffset, Src, SrcLen);
	}

	StartWriteOffset += SrcLen;
	StartWriteOffset %= ringBufferSize;

	return StartWriteOffset;
}


/*++

Name: 
	CopyFromRingBuffer()

Description:
	Helper routine to copy to source from ring buffer.
	Assume there is enough room. Handles wrap-around in src case only!!

--*/
UINT32
CopyFromRingBuffer(
	RING_BUFFER_INFO	*RingInfo,	
	PVOID				Dest, 
	UINT32				DestLen, 
	UINT32				StartReadOffset)
{
	/* Fixme:  This should not be a void pointer! */
	PVOID ringBuffer=GetRingBuffer(RingInfo);
	UINT32 ringBufferSize=GetRingBufferSize(RingInfo);

	UINT32 fragLen;

	if (DestLen > ringBufferSize - StartReadOffset) // wrap-around detected at the src
	{
		DPRINT_DBG(VMBUS, "src wrap-around detected!");

		fragLen = ringBufferSize - StartReadOffset;

		/* Fixme:  Cast needed due to void pointer */
		memcpy(Dest, (UCHAR *)ringBuffer + StartReadOffset, fragLen);
		/* Fixme:  Cast needed due to void pointer */
		memcpy((UCHAR *)Dest + fragLen, ringBuffer, DestLen - fragLen);
	}
	else
	{
		/* Fixme:  Cast needed due to void pointer */
		memcpy(Dest, (UCHAR *)ringBuffer + StartReadOffset, DestLen);
	}

	StartReadOffset += DestLen;
	StartReadOffset %= ringBufferSize;

	return StartReadOffset;
}

/*
 * Fixme:  originally for NetScaler.  Do we need these functions now?
 *
 * All functions below added for HyperV porting effort.
 */

void 
SetRingBufferInterruptMask(RING_BUFFER_INFO *rbi)
{
	rbi->RingBuffer->InterruptMask = 1;
}

void 
ClearRingBufferInterruptMask(RING_BUFFER_INFO *rbi)
{
	rbi->RingBuffer->InterruptMask = 0;
}

#define _PREFETCHT0(addr) \
	__extension__ ({ \
		__asm__ __volatile__ ("prefetcht0 (%0)\n" \
		: : "g" ((void *)(addr)) ); \
	})

int 
RingBufferCheck(RING_BUFFER_INFO *rbi)
{
#if 1
	UINT32 ri, wi, len;

	// Capture the read/write indices before they changed
        ri = rbi->RingBuffer->ReadIndex;
        wi = rbi->RingBuffer->WriteIndex;

	len = (ri <= wi) ? (wi - ri) : (rbi->RingDataSize - (ri - wi));

	if (len < sizeof(VMPACKET_DESCRIPTOR))
		return 0;

	_PREFETCHT0(rbi->RingBuffer->Buffer + ri);
#if 0
	if (ri <= wi) {
		/* no wrap */
		while (len > 0) {
			_PREFETCHT0(rbi->RingBuffer->Buffer + ri);
			len -= 128;
			ri += 128;
		}
	} else {
		/* wrap */
		while (ri < rbi->RingDataSize) {
			_PREFETCHT0(rbi->RingBuffer->Buffer + ri);
			len -= 128;
			ri += 128;
		}
		ri = 0;
		while (len > 0) {
			_PREFETCHT0(rbi->RingBuffer->Buffer + ri);
			len -= 128;
			ri += 128;
		}
	}
#endif

	return 1;


#else
	UINT32 toRead = 0;
	UINT32 toWrite;
	UINT32 addr;
	int i;

	GetRingBufferAvailBytes(rbi, &toRead, &toWrite);

	if (toRead < sizeof(VMPACKET_DESCRIPTOR))
		return 0;
	else {
		r = rbi->Ringbuffer;
		rdindx =  r->ReadIndex;

		addr = r->Buffer + rdindx;
		toRead += (addr & 0x~07f);
		addr &= ~0x7f;

		if (toRead > r->size - rdindx) {
			end = buf + size;
			for(;addr < end; ) {
				_PREFETCHT0(addr & ~0x7f);
				addr += 128;
				
			}
		} else {
			_PREFETCH0(addr & ~0x7f);
		}
		return 1;
	}
#endif

}

// eof
