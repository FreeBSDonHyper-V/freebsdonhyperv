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
 * HyperV RNDIS filter code
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

/* Fixme:  Added these includes to get struct arpcom definition */
#include <sys/param.h>
//#include <sys/systm.h>
//#include <sys/sockio.h>
#include <sys/mbuf.h>
//#include <sys/malloc.h>
//#include <sys/module.h>
//#include <sys/kernel.h>
#include <sys/socket.h>
//#include <sys/queue.h>
//#include <sys/lock.h>
//#include <sys/sx.h>

//#include <net/if.h>
#include <net/if_arp.h>


#ifdef REMOVED
/* Fixme:  Removed */
#include "logging.h"
#endif
#include <dev/hyperv/include/hv_osd.h>
#include <dev/hyperv/include/hv_logging.h>

#ifdef REMOVED
/* Fixme:  Removed */
  #include "VmbusPacketFormat.h"
    #include <VmbusChannelInterface.h>
  #include "nvspprotocol.h"
    #include "osd.h"
  #include "List.h"
      #include "osd.h"
    #include "vmbusvar.h"
  #include "NetVscApi.h"
//#include "NetVsc.h"
#endif
#include <dev/hyperv/include/hv_list.h>
#include <dev/hyperv/include/hv_vmbus_channel_interface.h>
#include <dev/hyperv/include/hv_vmbus_packet_format.h>
#include <dev/hyperv/include/hv_nvsp_protocol.h>
#include <dev/hyperv/vmbus/hv_vmbus_var.h>
#include <dev/hyperv/include/hv_net_vsc_api.h>
#include <dev/hyperv/include/hv_net_vsc.h>

#ifdef REMOVED
  #include "osd.h"
  #include "NetVsc.h"
  #include "rndis.h"
#include "RndisFilter.h"
#endif
#include <dev/hyperv/include/hv_rndis_filter.h>
#include <dev/hyperv/include/hv_rndis.h>


//
// Data types
//

typedef struct _RNDIS_FILTER_DRIVER_OBJECT {
	// The original driver
	NETVSC_DRIVER_OBJECT		InnerDriver;

} RNDIS_FILTER_DRIVER_OBJECT;

typedef enum {
	RNDIS_DEV_UNINITIALIZED = 0,
	RNDIS_DEV_INITIALIZING,
	RNDIS_DEV_INITIALIZED,
	RNDIS_DEV_DATAINITIALIZED,
} RNDIS_DEVICE_STATE;

typedef struct _RNDIS_DEVICE {
	NETVSC_DEVICE			*NetDevice;

	RNDIS_DEVICE_STATE		State;
	UINT32					LinkStatus;
	UINT32					NewRequestId;

	HANDLE					RequestLock;
	LIST_ENTRY				RequestList;

	UCHAR					HwMacAddr[HW_MACADDR_LEN];
} RNDIS_DEVICE;


typedef struct _RNDIS_REQUEST {
	LIST_ENTRY					ListEntry;
	HANDLE						WaitEvent;	

	// FIXME: We assumed a fixed size response here. If we do ever need to handle a bigger response,
	// we can either define a max response message or add a response buffer variable above this field
	RNDIS_MESSAGE				ResponseMessage;

	// Simplify allocation by having a netvsc packet inline
	NETVSC_PACKET				Packet;
	PAGE_BUFFER					Buffer;
	// FIXME: We assumed a fixed size request here.
	RNDIS_MESSAGE				RequestMessage;
} RNDIS_REQUEST;


typedef struct _RNDIS_FILTER_PACKET {
	void						*CompletionContext;
	PFN_ON_SENDRECVCOMPLETION	OnCompletion;

	RNDIS_MESSAGE				Message;
} RNDIS_FILTER_PACKET;

//
// Internal routines
//
static int
RndisFilterSendRequest(
	RNDIS_DEVICE	*Device,
	RNDIS_REQUEST	*Request
	);

static void 
RndisFilterReceiveResponse(
	RNDIS_DEVICE	*Device,
	RNDIS_MESSAGE	*Response
	);

static void 
RndisFilterReceiveIndicateStatus(
	RNDIS_DEVICE	*Device,
	RNDIS_MESSAGE	*Response
	);

static void
RndisFilterReceiveData(
	RNDIS_DEVICE	*Device,
	RNDIS_MESSAGE	*Message,
	NETVSC_PACKET	*Packet
	);

static int
RndisFilterOnReceive(
	DEVICE_OBJECT		*Device,
	NETVSC_PACKET		*Packet
	);

static int
RndisFilterQueryDevice(
	RNDIS_DEVICE	*Device,
	UINT32			Oid,
	VOID			*Result,
	UINT32			*ResultSize
	);

static inline int
RndisFilterQueryDeviceMac(
	RNDIS_DEVICE	*Device
	);

static inline int
RndisFilterQueryDeviceLinkStatus(
	RNDIS_DEVICE	*Device
	);

static int
RndisFilterSetPacketFilter(
	RNDIS_DEVICE	*Device,
	UINT32			NewFilter
	);

static int
RndisFilterInitDevice(
	RNDIS_DEVICE		*Device
	);

