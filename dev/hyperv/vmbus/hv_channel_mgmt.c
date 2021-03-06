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
 * HyperV channel code
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

/* Fixme:  Added these includes to get memset, memcmp, shutdown_nice */
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
#include "osd.h"
#include "logging.h"

#include "VmbusPrivate.h"
#include <sys/reboot.h>
#include <sys/systm.h>
#endif


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
#include "hv_timesync_ic.h"
#include "hv_vmbus_private.h"

#include <sys/reboot.h>
/* Fixme:  commented out for now.  Causes a huge number of compile errors */
//#include <sys/systm.h>


//
// Defines
//
#define HV_S_OK                   0x00000000
#define HV_E_FAIL                 0x80004005
#define HV_ERROR_NOT_SUPPORTED    0x80070032
#define HV_ERROR_MACHINE_LOCKED   0x800704F7

#ifndef false
#define false  0
#endif

#ifndef true
#define true  1
#endif

//
// Data types
//

typedef void (*PFN_CHANNEL_MESSAGE_HANDLER)(VMBUS_CHANNEL_MESSAGE_HEADER* msg);

typedef struct _VMBUS_CHANNEL_MESSAGE_TABLE_ENTRY {
	VMBUS_CHANNEL_MESSAGE_TYPE	messageType;
	PFN_CHANNEL_MESSAGE_HANDLER messageHandler;
} VMBUS_CHANNEL_MESSAGE_TABLE_ENTRY;

//
// Internal routines
//

static void 
VmbusChannelOnOffer(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	);
static void 
VmbusChannelOnOpenResult(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	);

static void 
VmbusChannelOnOfferRescind(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	);

static void 
VmbusChannelOnGpadlCreated(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	);

static void 
VmbusChannelOnGpadlTorndown(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	);

static void 
VmbusChannelOnOffersDelivered(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	);

static void
VmbusChannelOnVersionResponse(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	);

static void 
VmbusChannelProcessOffer(
	PVOID context
	);

static void 
VmbusChannelProcessRescindOffer(
	PVOID context
	);


//
// Globals
//

#define MAX_NUM_DEVICE_CLASSES_SUPPORTED 7

const GUID gSupportedDeviceClasses[MAX_NUM_DEVICE_CLASSES_SUPPORTED]= {
	//{ba6163d9-04a1-4d29-b605-72e2ffb1dc7f}
	{.Data  = {0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d, 0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f}},// Storage - SCSI
	//{F8615163-DF3E-46c5-913F-F2D2F965ED0E}
	{.Data = {0x63, 0x51, 0x61, 0xF8, 0x3E, 0xDF, 0xc5, 0x46, 0x91, 0x3F, 0xF2, 0xD2, 0xF9, 0x65, 0xED, 0x0E}},	// Network
	//{CFA8B69E-5B4A-4cc0-B98B-8BA1A1F3F95A}
	{.Data = {0x9E, 0xB6, 0xA8, 0xCF, 0x4A, 0x5B, 0xc0, 0x4c, 0xB9, 0x8B, 0x8B, 0xA1, 0xA1, 0xF3, 0xF9, 0x5A}}, // Input
	//{32412632-86cb-44a2-9b5c-50d1417354f5}
	{.Data = {0x32, 0x26, 0x41, 0x32, 0xcb, 0x86, 0xa2, 0x44, 0x9b, 0x5c, 0x50, 0xd1, 0x41, 0x73, 0x54, 0xf5}}, // IDE

        /* {9527E630-D0AE-497b-ADCE-E80AB0175CAF} */
        /* TimeSync */
        {
                .Data = {
                        0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49,
                        0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf
                }
        },
	/* 0E0B6031-5213-4934-818B-38D90CED39DB  Correct One*/
	/* Shutdown */
	{
		.Data = {
			0x31, 0x60, 0x0B, 0X0E, 0x13, 0x52, 0x34, 0x49,
			0x81, 0x8B, 0x38, 0XD9, 0x0C, 0xED, 0x39, 0xDB
		}
	},

// {0E0B6031-5213-4934-818B-38D90CED39DB}
//0x0e 0b 60 31, 0x5213, 0x4934, 0x81, 0x8b, 0x38, 0xd9, 0xc, 0xed, 0x39, 0xdb
// {57164f39-9115-4e78-ab55-382f3bd5422d}
//0x57 16 4f 39, 0x9115, 0x4e78, 0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d

        // VMBus channel type GUID
        // {57164f39-9115-4e78-ab55-382f3bd5422d}
	/* Heartbeat */
	{
		.Data = {
			0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e,
			0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d
		}
	},

};

