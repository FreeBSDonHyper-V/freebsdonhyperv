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
 * HyperV protocol used by the network VSP/VSC.  This protocol defines the
 * messages that are sent through the VMBus ring buffer established
 * during the channel offer from the VSP to the VSC.  The small size of this
 * protocol is possible because most of the work for facilitating a network
 * connection is handled by the RNDIS protocol.
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

#ifndef __HV_NVSP_PROTOCOL_H__
#define __HV_NVSP_PROTOCOL_H__

#pragma once

#ifdef REMOVED
/* Fixme:  Removed */
#include <VmbusChannelInterface.h>
#endif

#define NVSP_INVALID_PROTOCOL_VERSION           ((UINT32)0xFFFFFFFF)

#define NVSP_PROTOCOL_VERSION_1                 2
#define NVSP_MIN_PROTOCOL_VERSION               NVSP_PROTOCOL_VERSION_1
#define NVSP_MAX_PROTOCOL_VERSION               NVSP_PROTOCOL_VERSION_1

typedef enum _NVSP_MESSAGE_TYPE
{
    NvspMessageTypeNone = 0,

    //
    // Init Messages
    //
    NvspMessageTypeInit                         = 1,
    NvspMessageTypeInitComplete                 = 2,

    NvspVersionMessageStart                     = 100,

    //
    // Version 1 Messages
    //
    NvspMessage1TypeSendNdisVersion             = NvspVersionMessageStart,

    NvspMessage1TypeSendReceiveBuffer,
    NvspMessage1TypeSendReceiveBufferComplete,
    NvspMessage1TypeRevokeReceiveBuffer,

    NvspMessage1TypeSendSendBuffer,
    NvspMessage1TypeSendSendBufferComplete,
    NvspMessage1TypeRevokeSendBuffer,

    NvspMessage1TypeSendRNDISPacket,
    NvspMessage1TypeSendRNDISPacketComplete,
    
    //
    // This should be set to the number of messages for the version
    // with the maximum number of messages.
    //
    NvspNumMessagePerVersion                    = 9,

} NVSP_MESSAGE_TYPE, *PNVSP_MESSAGE_TYPE;

typedef enum _NVSP_STATUS
{
    NvspStatusNone = 0,
    NvspStatusSuccess,
    NvspStatusFailure,
    NvspStatusProtocolVersionRangeTooNew,
    NvspStatusProtocolVersionRangeTooOld,
    NvspStatusInvalidRndisPacket,
    NvspStatusBusy,
    NvspStatusMax,
} NVSP_STATUS, *PNVSP_STATUS;

#pragma pack(push, 1)

typedef struct _NVSP_MESSAGE_HEADER
{
    UINT32                                  MessageType;
} NVSP_MESSAGE_HEADER, *PNVSP_MESSAGE_HEADER;

//
// Init Messages
//

//
// This message is used by the VSC to initialize the channel
// after the channels has been opened. This message should 
// never include anything other then versioning (i.e. this
// message will be the same for ever).
//
typedef struct _NVSP_MESSAGE_INIT
{
    UINT32                                  MinProtocolVersion;
    UINT32                                  MaxProtocolVersion;
} NVSP_MESSAGE_INIT, *PNVSP_MESSAGE_INIT;

//
// This message is used by the VSP to complete the initialization
// of the channel. This message should never include anything other 
// then versioning (i.e. this message will be the same for ever).
//
typedef struct _NVSP_MESSAGE_INIT_COMPLETE
{
    UINT32                                  NegotiatedProtocolVersion;
    UINT32                                  MaximumMdlChainLength;
    UINT32                                  Status;
} NVSP_MESSAGE_INIT_COMPLETE, *PNVSP_MESSAGE_INIT_COMPLETE;

typedef union _NVSP_MESSAGE_INIT_UBER
{
    NVSP_MESSAGE_INIT                       Init;
    NVSP_MESSAGE_INIT_COMPLETE              InitComplete;
} NVSP_MESSAGE_INIT_UBER;