static int
RndisFilterOpenDevice(
	RNDIS_DEVICE		*Device
	);

static int
RndisFilterCloseDevice(
	RNDIS_DEVICE		*Device
	);

static int
RndisFilterOnDeviceAdd(
	DEVICE_OBJECT	*Device,
	void			*AdditionalInfo
	);

static int
RndisFilterOnDeviceRemove(
	DEVICE_OBJECT *Device
	);

static void
RndisFilterOnCleanup(
	DRIVER_OBJECT *Driver
	);

static int 
RndisFilterOnOpen(
	DEVICE_OBJECT		*Device
	);

static int 
RndisFilterOnClose(
	DEVICE_OBJECT		*Device
	);

static int 
RndisFilterOnSend(
	DEVICE_OBJECT		*Device,
	NETVSC_PACKET		*Packet
	);

static void 
RndisFilterOnSendCompletion(
   void *Context
	);

static void 
RndisFilterOnSendRequestCompletion(
   void *Context
	);

//
// Global var
//

// The one and only
RNDIS_FILTER_DRIVER_OBJECT gRndisFilter;

/*
 * Allow module_param to work and override to switch to promiscuous mode.
 */
static inline RNDIS_DEVICE* GetRndisDevice(void)
{
	RNDIS_DEVICE *device;

	device = MemAllocZeroed(sizeof(RNDIS_DEVICE));
	if (!device)
	{
		return NULL;
	}

	device->RequestLock = SpinlockCreate();
	if (!device->RequestLock)
	{
		MemFree(device);
		return NULL;
	}

	INITIALIZE_LIST_HEAD(&device->RequestList);

	device->State = RNDIS_DEV_UNINITIALIZED;

	return device;
}

static inline void PutRndisDevice(RNDIS_DEVICE *Device)
{
	SpinlockClose(Device->RequestLock);
	MemFree(Device);
}

static inline RNDIS_REQUEST* GetRndisRequest(RNDIS_DEVICE *Device, UINT32 MessageType, UINT32 MessageLength)
{
	RNDIS_REQUEST *request;
	RNDIS_MESSAGE *rndisMessage;
	RNDIS_SET_REQUEST *set;

	request = MemAllocZeroed(sizeof(RNDIS_REQUEST));
	if (!request)
	{
		return NULL;
	}

	request->WaitEvent = WaitEventCreate();
	if (!request->WaitEvent)
	{
		MemFree(request);
		return NULL;
	}
	
	rndisMessage = &request->RequestMessage;
	rndisMessage->NdisMessageType = MessageType;
	rndisMessage->MessageLength = MessageLength;

	// Set the request id. This field is always after the rndis header for request/response packet types so
	// we just used the SetRequest as a template
	set = &rndisMessage->Message.SetRequest;
	set->RequestId = InterlockedIncrement((int*)&Device->NewRequestId);

	// Add to the request list
	SpinlockAcquire(Device->RequestLock);
	INSERT_TAIL_LIST(&Device->RequestList, &request->ListEntry);
	SpinlockRelease(Device->RequestLock);

	return request;
}

static inline void PutRndisRequest(RNDIS_DEVICE *Device, RNDIS_REQUEST *Request)
{
	SpinlockAcquire(Device->RequestLock);
	REMOVE_ENTRY_LIST(&Request->ListEntry);
	SpinlockRelease(Device->RequestLock);

	WaitEventClose(Request->WaitEvent);
	MemFree(Request);
}

static inline void DumpRndisMessage(RNDIS_MESSAGE *RndisMessage)
{
	switch (RndisMessage->NdisMessageType)
	{
	case REMOTE_NDIS_PACKET_MSG:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_PACKET_MSG (len %u, data offset %u data len %u, # oob %u, oob offset %u, oob len %u, pkt offset %u, pkt len %u", 
			RndisMessage->MessageLength,
			RndisMessage->Message.Packet.DataOffset,
			RndisMessage->Message.Packet.DataLength,
			RndisMessage->Message.Packet.NumOOBDataElements,
			RndisMessage->Message.Packet.OOBDataOffset,
			RndisMessage->Message.Packet.OOBDataLength,
			RndisMessage->Message.Packet.PerPacketInfoOffset,
			RndisMessage->Message.Packet.PerPacketInfoLength);
		break;

	case REMOTE_NDIS_INITIALIZE_CMPLT:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_INITIALIZE_CMPLT (len %u, id 0x%x, status 0x%x, major %d, minor %d, device flags %d, max xfer size 0x%x, max pkts %u, pkt aligned %u)", 
			RndisMessage->MessageLength,
			RndisMessage->Message.InitializeComplete.RequestId,
			RndisMessage->Message.InitializeComplete.Status,
			RndisMessage->Message.InitializeComplete.MajorVersion,
			RndisMessage->Message.InitializeComplete.MinorVersion,
			RndisMessage->Message.InitializeComplete.DeviceFlags,
			RndisMessage->Message.InitializeComplete.MaxTransferSize,
			RndisMessage->Message.InitializeComplete.MaxPacketsPerMessage,
			RndisMessage->Message.InitializeComplete.PacketAlignmentFactor);
		break;

	case REMOTE_NDIS_QUERY_CMPLT:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_QUERY_CMPLT (len %u, id 0x%x, status 0x%x, buf len %u, buf offset %u)", 
			RndisMessage->MessageLength,
			RndisMessage->Message.QueryComplete.RequestId,
			RndisMessage->Message.QueryComplete.Status,
			RndisMessage->Message.QueryComplete.InformationBufferLength,
			RndisMessage->Message.QueryComplete.InformationBufferOffset);
		break;

	case REMOTE_NDIS_SET_CMPLT:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_SET_CMPLT (len %u, id 0x%x, status 0x%x)", 
			RndisMessage->MessageLength,
			RndisMessage->Message.SetComplete.RequestId,
			RndisMessage->Message.SetComplete.Status);
		break;

	case REMOTE_NDIS_INDICATE_STATUS_MSG:
		DPRINT_DBG(NETVSC, "REMOTE_NDIS_INDICATE_STATUS_MSG (len %u, status 0x%x, buf len %u, buf offset %u)", 
			RndisMessage->MessageLength,
			RndisMessage->Message.IndicateStatus.Status,
			RndisMessage->Message.IndicateStatus.StatusBufferLength,
			RndisMessage->Message.IndicateStatus.StatusBufferOffset);
		break;

	default:
		DPRINT_DBG(NETVSC, "0x%x (len %u)",
			RndisMessage->NdisMessageType,
			RndisMessage->MessageLength);
		break;
	}
}