// Channel message dispatch table
VMBUS_CHANNEL_MESSAGE_TABLE_ENTRY gChannelMessageTable[ChannelMessageCount]= {
    {ChannelMessageInvalid,		    NULL},
    {ChannelMessageOfferChannel,            VmbusChannelOnOffer},
    {ChannelMessageRescindChannelOffer,     VmbusChannelOnOfferRescind},
    {ChannelMessageRequestOffers,           NULL},
    {ChannelMessageAllOffersDelivered,      VmbusChannelOnOffersDelivered},
    {ChannelMessageOpenChannel,             NULL},
    {ChannelMessageOpenChannelResult,       VmbusChannelOnOpenResult},
    {ChannelMessageCloseChannel,            NULL},
    {ChannelMessageGpadlHeader,             NULL},
    {ChannelMessageGpadlBody,               NULL},
    {ChannelMessageGpadlCreated,	    VmbusChannelOnGpadlCreated},
    {ChannelMessageGpadlTeardown,           NULL},
    {ChannelMessageGpadlTorndown,           VmbusChannelOnGpadlTorndown},
    {ChannelMessageRelIdReleased,           NULL},
    {ChannelMessageInitiateContact,         NULL},
    {ChannelMessageVersionResponse,	    VmbusChannelOnVersionResponse},
    {ChannelMessageUnload,					NULL},
};

/*++

Name: 
	AllocVmbusChannel()

Description:
	Allocate and initialize a vmbus channel object

--*/
VMBUS_CHANNEL* AllocVmbusChannel(void)
{
	VMBUS_CHANNEL* channel;

	channel = (VMBUS_CHANNEL*) MemAllocAtomic(sizeof(VMBUS_CHANNEL));
	if (!channel)
	{
		return NULL;
	}

	memset(channel, 0,sizeof(VMBUS_CHANNEL));
	channel->InboundLock = SpinlockCreate();
	if (!channel->InboundLock)
	{
		MemFree(channel);
		return NULL;
	}

	channel->PollTimer = TimerCreate(VmbusChannelOnTimer, channel);
	if (!channel->PollTimer)
	{
		SpinlockClose(channel->InboundLock);
		MemFree(channel);
		return NULL;
	}

	//channel->dataWorkQueue = WorkQueueCreate("data");
	channel->ControlWQ = WorkQueueCreate("control");
	if (!channel->ControlWQ)
	{
		TimerClose(channel->PollTimer);
		SpinlockClose(channel->InboundLock);
		MemFree(channel);
		return NULL;
	}

	return channel;
}

/*++

Name: 
	ReleaseVmbusChannel()

Description:
	Release the vmbus channel object itself

--*/
static inline void ReleaseVmbusChannel(void* Context)
{
	VMBUS_CHANNEL* channel = (VMBUS_CHANNEL*)Context;

	DPRINT_ENTER(VMBUS);

	DPRINT_DBG(VMBUS, "releasing channel (%p)", channel);
	WorkQueueClose(channel->ControlWQ);
	DPRINT_DBG(VMBUS, "channel released (%p)", channel);

	MemFree(channel);

	DPRINT_EXIT(VMBUS);
}

/*++

Name: 
	FreeVmbusChannel()

Description:
	Release the resources used by the vmbus channel object

--*/
void FreeVmbusChannel(VMBUS_CHANNEL* Channel)
{
	SpinlockClose(Channel->InboundLock);
	TimerClose(Channel->PollTimer);

	// We have to release the channel's workqueue/thread in the vmbus's workqueue/thread context
	// ie we can't destroy ourselves.
	WorkQueueQueueWorkItem(gVmbusConnection.WorkQueue, ReleaseVmbusChannel, (void*)Channel);
}