//
// Version 1 Messages
//

//
// This message is used by the VSC to send the NDIS version
// to the VSP. The VSP can use this information when handling
// OIDs sent by the VSC.
//
typedef struct _NVSP_1_MESSAGE_SEND_NDIS_VERSION
{
    UINT32                                  NdisMajorVersion;
    UINT32                                  NdisMinorVersion;
} NVSP_1_MESSAGE_SEND_NDIS_VERSION, *PNVSP_1_MESSAGE_SEND_NDIS_VERSION;

//
// This message is used by the VSC to send a receive buffer
// to the VSP. The VSP can then use the receive buffer to
// send data to the VSC.
//
typedef struct _NVSP_1_MESSAGE_SEND_RECEIVE_BUFFER
{
    GPADL_HANDLE                            GpadlHandle;
    UINT16                                  Id;
} NVSP_1_MESSAGE_SEND_RECEIVE_BUFFER, *PNVSP_1_MESSAGE_SEND_RECEIVE_BUFFER;

typedef struct _NVSP_1_RECEIVE_BUFFER_SECTION
{
    UINT32                                  Offset;
    UINT32                                  SubAllocationSize;
    UINT32                                  NumSubAllocations;
    UINT32                                  EndOffset;
} NVSP_1_RECEIVE_BUFFER_SECTION, *PNVSP_1_RECEIVE_BUFFER_SECTION;

//
// This message is used by the VSP to acknowledge a receive 
// buffer send by the VSC. This message must be sent by the 
// VSP before the VSP uses the receive buffer.
//
typedef struct _NVSP_1_MESSAGE_SEND_RECEIVE_BUFFER_COMPLETE
{
    UINT32                                  Status;
    UINT32                                  NumSections;

    //
    // The receive buffer is split into two parts, a large
    // suballocation section and a small suballocation
    // section. These sections are then suballocated by a 
    // certain size.
    //
    // For example, the following break up of the receive
    // buffer has 6 large suballocations and 10 small
    // suballocations.
    //
    // |            Large Section          |  |   Small Section   |
    // ------------------------------------------------------------
    // |     |     |     |     |     |     |  | | | | | | | | | | |
    // |                                      |  
    // LargeOffset                            SmallOffset
    //
    NVSP_1_RECEIVE_BUFFER_SECTION           Sections[1];

} NVSP_1_MESSAGE_SEND_RECEIVE_BUFFER_COMPLETE, *PNVSP_1_MESSAGE_SEND_RECEIVE_BUFFER_COMPLETE;

//
// This message is sent by the VSC to revoke the receive buffer.
// After the VSP completes this transaction, the vsp should never
// use the receive buffer again.
//
typedef struct _NVSP_1_MESSAGE_REVOKE_RECEIVE_BUFFER
{
    UINT16                                  Id;
} NVSP_1_MESSAGE_REVOKE_RECEIVE_BUFFER, *PNVSP_1_MESSAGE_REVOKE_RECEIVE_BUFFER;

//
// This message is used by the VSC to send a send buffer
// to the VSP. The VSC can then use the send buffer to
// send data to the VSP.
//
typedef struct _NVSP_1_MESSAGE_SEND_SEND_BUFFER
{
    GPADL_HANDLE                            GpadlHandle;
    UINT16                                  Id;
} NVSP_1_MESSAGE_SEND_SEND_BUFFER, *PNVSP_1_MESSAGE_SEND_SEND_BUFFER;

//
// This message is used by the VSP to acknowledge a send 
// buffer sent by the VSC. This message must be sent by the 
// VSP before the VSP uses the sent buffer.
//
typedef struct _NVSP_1_MESSAGE_SEND_SEND_BUFFER_COMPLETE
{
    UINT32                                  Status;

    //
    // The VSC gets to choose the size of the send buffer and
    // the VSP gets to choose the sections size of the buffer.
    // This was done to enable dynamic reconfigurations when
    // the cost of GPA-direct buffers decreases.
    //
    UINT32                                  SectionSize;
} NVSP_1_MESSAGE_SEND_SEND_BUFFER_COMPLETE, *PNVSP_1_MESSAGE_SEND_SEND_BUFFER_COMPLETE;