static int
RndisFilterSendRequest(
	RNDIS_DEVICE	*Device,
	RNDIS_REQUEST	*Request
	)
{
	int ret=0;
	NETVSC_PACKET *packet;
	
	DPRINT_ENTER(NETVSC);

	// Setup the packet to send it
	packet = &Request->Packet;
	
	packet->IsDataPacket = FALSE;
	packet->TotalDataBufferLength = Request->RequestMessage.MessageLength;
	packet->PageBufferCount = 1;

	packet->PageBuffers[0].Pfn = GetPhysicalAddress(&Request->RequestMessage) >> PAGE_SHIFT;
	packet->PageBuffers[0].Length = Request->RequestMessage.MessageLength;
	packet->PageBuffers[0].Offset = (ULONG_PTR)&Request->RequestMessage & (PAGE_SIZE -1);

	packet->Completion.Send.SendCompletionContext = Request;//packet;
	packet->Completion.Send.OnSendCompletion = RndisFilterOnSendRequestCompletion;
	packet->Completion.Send.SendCompletionTid = (ULONG_PTR)Device;

	ret = gRndisFilter.InnerDriver.OnSend(Device->NetDevice->Device, packet);
	DPRINT_EXIT(NETVSC);
	return ret;
}


static void 
RndisFilterReceiveResponse(
	RNDIS_DEVICE	*Device,
	RNDIS_MESSAGE	*Response
	)
{
	LIST_ENTRY *anchor;
	LIST_ENTRY *curr;
	RNDIS_REQUEST *request=NULL;
	BOOL found=FALSE;

	DPRINT_ENTER(NETVSC);

	SpinlockAcquire(Device->RequestLock);
	ITERATE_LIST_ENTRIES(anchor, curr, &Device->RequestList)
	{		
		request = CONTAINING_RECORD(curr, RNDIS_REQUEST, ListEntry);

		// All request/response message contains RequestId as the 1st field
		if (request->RequestMessage.Message.InitializeRequest.RequestId == Response->Message.InitializeComplete.RequestId)
		{
			DPRINT_DBG(NETVSC, "found rndis request for this response (id 0x%x req type 0x%x res type 0x%x)", 
				request->RequestMessage.Message.InitializeRequest.RequestId, request->RequestMessage.NdisMessageType, Response->NdisMessageType);

			found = TRUE;
			break;
		}
	}
	SpinlockRelease(Device->RequestLock);

	if (found)
	{
		if (Response->MessageLength <= sizeof(RNDIS_MESSAGE))
		{
			memcpy(&request->ResponseMessage, Response, Response->MessageLength);
		}
		else
		{
			DPRINT_ERR(NETVSC, "rndis response buffer overflow detected (size %u max %u)", Response->MessageLength, sizeof(RNDIS_FILTER_PACKET));

			if (Response->NdisMessageType == REMOTE_NDIS_RESET_CMPLT) // does not have a request id field
			{
				request->ResponseMessage.Message.ResetComplete.Status = STATUS_BUFFER_OVERFLOW;
			}
			else
			{
				request->ResponseMessage.Message.InitializeComplete.Status = STATUS_BUFFER_OVERFLOW;
			}
		}

		WaitEventSet(request->WaitEvent);
	}
	else
	{
		DPRINT_ERR(NETVSC, "no rndis request found for this response (id 0x%x res type 0x%x)", 
				Response->Message.InitializeComplete.RequestId, Response->NdisMessageType);
	}

	DPRINT_EXIT(NETVSC);
}

