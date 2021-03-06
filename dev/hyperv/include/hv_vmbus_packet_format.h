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
 * HyperV structures that define the format of the vmbus packets.
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

#ifndef __HV_VMBUS_PACKET_FORMAT_H__
#define __HV_VMBUS_PACKET_FORMAT_H__

#pragma once

//#ifndef PAGE_SIZE
//#if defined(_IA64_)
//#error This does not work for IA64
//#else
//#define PAGE_SIZE 0x1000
//#endif
//#endif

// allow nameless unions
//#pragma warning(disable : 4201)

typedef struct
{
    union
    {
        struct 
        {
            volatile UINT32  In;        // Offset in bytes from the ring base
            volatile UINT32  Out;       // Offset in bytes from the ring base
        };
        volatile LONGLONG    InOut;
    };

    //
    // If the receiving endpoint sets this to some non-zero value, the sending 
    // endpoint should not send any interrupts.
    //

    volatile UINT32 InterruptMask;

} VMRCB, *PVMRCB;

typedef struct 
{
    union
    {
        struct
        {
            VMRCB Control;
        };
        
        UINT8 Reserved[PAGE_SIZE];
    };
    
    //
    // Beginning of the ring data.  Note: It must be guaranteed that
    // this data does not share a page with the control structure.
    //
    UINT8 Data[1];
} VMRING, *PVMRING;

#pragma pack(push, 1)

typedef struct
{
    UINT16 Type;
    UINT16 DataOffset8;
    UINT16 Length8;
    UINT16 Flags;
    UINT64 TransactionId;
} VMPACKET_DESCRIPTOR, *PVMPACKET_DESCRIPTOR;

typedef UINT32 PREVIOUS_PACKET_OFFSET, *PPREVIOUS_PACKET_OFFSET;

typedef struct 
{
    PREVIOUS_PACKET_OFFSET  PreviousPacketStartOffset;
    VMPACKET_DESCRIPTOR     Descriptor;
} VMPACKET_HEADER, *PVMPACKET_HEADER;

typedef struct
{
    UINT32  ByteCount;
    UINT32  ByteOffset;
} VMTRANSFER_PAGE_RANGE, *PVMTRANSFER_PAGE_RANGE;

#ifdef __cplusplus