//
// This message is sent by the VSC to revoke the send buffer.
// After the VSP completes this transaction, the vsp should never
// use the send buffer again.
//
typedef struct _NVSP_1_MESSAGE_REVOKE_SEND_BUFFER
{
    UINT16                                  Id;
} NVSP_1_MESSAGE_REVOKE_SEND_BUFFER, *PNVSP_1_MESSAGE_REVOKE_SEND_BUFFER;

//
// This message is used by both the VSP and the VSC to send
// a RNDIS message to the opposite channel endpoint.
//
typedef struct _NVSP_1_MESSAGE_SEND_RNDIS_PACKET
{
    //
    // This field is specified by RNIDS. They assume there's
    // two different channels of communication. However, 
    // the Network VSP only has one. Therefore, the channel
    // travels with the RNDIS packet.
    //
    UINT32                                  ChannelType;

    //
    // This field is used to send part or all of the data
    // through a send buffer. This values specifies an 
    // index into the send buffer. If the index is 
    // 0xFFFFFFFF, then the send buffer is not being used
    // and all of the data was sent through other VMBus
    // mechanisms.
    //
    UINT32                                  SendBufferSectionIndex;
    UINT32                                  SendBufferSectionSize;
} NVSP_1_MESSAGE_SEND_RNDIS_PACKET, *PNVSP_1_MESSAGE_SEND_RNDIS_PACKET;

//
// This message is used by both the VSP and the VSC to complete
// a RNDIS message to the opposite channel endpoint. At this
// point, the initiator of this message cannot use any resources
// associated with the original RNDIS packet.
//
typedef struct _NVSP_1_MESSAGE_SEND_RNDIS_PACKET_COMPLETE
{
    UINT32                                  Status;
} NVSP_1_MESSAGE_SEND_RNDIS_PACKET_COMPLETE, *PNVSP_1_MESSAGE_SEND_RNDIS_PACKET_COMPLETE;

typedef union _NVSP_MESSAGE_1_UBER
{
    NVSP_1_MESSAGE_SEND_NDIS_VERSION            SendNdisVersion;

    NVSP_1_MESSAGE_SEND_RECEIVE_BUFFER          SendReceiveBuffer;
    NVSP_1_MESSAGE_SEND_RECEIVE_BUFFER_COMPLETE SendReceiveBufferComplete;
    NVSP_1_MESSAGE_REVOKE_RECEIVE_BUFFER        RevokeReceiveBuffer;

    NVSP_1_MESSAGE_SEND_SEND_BUFFER             SendSendBuffer;
    NVSP_1_MESSAGE_SEND_SEND_BUFFER_COMPLETE    SendSendBufferComplete;
    NVSP_1_MESSAGE_REVOKE_SEND_BUFFER           RevokeSendBuffer;

    NVSP_1_MESSAGE_SEND_RNDIS_PACKET            SendRNDISPacket;
    NVSP_1_MESSAGE_SEND_RNDIS_PACKET_COMPLETE   SendRNDISPacketComplete;
} NVSP_1_MESSAGE_UBER;

typedef union _NVSP_ALL_MESSAGES
{
    NVSP_MESSAGE_INIT_UBER                  InitMessages;
    NVSP_1_MESSAGE_UBER                     Version1Messages;

} NVSP_ALL_MESSAGES;

//
// ALL Messages
//
typedef struct _NVSP_MESSAGE
{
    NVSP_MESSAGE_HEADER                     Header; 
    NVSP_ALL_MESSAGES                       Messages;
} NVSP_MESSAGE, *PNVSP_MESSAGE;

#pragma pack(pop)

#endif  /* __HV_NVSP_PROTOCOL_H__ */