static void 
RndisFilterReceiveIndicateStatus(
	RNDIS_DEVICE	*Device,
	RNDIS_MESSAGE	*Response
	)
{
	RNDIS_INDICATE_STATUS *indicate = &Response->Message.IndicateStatus;
		
	if (indicate->Status == RNDIS_STATUS_MEDIA_CONNECT)
	{
		gRndisFilter.InnerDriver.OnLinkStatusChanged(Device->NetDevice->Device, 1);
	}
	else if (indicate->Status == RNDIS_STATUS_MEDIA_DISCONNECT)
	{
		gRndisFilter.InnerDriver.OnLinkStatusChanged(Device->NetDevice->Device, 0);
	}
	else
	{
		// TODO:
	}
}

static void
RndisFilterReceiveData(
	RNDIS_DEVICE	*Device,
	RNDIS_MESSAGE	*Message,
	NETVSC_PACKET	*Packet
	)
{
	RNDIS_PACKET *rndisPacket;
	UINT32 dataOffset;

	DPRINT_ENTER(NETVSC);

	// empty ethernet frame ??
	ASSERT(Packet->PageBuffers[0].Length > RNDIS_MESSAGE_SIZE(RNDIS_PACKET));

	rndisPacket = &Message->Message.Packet;

	// FIXME: Handle multiple rndis pkt msgs that maybe enclosed in this
	// netvsc packet (ie TotalDataBufferLength != MessageLength)

	// Remove the rndis header and pass it back up the stack
	dataOffset = RNDIS_HEADER_SIZE + rndisPacket->DataOffset;
		
	Packet->TotalDataBufferLength -= dataOffset;
	Packet->PageBuffers[0].Offset += dataOffset;
	Packet->PageBuffers[0].Length -= dataOffset;

	Packet->IsDataPacket = TRUE;
		
	gRndisFilter.InnerDriver.OnReceiveCallback(Device->NetDevice->Device, Packet);

	DPRINT_EXIT(NETVSC);
}

static int
RndisFilterOnReceive(
	DEVICE_OBJECT		*Device,
	NETVSC_PACKET		*Packet
	)
{
	NETVSC_DEVICE *netDevice = (NETVSC_DEVICE*)Device->Extension;
	RNDIS_DEVICE *rndisDevice;
	RNDIS_MESSAGE rndisMessage;
	RNDIS_MESSAGE *rndisHeader;

	DPRINT_ENTER(NETVSC);

	ASSERT(netDevice);
	//Make sure the rndis device state is initialized
	if (!netDevice->Extension)
	{
		DPRINT_ERR(NETVSC, "got rndis message but no rndis device...dropping this message!");
		DPRINT_EXIT(NETVSC);
		return -1;
	}

	rndisDevice = (RNDIS_DEVICE*)netDevice->Extension;
	if (rndisDevice->State == RNDIS_DEV_UNINITIALIZED)
	{
		DPRINT_ERR(NETVSC, "got rndis message but rndis device uninitialized...dropping this message!");
		DPRINT_EXIT(NETVSC);
		return -1;
	}

	rndisHeader = (RNDIS_MESSAGE*)PageMapVirtualAddress(Packet->PageBuffers[0].Pfn);

	rndisHeader = (void*)((ULONG_PTR)rndisHeader + Packet->PageBuffers[0].Offset);
	
	// Make sure we got a valid rndis message
	// FIXME: There seems to be a bug in set completion msg where its MessageLength is 16 bytes but
	// the ByteCount field in the xfer page range shows 52 bytes
#if 0
	if ( Packet->TotalDataBufferLength != rndisHeader->MessageLength )
	{
		PageUnmapVirtualAddress((void*)(ULONG_PTR)rndisHeader - Packet->PageBuffers[0].Offset);

		DPRINT_ERR(NETVSC, "invalid rndis message? (expected %u bytes got %u)...dropping this message!",
			rndisHeader->MessageLength, Packet->TotalDataBufferLength);
		DPRINT_EXIT(NETVSC);
		return -1;
	}
#endif

	if ((rndisHeader->NdisMessageType != REMOTE_NDIS_PACKET_MSG) && (rndisHeader->MessageLength > sizeof(RNDIS_MESSAGE)))
	{
		DPRINT_ERR(NETVSC, "incoming rndis message buffer overflow detected (got %u, max %u)...marking it an error!",
			rndisHeader->MessageLength, sizeof(RNDIS_MESSAGE));
	}

	memcpy(&rndisMessage, rndisHeader, (rndisHeader->MessageLength > sizeof(RNDIS_MESSAGE))?sizeof(RNDIS_MESSAGE):rndisHeader->MessageLength);

	PageUnmapVirtualAddress((void*)(ULONG_PTR)rndisHeader - Packet->PageBuffers[0].Offset);

	DumpRndisMessage(&rndisMessage);

	switch (rndisMessage.NdisMessageType)
	{
		// data msg
	case REMOTE_NDIS_PACKET_MSG:
		RndisFilterReceiveData(rndisDevice, &rndisMessage, Packet);
		break;

		// completion msgs
	case REMOTE_NDIS_INITIALIZE_CMPLT:
	case REMOTE_NDIS_QUERY_CMPLT:
	case REMOTE_NDIS_SET_CMPLT:
	//case REMOTE_NDIS_RESET_CMPLT:
	//case REMOTE_NDIS_KEEPALIVE_CMPLT:
		RndisFilterReceiveResponse(rndisDevice, &rndisMessage);
		break;

		// notification msgs
	case REMOTE_NDIS_INDICATE_STATUS_MSG:
		RndisFilterReceiveIndicateStatus(rndisDevice, &rndisMessage);
		break;
	default:
		DPRINT_ERR(NETVSC, "unhandled rndis message (type %u len %u)", rndisMessage.NdisMessageType, rndisMessage.MessageLength);
		break;
	}

	DPRINT_EXIT(NETVSC);
	return 0;
}