void shutdown_onchannelcallback(void *context)
{
	VMBUS_CHANNEL *channel = context;
	u8 *buf;
	u32 buflen, recvlen;
	u64 requestid;
	u8  execute_shutdown = false;

	struct shutdown_msg_data *shutdown_msg;

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop;

	DPRINT_ENTER(VMBUS);

	buflen = PAGE_SIZE;
	buf = MemAllocAtomic(buflen);

	VmbusChannelRecvPacket(channel, buf, buflen, &recvlen, &requestid);

	if (recvlen > 0) 
	{
		DPRINT_DBG(VMBUS, "shutdown packet: len=%d, requestid=%lld", 
			   recvlen, requestid);

		icmsghdrp = (struct icmsg_hdr *)&buf[
			sizeof(struct vmbuspipe_hdr)];
	    
		if(icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) 
		{
			icmsghdrp->icmsgsize = 0x10;

			negop = (struct icmsg_negotiate *)&buf[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

			if(negop->icframe_vercnt == 2 &&
			   negop->icversion_data[1].major == 3) {
				negop->icversion_data[0].major = 3;
				negop->icversion_data[0].minor = 0;
				negop->icversion_data[1].major = 3;
				negop->icversion_data[1].minor = 0;
			} else {
				negop->icversion_data[0].major = 1;
				negop->icversion_data[0].minor = 0;
				negop->icversion_data[1].major = 1;
				negop->icversion_data[1].minor = 0;
			}

			negop->icframe_vercnt = 1;
			negop->icmsg_vercnt = 1;
	    } else {
			shutdown_msg = (struct shutdown_msg_data *)&buf[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

/*			printk(KERN_INFO "Shutdown Message: ");
 *
 *			for (cnt = 0; cnt < 30; cnt = cnt + 2)
 *				printk(KERN_INFO "%c", 
 *				       shutdown_msg->display_message[cnt]);
 *			
 *			printk(KERN_INFO "\n");
 */
			switch(shutdown_msg->flags) 
			{
/*
 * Interactive shutdown requests are not supported on Linux (e.g. Screen locked
 * If it was, this is what you would be sending.
 *			case 0:
 *
 *        			icmsghdrp->status = HV_ERROR_MACHINE_LOCKED;
 *				execute_shutdown = false;
 *
 *				icmsghdrp->status = HV_S_OK;
 *				execute_shutdown = true;
 *				
 *				DPRINT_INFO(VMBUS, "Shutdown request received -"
 *					    "interactive shutdown request "
 *					    "not supported");
 *				break;
*/
			case 0:
			case 1:
				icmsghdrp->status = HV_S_OK;
				execute_shutdown = true;
				
				DPRINT_INFO(VMBUS, "Shutdown request received -"
					    "gracefull shutdown initiated");
				break;
			default:
				icmsghdrp->status = HV_E_FAIL;
				execute_shutdown = false;
				
				DPRINT_INFO(VMBUS, "Shutdown request received -"
					    "Invalid request");
				break;
			};
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION 
			| ICMSGHDRFLAG_RESPONSE;

		VmbusChannelSendPacket(channel, buf,
				       recvlen, requestid,
				       VmbusPacketTypeDataInBand, 0);
	}

	MemFree(buf);

	DPRINT_EXIT(VMBUS);

	if (execute_shutdown == true)
	{
		/*
		 * Fixme:  This replaces several lines of Linux shutdown code
		 */
		shutdown_nice(RB_POWEROFF | RB_HALT);
	}
}

static /* Fixme -- added static to quash warning */
void heartbeat_onchannelcallback(void *context)
{
	VMBUS_CHANNEL *channel = context;
	u8 *buf;
	u32 buflen, recvlen;
	u64 requestid;

	struct heartbeat_msg_data *heartbeat_msg;

	struct icmsg_hdr *icmsghdrp;
	struct icmsg_negotiate *negop;

	DPRINT_ENTER(VMBUS);

	buflen = PAGE_SIZE;
	buf = MemAllocAtomic(buflen);

	VmbusChannelRecvPacket(channel, buf, buflen, &recvlen, &requestid);

	if (recvlen > 0) 
	{
		DPRINT_DBG(VMBUS, "heartbeat packet: len=%d, requestid=%lld", 
			   recvlen, requestid);

	//	printf("heartbeat packet: len=%d, requestid=%lld", 
	//		   recvlen, requestid);

		icmsghdrp = (struct icmsg_hdr *)&buf[
			sizeof(struct vmbuspipe_hdr)];
	    
		if(icmsghdrp->icmsgtype == ICMSGTYPE_NEGOTIATE) 
		{
			icmsghdrp->icmsgsize = 0x10;

			negop = (struct icmsg_negotiate *)&buf[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

			if(negop->icframe_vercnt == 2 &&
			   negop->icversion_data[1].major == 3) {
				negop->icversion_data[0].major = 3;
				negop->icversion_data[0].minor = 0;
				negop->icversion_data[1].major = 3;
				negop->icversion_data[1].minor = 0;
			} else {
				negop->icversion_data[0].major = 1;
				negop->icversion_data[0].minor = 0;
				negop->icversion_data[1].major = 1;
				negop->icversion_data[1].minor = 0;
			}

			negop->icframe_vercnt = 1;
			negop->icmsg_vercnt = 1;
		} else {
			heartbeat_msg = (struct heartbeat_msg_data *)&buf[
				sizeof(struct vmbuspipe_hdr) +
				sizeof(struct icmsg_hdr)];

		DPRINT_DBG(VMBUS, "heartbeat seq = %lld", 
			   heartbeat_msg->seq_num);
		// printf("heartbeat seq = %lld", 
		//	   heartbeat_msg->seq_num);

			heartbeat_msg->seq_num += 1;
		}

		icmsghdrp->icflags = ICMSGHDRFLAG_TRANSACTION 
			| ICMSGHDRFLAG_RESPONSE;

		VmbusChannelSendPacket(channel, buf,
				       recvlen, requestid,
				       VmbusPacketTypeDataInBand, 0);
	}

	MemFree(buf);

	DPRINT_EXIT(VMBUS);
}

/*++

Name: 
	VmbusChannelProcessOffer()

Description:
	Process the offer by creating a channel/device associated with this offer

--*/
static void 
VmbusChannelProcessOffer(
	PVOID context
	)
{
	int ret=0;
	VMBUS_CHANNEL* newChannel=(VMBUS_CHANNEL*)context;
	GUID *guidType;
	LIST_ENTRY* anchor;
	LIST_ENTRY* curr;
	BOOL fNew=TRUE;
	VMBUS_CHANNEL* channel;

	DPRINT_ENTER(VMBUS);

	// Make sure this is a new offer
	SpinlockAcquire(gVmbusConnection.ChannelLock);

	ITERATE_LIST_ENTRIES(anchor, curr, &gVmbusConnection.ChannelList)
	{		
		channel = CONTAINING_RECORD(curr, VMBUS_CHANNEL, ListEntry);

		if (!memcmp(&channel->OfferMsg.Offer.InterfaceType, &newChannel->OfferMsg.Offer.InterfaceType,sizeof(GUID)) &&
			!memcmp(&channel->OfferMsg.Offer.InterfaceInstance, &newChannel->OfferMsg.Offer.InterfaceInstance, sizeof(GUID)))
		{
			fNew = FALSE;
			break;
		}
	}

	if (fNew)
	{
		INSERT_TAIL_LIST(&gVmbusConnection.ChannelList, &newChannel->ListEntry);
	}
	SpinlockRelease(gVmbusConnection.ChannelLock);

	if (!fNew)
	{
		DPRINT_DBG(VMBUS, "Ignoring duplicate offer for relid (%d)", newChannel->OfferMsg.ChildRelId);
		FreeVmbusChannel(newChannel);
		DPRINT_EXIT(VMBUS);
		return;
	}

	// Start the process of binding this offer to the driver
	// We need to set the DeviceObject field before calling VmbusChildDeviceAdd()
	newChannel->DeviceObject = VmbusChildDeviceCreate(
		newChannel->OfferMsg.Offer.InterfaceType,
		newChannel->OfferMsg.Offer.InterfaceInstance,
		newChannel);

	DPRINT_DBG(VMBUS, "child device object allocated - %p", newChannel->DeviceObject);

	// Add the new device to the bus. This will kick off device-driver binding
	// which eventually invokes the device driver's AddDevice() method.
	ret = VmbusChildDeviceAdd(newChannel->DeviceObject);
	if (ret != 0)
	{
		DPRINT_ERR(VMBUS, "unable to add child device object (relid %d)", 
			newChannel->OfferMsg.ChildRelId);

		SpinlockAcquire(gVmbusConnection.ChannelLock);
		REMOVE_ENTRY_LIST(&newChannel->ListEntry);
		SpinlockRelease(gVmbusConnection.ChannelLock);

		FreeVmbusChannel(newChannel);
	}
	else
	{
		// This state is used to indicate a successful open 
		// so that when we do close the channel normally,
		// we can cleanup properly
		newChannel->State = CHANNEL_OPEN_STATE;

		if (memcmp(&newChannel->OfferMsg.Offer.InterfaceType,
		    &gSupportedDeviceClasses[4], sizeof(GUID)) == 0) {
			DPRINT_INFO(VMBUS, "Opening Timesync channel...");
			if(VmbusChannelOpen(newChannel, 10*PAGE_SIZE,
					    10*PAGE_SIZE, NULL, 0,
					    timesync_channel_cb, newChannel) == 0)
				DPRINT_INFO(VMBUS, "Opened Timesync channel successfully:%p", newChannel);
		}

		if (memcmp(&newChannel->OfferMsg.Offer.InterfaceType,
			   &gSupportedDeviceClasses[5], sizeof(GUID)) == 0) {
			DPRINT_INFO(VMBUS, "Opening Shutdown channel...");

			if(VmbusChannelOpen(newChannel, 10 * PAGE_SIZE,
					    10 * PAGE_SIZE, NULL, 0,
					    shutdown_onchannelcallback, 
					    newChannel) == 0)
				DPRINT_INFO(VMBUS, "Opened Shutdown channel successfully:%p", newChannel);
			guidType = &newChannel->OfferMsg.Offer.InterfaceType;
			
			DPRINT_DBG(VMBUS, "Shutdown type guid - "
                                    "type {%02x%02x%02x%02x-%02x%02x-%02x%02x-"
                                    "%02x%02x%02x%02x%02x%02x%02x%02x} ",
                                    guidType->Data[3], guidType->Data[2],
                                    guidType->Data[1], guidType->Data[0],
                                    guidType->Data[5], guidType->Data[4],
                                    guidType->Data[7], guidType->Data[6],
                                    guidType->Data[8], guidType->Data[9],
                                    guidType->Data[10], guidType->Data[11],
                                    guidType->Data[12], guidType->Data[13],
                                    guidType->Data[14], guidType->Data[15]);
		}

		if (memcmp(&newChannel->OfferMsg.Offer.InterfaceType,
		    &gSupportedDeviceClasses[6], sizeof(GUID)) == 0) {
			DPRINT_INFO(VMBUS, "Opening Heartbeat channel...");
			if(VmbusChannelOpen(newChannel, 10*PAGE_SIZE,
					    10*PAGE_SIZE, NULL, 0,
					    heartbeat_onchannelcallback, newChannel) == 0)
				DPRINT_INFO(VMBUS, "Opened Heartbeat channel successfully:%p", newChannel);
		}
	}
	DPRINT_EXIT(VMBUS);
}

/*++

Name: 
	VmbusChannelProcessRescindOffer()

Description:
	Rescind the offer by initiating a device removal

--*/
static void 
VmbusChannelProcessRescindOffer(
	PVOID context
	)
{
	VMBUS_CHANNEL* channel=(VMBUS_CHANNEL*)context;

	DPRINT_ENTER(VMBUS);

	VmbusChildDeviceRemove(channel->DeviceObject);

	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusChannelOnOffer()

Description:
	Handler for channel offers from vmbus in parent partition. We ignore all offers except
	network and storage offers. For each network and storage offers, we create a channel object 
	and queue a work item to the channel object to process the offer synchronously

--*/
static void 
VmbusChannelOnOffer(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	)
{
	VMBUS_CHANNEL_OFFER_CHANNEL* offer = (VMBUS_CHANNEL_OFFER_CHANNEL*)hdr;
	VMBUS_CHANNEL* newChannel;

	GUID *guidType;
	GUID *guidInstance;
	int i;
	int fSupported=0;

	DPRINT_ENTER(VMBUS);

	for (i=0; i<MAX_NUM_DEVICE_CLASSES_SUPPORTED; i++)
	{
		if (memcmp(&offer->Offer.InterfaceType, &gSupportedDeviceClasses[i], sizeof(GUID)) == 0)
		{
			fSupported = 1;
			break;
		}
	}

	if (!fSupported)
	{
		DPRINT_DBG(VMBUS, "Ignoring channel offer notification for child relid %d", offer->ChildRelId);
		DPRINT_EXIT(VMBUS);

		return;
	}

	guidType = &offer->Offer.InterfaceType;
	guidInstance = &offer->Offer.InterfaceInstance;

	DPRINT_DBG(VMBUS, "Channel offer notification - child relid %d monitor id %d allocated %d, "
		"type {%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x} "
		"instance {%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x%02x%02x}", 
		offer->ChildRelId,
		offer->MonitorId,
		offer->MonitorAllocated,
		guidType->Data[3], guidType->Data[2], guidType->Data[1], guidType->Data[0], guidType->Data[5], guidType->Data[4], guidType->Data[7], guidType->Data[6], guidType->Data[8], guidType->Data[9], guidType->Data[10], guidType->Data[11], guidType->Data[12], guidType->Data[13], guidType->Data[14], guidType->Data[15],
		guidInstance->Data[3], guidInstance->Data[2], guidInstance->Data[1], guidInstance->Data[0], guidInstance->Data[5], guidInstance->Data[4], guidInstance->Data[7], guidInstance->Data[6], guidInstance->Data[8], guidInstance->Data[9], guidInstance->Data[10], guidInstance->Data[11], guidInstance->Data[12], guidInstance->Data[13], guidInstance->Data[14], guidInstance->Data[15]);

	// Allocate the channel object and save this offer.
	newChannel = AllocVmbusChannel();
	if (!newChannel)
	{
		DPRINT_ERR(VMBUS, "unable to allocate channel object");
		return;
	}
	
	DPRINT_DBG(VMBUS, "channel object allocated - %p", newChannel);

	memcpy(&newChannel->OfferMsg, offer, sizeof(VMBUS_CHANNEL_OFFER_CHANNEL));
	newChannel->MonitorGroup = (UINT8)offer->MonitorId / 32;
	newChannel->MonitorBit = (UINT8)offer->MonitorId % 32;

	// TODO: Make sure the offer comes from our parent partition
	WorkQueueQueueWorkItem(newChannel->ControlWQ, VmbusChannelProcessOffer, newChannel);

	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusChannelOnOfferRescind()

Description:
	Rescind offer handler. We queue a work item to process this offer
	synchronously

--*/
static void 
VmbusChannelOnOfferRescind(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	)
{
	VMBUS_CHANNEL_RESCIND_OFFER* rescind = (VMBUS_CHANNEL_RESCIND_OFFER*)hdr;
	VMBUS_CHANNEL* channel;

	DPRINT_ENTER(VMBUS);

	channel = GetChannelFromRelId(rescind->ChildRelId);
	if (channel == NULL)
	{
		DPRINT_DBG(VMBUS, "channel not found for relId %d", rescind->ChildRelId);
		return;
	}

	WorkQueueQueueWorkItem(channel->ControlWQ, VmbusChannelProcessRescindOffer, channel);

	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusChannelOnOffersDelivered()

Description:
	This is invoked when all offers have been delivered.
	Nothing to do here.

--*/
static void 
VmbusChannelOnOffersDelivered(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	)
{
	DPRINT_ENTER(VMBUS);
	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusChannelOnOpenResult()

Description:
	Open result handler. This is invoked when we received a response
	to our channel open request. Find the matching request, copy the 
	response and signal the requesting thread.

--*/
static void 
VmbusChannelOnOpenResult(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	)
{
	VMBUS_CHANNEL_OPEN_RESULT* result = (VMBUS_CHANNEL_OPEN_RESULT*)hdr;
	LIST_ENTRY* anchor;
	LIST_ENTRY* curr;
	VMBUS_CHANNEL_MSGINFO* msgInfo;
	VMBUS_CHANNEL_MESSAGE_HEADER* requestHeader;
	VMBUS_CHANNEL_OPEN_CHANNEL* openMsg;

	DPRINT_ENTER(VMBUS);

	DPRINT_DBG(VMBUS, "vmbus open result - %d", result->Status);
	
	// Find the open msg, copy the result and signal/unblock the wait event
	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);

	ITERATE_LIST_ENTRIES(anchor, curr, &gVmbusConnection.ChannelMsgList)
	{
		msgInfo = (VMBUS_CHANNEL_MSGINFO*) curr;
		requestHeader = (VMBUS_CHANNEL_MESSAGE_HEADER*)msgInfo->Msg;

		if (requestHeader->MessageType == ChannelMessageOpenChannel)
		{
			openMsg = (VMBUS_CHANNEL_OPEN_CHANNEL*)msgInfo->Msg;
			if (openMsg->ChildRelId == result->ChildRelId &&
				openMsg->OpenId == result->OpenId)
			{
				memcpy(&msgInfo->Response.OpenResult, result, sizeof(VMBUS_CHANNEL_OPEN_RESULT));
				WaitEventSet(msgInfo->WaitEvent);
				break;
			}
		}
	}
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusChannelOnGpadlCreated()

Description:
	GPADL created handler. This is invoked when we received a response
	to our gpadl create request. Find the matching request, copy the 
	response and signal the requesting thread.

--*/
static void 
VmbusChannelOnGpadlCreated(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	)
{
	VMBUS_CHANNEL_GPADL_CREATED *gpadlCreated = (VMBUS_CHANNEL_GPADL_CREATED*)hdr;
	LIST_ENTRY *anchor;
	LIST_ENTRY *curr;
	VMBUS_CHANNEL_MSGINFO *msgInfo;
	VMBUS_CHANNEL_MESSAGE_HEADER *requestHeader;
	VMBUS_CHANNEL_GPADL_HEADER *gpadlHeader;

	DPRINT_ENTER(VMBUS);

	DPRINT_DBG(VMBUS, "vmbus gpadl created result - %d", gpadlCreated->CreationStatus);

	// Find the establish msg, copy the result and signal/unblock the wait event
	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);

	ITERATE_LIST_ENTRIES(anchor, curr, &gVmbusConnection.ChannelMsgList)
	{
		msgInfo = (VMBUS_CHANNEL_MSGINFO*) curr;
		requestHeader = (VMBUS_CHANNEL_MESSAGE_HEADER*)msgInfo->Msg;

		if (requestHeader->MessageType == ChannelMessageGpadlHeader)
		{
			gpadlHeader = (VMBUS_CHANNEL_GPADL_HEADER*)requestHeader;

			if ((gpadlCreated->ChildRelId == gpadlHeader->ChildRelId) &&
					(gpadlCreated->Gpadl == gpadlHeader->Gpadl))
			{
				memcpy(&msgInfo->Response.GpadlCreated, gpadlCreated, sizeof(VMBUS_CHANNEL_GPADL_CREATED));
				WaitEventSet(msgInfo->WaitEvent);
				break;
			}
		}
	}
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusChannelOnGpadlTorndown()

Description:
	GPADL torndown handler. This is invoked when we received a response
	to our gpadl teardown request. Find the matching request, copy the 
	response and signal the requesting thread.

--*/
static void 
VmbusChannelOnGpadlTorndown(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	)
{
	VMBUS_CHANNEL_GPADL_TORNDOWN* gpadlTorndown  = (VMBUS_CHANNEL_GPADL_TORNDOWN*)hdr;
	LIST_ENTRY* anchor;
	LIST_ENTRY* curr;
	VMBUS_CHANNEL_MSGINFO* msgInfo;
	VMBUS_CHANNEL_MESSAGE_HEADER *requestHeader;
	VMBUS_CHANNEL_GPADL_TEARDOWN *gpadlTeardown;

	DPRINT_ENTER(VMBUS);

	// Find the open msg, copy the result and signal/unblock the wait event
	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);

	ITERATE_LIST_ENTRIES(anchor, curr, &gVmbusConnection.ChannelMsgList)
	{
		msgInfo = (VMBUS_CHANNEL_MSGINFO*) curr;
		requestHeader = (VMBUS_CHANNEL_MESSAGE_HEADER*)msgInfo->Msg;

		if (requestHeader->MessageType == ChannelMessageGpadlTeardown)
		{
			gpadlTeardown = (VMBUS_CHANNEL_GPADL_TEARDOWN*)requestHeader;

			if (gpadlTorndown->Gpadl == gpadlTeardown->Gpadl)
			{
				memcpy(&msgInfo->Response.GpadlTorndown, gpadlTorndown, sizeof(VMBUS_CHANNEL_GPADL_TORNDOWN));
				WaitEventSet(msgInfo->WaitEvent);
				break;
			}
		}
	}
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusChannelOnVersionResponse()

Description:
	Version response handler. This is invoked when we received a response
	to our initiate contact request. Find the matching request, copy the 
	response and signal the requesting thread.

--*/
static void 
VmbusChannelOnVersionResponse(
	PVMBUS_CHANNEL_MESSAGE_HEADER hdr
	)
{
	LIST_ENTRY* anchor;
	LIST_ENTRY* curr;
	VMBUS_CHANNEL_MSGINFO *msgInfo;
	VMBUS_CHANNEL_MESSAGE_HEADER *requestHeader;
	VMBUS_CHANNEL_INITIATE_CONTACT *initiate;
	VMBUS_CHANNEL_VERSION_RESPONSE *versionResponse  = (VMBUS_CHANNEL_VERSION_RESPONSE*)hdr;

	DPRINT_ENTER(VMBUS);

	SpinlockAcquire(gVmbusConnection.ChannelMsgLock);

	ITERATE_LIST_ENTRIES(anchor, curr, &gVmbusConnection.ChannelMsgList)
	{
		msgInfo = (VMBUS_CHANNEL_MSGINFO*) curr;
		requestHeader = (VMBUS_CHANNEL_MESSAGE_HEADER*)msgInfo->Msg;

		if (requestHeader->MessageType == ChannelMessageInitiateContact)
		{
			initiate = (VMBUS_CHANNEL_INITIATE_CONTACT*)requestHeader;
			memcpy(&msgInfo->Response.VersionResponse, versionResponse, sizeof(VMBUS_CHANNEL_VERSION_RESPONSE));
			WaitEventSet(msgInfo->WaitEvent);			
		}
	}
	SpinlockRelease(gVmbusConnection.ChannelMsgLock);

	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusOnChannelMessage()

Description:
	Handler for channel protocol messages.
	This is invoked in the vmbus worker thread context.

--*/
VOID
VmbusOnChannelMessage(
	void *Context
	)
{
	HV_MESSAGE *msg=(HV_MESSAGE*)Context;
	VMBUS_CHANNEL_MESSAGE_HEADER* hdr;
	int size;

	DPRINT_ENTER(VMBUS);
	
	hdr = (VMBUS_CHANNEL_MESSAGE_HEADER*)msg->u.Payload;
	size=msg->Header.PayloadSize;
	
	DPRINT_DBG(VMBUS, "message type %d size %d", hdr->MessageType, size);

	if (hdr->MessageType >= ChannelMessageCount)
	{
		DPRINT_ERR(VMBUS, "Received invalid channel message type %d size %d", hdr->MessageType, size);
//		PrintBytes((unsigned char *)msg->u.Payload, size);
		MemFree(msg);
		return;
	}

	if (gChannelMessageTable[hdr->MessageType].messageHandler)
	{
		gChannelMessageTable[hdr->MessageType].messageHandler(hdr);
	}
	else
	{
		DPRINT_ERR(VMBUS, "Unhandled channel message type %d", hdr->MessageType);
	}

	// Free the msg that was allocated in VmbusOnMsgDPC()
	MemFree(msg);
	DPRINT_EXIT(VMBUS);
}


/*++

Name: 
	VmbusChannelRequestOffers()

Description:
	Send a request to get all our pending offers.

--*/
int
VmbusChannelRequestOffers(
	VOID
	)
{
	int ret=0;
	VMBUS_CHANNEL_MESSAGE_HEADER* msg;
	VMBUS_CHANNEL_MSGINFO* msgInfo;

	DPRINT_ENTER(VMBUS);

	msgInfo = 
		(VMBUS_CHANNEL_MSGINFO*)MemAlloc(sizeof(VMBUS_CHANNEL_MSGINFO) + sizeof(VMBUS_CHANNEL_MESSAGE_HEADER));
	ASSERT(msgInfo != NULL);

	msgInfo->WaitEvent = WaitEventCreate();
	msg = (VMBUS_CHANNEL_MESSAGE_HEADER*)msgInfo->Msg;

	msg->MessageType = ChannelMessageRequestOffers;

	/*SpinlockAcquire(gVmbusConnection.channelMsgLock);
	INSERT_TAIL_LIST(&gVmbusConnection.channelMsgList, &msgInfo->msgListEntry);
	SpinlockRelease(gVmbusConnection.channelMsgLock);*/

	ret = VmbusPostMessage(msg, sizeof(VMBUS_CHANNEL_MESSAGE_HEADER));
	if (ret != 0)
	{
		DPRINT_ERR(VMBUS, "Unable to request offers - %d", ret);

		/*SpinlockAcquire(gVmbusConnection.channelMsgLock);
		REMOVE_ENTRY_LIST(&msgInfo->msgListEntry);
		SpinlockRelease(gVmbusConnection.channelMsgLock);*/

		goto Cleanup;
	}
	//WaitEventWait(msgInfo->waitEvent);

	/*SpinlockAcquire(gVmbusConnection.channelMsgLock);
	REMOVE_ENTRY_LIST(&msgInfo->msgListEntry);
	SpinlockRelease(gVmbusConnection.channelMsgLock);*/


Cleanup:
	if (msgInfo)
	{
		WaitEventClose(msgInfo->WaitEvent);
		MemFree(msgInfo);
	}

	DPRINT_EXIT(VMBUS);

	return ret;
}

/*++

Name: 
	VmbusChannelReleaseUnattachedChannels()

Description:
	Release channels that are unattached/unconnected ie (no drivers associated) 

--*/
void
VmbusChannelReleaseUnattachedChannels(
	VOID
	)
{
	LIST_ENTRY *entry;
	VMBUS_CHANNEL *channel;
	VMBUS_CHANNEL *start=NULL;

	SpinlockAcquire(gVmbusConnection.ChannelLock);

	while (!IsListEmpty(&gVmbusConnection.ChannelList))
	{	
		entry = TOP_LIST_ENTRY(&gVmbusConnection.ChannelList);
		channel = CONTAINING_RECORD(entry, VMBUS_CHANNEL, ListEntry);

		if (channel == start)
			break;

		if (!channel->DeviceObject->Driver)
		{
			REMOVE_ENTRY_LIST(&channel->ListEntry);
			DPRINT_INFO(VMBUS, "Releasing unattached device object %p", channel->DeviceObject);

			VmbusChildDeviceRemove(channel->DeviceObject);
			FreeVmbusChannel(channel);
		}
		else
		{
			if (!start)
			{
				start = channel;
			}
		}
	}

	SpinlockRelease(gVmbusConnection.ChannelLock);
}

// eof