typedef struct _VMTRANSFER_PAGE_PACKET_HEADER : VMPACKET_DESCRIPTOR {

#else

typedef struct VMTRANSFER_PAGE_PACKET_HEADER {
    
    VMPACKET_DESCRIPTOR d;
    
#endif

    UINT16                  TransferPageSetId;
    BOOLEAN                 SenderOwnsSet;
    UINT8                   Reserved;
    UINT32                  RangeCount;
    VMTRANSFER_PAGE_RANGE   Ranges[1];
    
} VMTRANSFER_PAGE_PACKET_HEADER, *PVMTRANSFER_PAGE_PACKET_HEADER;


#ifdef __cplusplus

typedef struct _VMGPADL_PACKET_HEADER : VMPACKET_DESCRIPTOR {

#else

typedef struct _VMGPADL_PACKET_HEADER {
    
    VMPACKET_DESCRIPTOR d;
    
#endif


    UINT32  Gpadl;
    UINT32  Reserved;
    
} VMGPADL_PACKET_HEADER, *PVMGPADL_PACKET_HEADER;

#ifdef __cplusplus

typedef struct _VMADD_REMOVE_TRANSFER_PAGE_SET : VMPACKET_DESCRIPTOR {

#else

typedef struct _VMADD_REMOVE_TRANSFER_PAGE_SET {
    
    VMPACKET_DESCRIPTOR d;
    
#endif
    
    UINT32  Gpadl;
    UINT16  TransferPageSetId;
    UINT16  Reserved;
    
} VMADD_REMOVE_TRANSFER_PAGE_SET, *PVMADD_REMOVE_TRANSFER_PAGE_SET;

#pragma pack(pop)

//
// This structure defines a range in guest physical space that can be made
// to look virtually contiguous.
// 

typedef struct _GPA_RANGE {

    UINT32  ByteCount;
    UINT32  ByteOffset;
    UINT64  PfnArray[0];

} GPA_RANGE, *PGPA_RANGE;



#pragma pack(push, 1)

//
// This is the format for an Establish Gpadl packet, which contains a handle
// by which this GPADL will be known and a set of GPA ranges associated with
// it.  This can be converted to a MDL by the guest OS.  If there are multiple
// GPA ranges, then the resulting MDL will be "chained," representing multiple
// VA ranges.
// 

#ifdef __cplusplus

typedef struct _VMESTABLISH_GPADL : VMPACKET_DESCRIPTOR {

#else

typedef struct _VMESTABLISH_GPADL {
    
    VMPACKET_DESCRIPTOR d;
    
#endif

    UINT32      Gpadl;
    UINT32      RangeCount;
    GPA_RANGE   Range[1];

} VMESTABLISH_GPADL, *PVMESTABLISH_GPADL;


//
// This is the format for a Teardown Gpadl packet, which indicates that the
// GPADL handle in the Establish Gpadl packet will never be referenced again.
//

#ifdef __cplusplus

typedef struct _VMTEARDOWN_GPADL : VMPACKET_DESCRIPTOR {

#else

typedef struct _VMTEARDOWN_GPADL {
    
    VMPACKET_DESCRIPTOR d;

#endif    

    UINT32  Gpadl;
    UINT32  Reserved; // for alignment to a 8-byte boundary
} VMTEARDOWN_GPADL, *PVMTEARDOWN_GPADL;


//
// This is the format for a GPA-Direct packet, which contains a set of GPA
// ranges, in addition to commands and/or data.
// 

#ifdef __cplusplus

typedef struct _VMDATA_GPA_DIRECT : VMPACKET_DESCRIPTOR {

#else

typedef struct _VMDATA_GPA_DIRECT {
    
    VMPACKET_DESCRIPTOR d;
    
#endif
    
    UINT32      Reserved;
    UINT32      RangeCount;
    GPA_RANGE   Range[1];
    
} VMDATA_GPA_DIRECT, *PVMDATA_GPA_DIRECT;



//
// This is the format for a Additional Data Packet.
// 

#ifdef __cplusplus

typedef struct _VMADDITIONAL_DATA : VMPACKET_DESCRIPTOR {

#else

typedef struct _VMADDITIONAL_DATA {
    
    VMPACKET_DESCRIPTOR d;
    
#endif
    
    UINT64  TotalBytes;
    UINT32  ByteOffset;
    UINT32  ByteCount;
    UCHAR   Data[1];
    
} VMADDITIONAL_DATA, *PVMADDITIONAL_DATA;


#pragma pack(pop)

typedef union {
    VMPACKET_DESCRIPTOR             SimpleHeader;
    VMTRANSFER_PAGE_PACKET_HEADER   TransferPageHeader;
    VMGPADL_PACKET_HEADER           GpadlHeader;
    VMADD_REMOVE_TRANSFER_PAGE_SET  AddRemoveTransferPageHeader;
    VMESTABLISH_GPADL               EstablishGpadlHeader;
    VMTEARDOWN_GPADL                TeardownGpadlHeader;
    VMDATA_GPA_DIRECT               DataGpaDirectHeader;
} VMPACKET_LARGEST_POSSIBLE_HEADER, *PVMPACKET_LARGEST_POSSIBLE_HEADER;

#define VMPACKET_DATA_START_ADDRESS(__packet)                           \
    (PVOID)(((PUCHAR)__packet) + ((PVMPACKET_DESCRIPTOR)__packet)->DataOffset8 * 8)

#define VMPACKET_DATA_LENGTH(__packet)                                  \
    ((((PVMPACKET_DESCRIPTOR)__packet)->Length8 - ((PVMPACKET_DESCRIPTOR)__packet)->DataOffset8) * 8)

#define VMPACKET_TRANSFER_MODE(__packet) ((PVMPACKET_DESCRIPTOR)__packet)->Type

typedef enum {
    VmbusServerEndpoint = 0,
    VmbusClientEndpoint,
    VmbusEndpointMaximum
} ENDPOINT_TYPE, *PENDPOINT_TYPE;

typedef enum {
    VmbusPacketTypeInvalid                      = 0x0,
    VmbusPacketTypeSynch                        = 0x1,
    VmbusPacketTypeAddTransferPageSet           = 0x2,
    VmbusPacketTypeRemoveTransferPageSet        = 0x3,
    VmbusPacketTypeEstablishGpadl               = 0x4,
    VmbusPacketTypeTearDownGpadl                = 0x5,
    VmbusPacketTypeDataInBand                   = 0x6,
    VmbusPacketTypeDataUsingTransferPages       = 0x7,
    VmbusPacketTypeDataUsingGpadl               = 0x8,
    VmbusPacketTypeDataUsingGpaDirect           = 0x9,
    VmbusPacketTypeCancelRequest                = 0xa,
    VmbusPacketTypeCompletion                   = 0xb,
    VmbusPacketTypeDataUsingAdditionalPackets   = 0xc,
    VmbusPacketTypeAdditionalData               = 0xd
} VMBUS_PACKET_TYPE, *PVMBUS_PACKET_TYPE;

#define VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED    1

#endif  /* __HV_VMBUS_PACKET_FORMAT_H__ */