static int
RndisFilterQueryDevice(
	RNDIS_DEVICE	*Device,
	UINT32			Oid,
	VOID			*Result,
	UINT32			*ResultSize
	)
{
	RNDIS_REQUEST *request;
	UINT32 inresultSize = *ResultSize;
	RNDIS_QUERY_REQUEST *query;
	RNDIS_QUERY_COMPLETE *queryComplete;
	int ret=0;

	DPRINT_ENTER(NETVSC);

	ASSERT(Result);

	*ResultSize = 0;
	request = GetRndisRequest(Device, REMOTE_NDIS_QUERY_MSG, RNDIS_MESSAGE_SIZE(RNDIS_QUERY_REQUEST));
	if (!request)
	{
		ret = -1;
		goto Cleanup;
	}

	// Setup the rndis query
	query = &request->RequestMessage.Message.QueryRequest;
	query->Oid = Oid;
	query->InformationBufferOffset = sizeof(RNDIS_QUERY_REQUEST); 
	query->InformationBufferLength = 0;
	query->DeviceVcHandle = 0;

	ret = RndisFilterSendRequest(Device, request);
	if (ret != 0)
	{
		/* Fixme:  printf added */
		printf("RNDISFILTER request failed to Send!\n");
		goto Cleanup;
	}

	WaitEventWait(request->WaitEvent);

	// Copy the response back
	queryComplete = &request->ResponseMessage.Message.QueryComplete;
	
	if (queryComplete->InformationBufferLength > inresultSize)
	{
		ret = -1;
		goto Cleanup;
	}

	memcpy(Result, 
			(void*)((ULONG_PTR)queryComplete + queryComplete->InformationBufferOffset),
			queryComplete->InformationBufferLength);

	*ResultSize = queryComplete->InformationBufferLength;

Cleanup:
	if (request)
	{
		PutRndisRequest(Device, request);
	}
	DPRINT_EXIT(NETVSC);

	return ret;
}

static inline int
RndisFilterQueryDeviceMac(
	RNDIS_DEVICE	*Device
	)
{
	UINT32 size=HW_MACADDR_LEN;

	return RndisFilterQueryDevice(Device, 
									RNDIS_OID_802_3_PERMANENT_ADDRESS,
									Device->HwMacAddr,
									&size);
}

static inline int
RndisFilterQueryDeviceLinkStatus(
	RNDIS_DEVICE	*Device
	)
{
	UINT32 size=sizeof(UINT32);

	return RndisFilterQueryDevice(Device, 
									RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
									&Device->LinkStatus,
									&size);
}

static int
RndisFilterSetPacketFilter(
	RNDIS_DEVICE	*Device,
	UINT32			NewFilter
	)
{
	RNDIS_REQUEST *request;
	RNDIS_SET_REQUEST *set;
	RNDIS_SET_COMPLETE *setComplete;
	UINT32 status;
	int ret;

	DPRINT_ENTER(NETVSC);

	ASSERT(RNDIS_MESSAGE_SIZE(RNDIS_SET_REQUEST) + sizeof(UINT32) <= sizeof(RNDIS_MESSAGE));

	request = GetRndisRequest(Device, REMOTE_NDIS_SET_MSG, RNDIS_MESSAGE_SIZE(RNDIS_SET_REQUEST) + sizeof(UINT32));
	if (!request)
	{
		ret = -1;
		goto Cleanup;
	}

	// Setup the rndis set
	set = &request->RequestMessage.Message.SetRequest;
	set->Oid = RNDIS_OID_GEN_CURRENT_PACKET_FILTER;
	set->InformationBufferLength = sizeof(UINT32);
	set->InformationBufferOffset = sizeof(RNDIS_SET_REQUEST); 

	memcpy((void*)(ULONG_PTR)set + sizeof(RNDIS_SET_REQUEST), &NewFilter, sizeof(UINT32));

	ret = RndisFilterSendRequest(Device, request);
	if (ret != 0)
	{
		/* Fixme:  printf added */
		printf("RNDISFILTER request failed to Send!  ret %d\n", ret);
		goto Cleanup;
	}

	/* Fixme:  second parameter is 2000 in the lis21 code drop */
	ret = WaitEventWaitEx(request->WaitEvent, 4000/*2sec*/);
	if (!ret)
	{
		ret = -1;
		DPRINT_ERR(NETVSC, "timeout before we got a set response...cmd %d ", NewFilter);
		// We cant deallocate the request since we may still receive a send completion for it.
		goto Exit;
	}
	else
	{
		if (ret > 0)
		{
			ret = 0;
		}
		setComplete = &request->ResponseMessage.Message.SetComplete;
		status = setComplete->Status;
	}

Cleanup:
	if (request)
	{
		PutRndisRequest(Device, request);
	}
Exit:
	DPRINT_EXIT(NETVSC);

	return ret;
}

int
RndisFilterInit(
	NETVSC_DRIVER_OBJECT	*Driver
	)
{
	DPRINT_ENTER(NETVSC);

	DPRINT_DBG(NETVSC, "sizeof(RNDIS_FILTER_PACKET) == %d", sizeof(RNDIS_FILTER_PACKET));

	Driver->RequestExtSize = sizeof(RNDIS_FILTER_PACKET);
	Driver->AdditionalRequestPageBufferCount = 1; // For rndis header

	//Driver->Context = rndisDriver;

	memset(&gRndisFilter, 0, sizeof(RNDIS_FILTER_DRIVER_OBJECT));

	/*rndisDriver->Driver = Driver;

	ASSERT(Driver->OnLinkStatusChanged);
	rndisDriver->OnLinkStatusChanged = Driver->OnLinkStatusChanged;*/

	// Save the original dispatch handlers before we override it
	gRndisFilter.InnerDriver.Base.OnDeviceAdd = Driver->Base.OnDeviceAdd;
	gRndisFilter.InnerDriver.Base.OnDeviceRemove = Driver->Base.OnDeviceRemove;
	gRndisFilter.InnerDriver.Base.OnCleanup = Driver->Base.OnCleanup;

	ASSERT(Driver->OnSend);
	ASSERT(Driver->OnReceiveCallback);
	gRndisFilter.InnerDriver.OnSend = Driver->OnSend;
	gRndisFilter.InnerDriver.OnReceiveCallback = Driver->OnReceiveCallback;
	gRndisFilter.InnerDriver.OnLinkStatusChanged = Driver->OnLinkStatusChanged;

	// Override 
	Driver->Base.OnDeviceAdd = RndisFilterOnDeviceAdd;
	Driver->Base.OnDeviceRemove = RndisFilterOnDeviceRemove;
	Driver->Base.OnCleanup = RndisFilterOnCleanup;
	/* Fixme:  Prototype mismatch */
	Driver->OnSend = RndisFilterOnSend;
	/* Fixme:  Prototype mismatch */
	Driver->OnOpen = RndisFilterOnOpen;
	/* Fixme:  Prototype mismatch */
	Driver->OnClose = RndisFilterOnClose;
	//Driver->QueryLinkStatus = RndisFilterQueryDeviceLinkStatus;
	/* Fixme:  Prototype mismatch */
	Driver->OnReceiveCallback = RndisFilterOnReceive;

	DPRINT_EXIT(NETVSC);

	return 0;
}

static int
RndisFilterInitDevice(
	RNDIS_DEVICE	*Device
	)
{
	RNDIS_REQUEST *request;
	RNDIS_INITIALIZE_REQUEST *init;
	RNDIS_INITIALIZE_COMPLETE *initComplete;
	UINT32 status;
	int ret;

	DPRINT_ENTER(NETVSC);

	request = GetRndisRequest(Device, REMOTE_NDIS_INITIALIZE_MSG, RNDIS_MESSAGE_SIZE(RNDIS_INITIALIZE_REQUEST));
	if (!request)
	{
		ret = -1;
		goto Cleanup;
	}

	// Setup the rndis set
	init = &request->RequestMessage.Message.InitializeRequest;
	init->MajorVersion = RNDIS_MAJOR_VERSION;
	init->MinorVersion = RNDIS_MINOR_VERSION;
	init->MaxTransferSize = 2048; // FIXME: Use 1536 - rounded ethernet frame size
	
	Device->State = RNDIS_DEV_INITIALIZING;

	ret = RndisFilterSendRequest(Device, request);
	if (ret != 0)
	{
		Device->State = RNDIS_DEV_UNINITIALIZED;
		goto Cleanup;
	}

	WaitEventWait(request->WaitEvent);

	initComplete = &request->ResponseMessage.Message.InitializeComplete;
	status = initComplete->Status;
	if (status == RNDIS_STATUS_SUCCESS)
	{
		Device->State = RNDIS_DEV_INITIALIZED;
		ret = 0;
	}
	else
	{
		Device->State = RNDIS_DEV_UNINITIALIZED; 
		ret = -1;
	}

Cleanup:
	if (request)
	{
		PutRndisRequest(Device, request);
	}
	DPRINT_EXIT(NETVSC);

	return ret;
}

static void
RndisFilterHaltDevice(
	RNDIS_DEVICE	*Device
	)
{
	RNDIS_REQUEST *request;
	RNDIS_HALT_REQUEST *halt;

	DPRINT_ENTER(NETVSC);

	// Attempt to do a rndis device halt
	request = GetRndisRequest(Device, REMOTE_NDIS_HALT_MSG, RNDIS_MESSAGE_SIZE(RNDIS_HALT_REQUEST));
	if (!request)
	{
		goto Cleanup;
	}

	// Setup the rndis set
	halt = &request->RequestMessage.Message.HaltRequest;
	halt->RequestId = InterlockedIncrement((int*)&Device->NewRequestId);
	
	// Ignore return since this msg is optional.
	RndisFilterSendRequest(Device, request);
	
	Device->State = RNDIS_DEV_UNINITIALIZED;

Cleanup:
	if (request)
	{
		PutRndisRequest(Device, request);
	}
	DPRINT_EXIT(NETVSC);
	return;
}


static int
RndisFilterOpenDevice(
	RNDIS_DEVICE	*Device
	)
{
	int ret=0;

	DPRINT_ENTER(NETVSC);

	if (Device->State != RNDIS_DEV_INITIALIZED)
		return 0;

	if (promisc_mode != 1) {
		ret = RndisFilterSetPacketFilter(Device, 
						 NDIS_PACKET_TYPE_BROADCAST|
						 NDIS_PACKET_TYPE_ALL_MULTICAST|
						 NDIS_PACKET_TYPE_DIRECTED);
		DPRINT_INFO(NETVSC, "Network set to normal mode");
	} else {
		ret = RndisFilterSetPacketFilter(Device, 
						 NDIS_PACKET_TYPE_PROMISCUOUS);
		DPRINT_INFO(NETVSC, "Network set to promiscuous mode");
	}

	if (ret == 0)
		Device->State = RNDIS_DEV_DATAINITIALIZED;

	DPRINT_EXIT(NETVSC);
	return ret;
}

static int
RndisFilterCloseDevice(
	RNDIS_DEVICE		*Device
	)
{
	int ret;

	DPRINT_ENTER(NETVSC);

	if (Device->State != RNDIS_DEV_DATAINITIALIZED)
		return 0;

	ret = RndisFilterSetPacketFilter(Device, 0);
	if (ret == 0)
	{
		Device->State = RNDIS_DEV_INITIALIZED;
	}

	DPRINT_EXIT(NETVSC);

	return ret;
}


int
RndisFilterOnDeviceAdd(
	DEVICE_OBJECT	*Device,
	void			*AdditionalInfo
	)
{
	int ret;
	NETVSC_DEVICE *netDevice;
	RNDIS_DEVICE *rndisDevice;
	NETVSC_DEVICE_INFO *deviceInfo = (NETVSC_DEVICE_INFO*)AdditionalInfo;

	DPRINT_ENTER(NETVSC);

	//rndisDevice = MemAlloc(sizeof(RNDIS_DEVICE));
	rndisDevice = GetRndisDevice();
	if (!rndisDevice)
	{
		DPRINT_EXIT(NETVSC);
		return -1;
	}

	DPRINT_DBG(NETVSC, "rndis device object allocated - %p", rndisDevice);

	// Let the inner driver handle this first to create the netvsc channel
	// NOTE! Once the channel is created, we may get a receive callback 
	// (RndisFilterOnReceive()) before this call is completed
	ret = gRndisFilter.InnerDriver.Base.OnDeviceAdd(Device, AdditionalInfo);
	if (ret != 0)
	{
		PutRndisDevice(rndisDevice);
		DPRINT_EXIT(NETVSC);
		return ret;
	}

	//
	// Initialize the rndis device
	//
	netDevice = (NETVSC_DEVICE*)Device->Extension;
	ASSERT(netDevice);
	ASSERT(netDevice->Device);

	netDevice->Extension = rndisDevice;
	rndisDevice->NetDevice = netDevice;

	// Send the rndis initialization message
	ret = RndisFilterInitDevice(rndisDevice);
	if (ret != 0)
	{
		// TODO: If rndis init failed, we will need to shut down the channel
	}

	// Get the mac address
	ret = RndisFilterQueryDeviceMac(rndisDevice);
	if (ret != 0)
	{
		// TODO: shutdown rndis device and the channel
	}
	
	DPRINT_INFO(NETVSC, "Device 0x%p mac addr %02x%02x%02x%02x%02x%02x",
				rndisDevice,
				rndisDevice->HwMacAddr[0],
				rndisDevice->HwMacAddr[1],
				rndisDevice->HwMacAddr[2],
				rndisDevice->HwMacAddr[3],
				rndisDevice->HwMacAddr[4],
				rndisDevice->HwMacAddr[5]);

	memcpy(deviceInfo->MacAddr, rndisDevice->HwMacAddr, HW_MACADDR_LEN);

	RndisFilterQueryDeviceLinkStatus(rndisDevice);
	
	deviceInfo->LinkState = rndisDevice->LinkStatus;
	DPRINT_INFO(NETVSC, "Device 0x%p link state %s", rndisDevice, ((deviceInfo->LinkState)?("down"):("up")));

	DPRINT_EXIT(NETVSC);

	return ret;
}


static int
RndisFilterOnDeviceRemove(
	DEVICE_OBJECT *Device
	)
{
	NETVSC_DEVICE *netDevice = (NETVSC_DEVICE*)Device->Extension;
	RNDIS_DEVICE *rndisDevice = (RNDIS_DEVICE*)netDevice->Extension;

	DPRINT_ENTER(NETVSC);

	// Halt and release the rndis device
	RndisFilterHaltDevice(rndisDevice);

	PutRndisDevice(rndisDevice);
	netDevice->Extension = NULL;

	// Pass control to inner driver to remove the device
	gRndisFilter.InnerDriver.Base.OnDeviceRemove(Device);

	DPRINT_EXIT(NETVSC);

	return 0;
}


static void
RndisFilterOnCleanup(
	DRIVER_OBJECT *Driver
	)
{
	DPRINT_ENTER(NETVSC);

	DPRINT_EXIT(NETVSC);
}

static int 
RndisFilterOnOpen(
	DEVICE_OBJECT		*Device
	)
{
	int ret;
	NETVSC_DEVICE *netDevice = (NETVSC_DEVICE*)Device->Extension;
	
	DPRINT_ENTER(NETVSC);
	
	ASSERT(netDevice);
	ret = RndisFilterOpenDevice((RNDIS_DEVICE*)netDevice->Extension);

	DPRINT_EXIT(NETVSC);

	return ret;
}

static int 
RndisFilterOnClose(
	DEVICE_OBJECT		*Device
	)
{
	int ret;
	NETVSC_DEVICE *netDevice = (NETVSC_DEVICE*)Device->Extension;
	
	DPRINT_ENTER(NETVSC);
	
	ASSERT(netDevice);
	ret = RndisFilterCloseDevice((RNDIS_DEVICE*)netDevice->Extension);

	DPRINT_EXIT(NETVSC);

	return ret;
}


static int 
RndisFilterOnSend(
	DEVICE_OBJECT		*Device,
	NETVSC_PACKET		*Packet
	)
{
	int ret=0;
	RNDIS_FILTER_PACKET *filterPacket;
	RNDIS_MESSAGE *rndisMessage;
	RNDIS_PACKET *rndisPacket;
	UINT32 rndisMessageSize;

	DPRINT_ENTER(NETVSC);

	// Add the rndis header
	filterPacket = (RNDIS_FILTER_PACKET*)Packet->Extension;
	ASSERT(filterPacket);

	memset(filterPacket, 0, sizeof(RNDIS_FILTER_PACKET));

	rndisMessage = &filterPacket->Message;
	rndisMessageSize = RNDIS_MESSAGE_SIZE(RNDIS_PACKET);

	rndisMessage->NdisMessageType = REMOTE_NDIS_PACKET_MSG;
	rndisMessage->MessageLength = Packet->TotalDataBufferLength + rndisMessageSize;
	
	rndisPacket = &rndisMessage->Message.Packet;
	rndisPacket->DataOffset = sizeof(RNDIS_PACKET);
	rndisPacket->DataLength = Packet->TotalDataBufferLength;

	Packet->IsDataPacket = TRUE;
	Packet->PageBuffers[0].Pfn		= GetPhysicalAddress(rndisMessage) >> PAGE_SHIFT;
	Packet->PageBuffers[0].Offset	= (ULONG_PTR)rndisMessage & (PAGE_SIZE-1);
	Packet->PageBuffers[0].Length	= rndisMessageSize;

	// Save the packet send completion and context
	filterPacket->OnCompletion = Packet->Completion.Send.OnSendCompletion;
	filterPacket->CompletionContext = Packet->Completion.Send.SendCompletionContext;

	// Use ours
	Packet->Completion.Send.OnSendCompletion = RndisFilterOnSendCompletion;
	Packet->Completion.Send.SendCompletionContext = filterPacket;

	ret = gRndisFilter.InnerDriver.OnSend(Device, Packet);
	if (ret != 0)
	{
		// Reset the completion to originals to allow retries from above
		Packet->Completion.Send.OnSendCompletion = filterPacket->OnCompletion;
		Packet->Completion.Send.SendCompletionContext = filterPacket->CompletionContext;
	}

	DPRINT_EXIT(NETVSC);

	return ret;
}

static void 
RndisFilterOnSendCompletion(
   void *Context)
{
	RNDIS_FILTER_PACKET *filterPacket = (RNDIS_FILTER_PACKET *)Context;

	DPRINT_ENTER(NETVSC);

	// Pass it back to the original handler
	filterPacket->OnCompletion(filterPacket->CompletionContext);

	DPRINT_EXIT(NETVSC);
}


static void 
RndisFilterOnSendRequestCompletion(
   void *Context
   )
{
	DPRINT_ENTER(NETVSC);

	// Noop
	DPRINT_EXIT(NETVSC);
}

/*
 * Fixme:  Function below originally added for NetScaler
 *
 * Fixme:  Is this needed for the HyperV porting effort?
 */

int RndisSetMode(DEVICE_OBJECT *Device, int mode)
{
	NETVSC_DEVICE *netDevice = (NETVSC_DEVICE*)Device->Extension;
	RNDIS_DEVICE *rndisDevice = (RNDIS_DEVICE*)netDevice->Extension;
	int ret;

	if (mode) {
		ret = RndisFilterSetPacketFilter(rndisDevice,
		    NDIS_PACKET_TYPE_PROMISCUOUS);
	} else {
		ret = RndisFilterSetPacketFilter(rndisDevice,
		    NDIS_PACKET_TYPE_BROADCAST |
		    NDIS_PACKET_TYPE_ALL_MULTICAST |
		    NDIS_PACKET_TYPE_DIRECTED);
	}
	return (ret);
}

