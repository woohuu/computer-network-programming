#pragma once
#include "stdafx.h"

#define FORWARD_PATH 0
#define RETURN_PATH 1

#define MAGIC_PROTOCOL 0x8311AA

#pragma pack(push, 1)
class LinkProperties {
public:
	// transfer parameters
	float RTT;		// propagation RTT (in sec)
	float speed;	// bottleneck bandwith (in bits/sec)
	float pLoss[2];	// probability of loss in each direction
	DWORD bufferSize;	// buffer size of emulated routers (in packets)

	LinkProperties() {
		memset(this, 0, sizeof(*this));
	}
};

class Flags {
public:
	DWORD reserverd:5;
	DWORD SYN:1;
	DWORD ACK:1;
	DWORD FIN:1;
	DWORD magic:24;

	Flags() {
		memset(this, 0, sizeof(*this));
		magic = MAGIC_PROTOCOL;
	}
};

class SenderDataHeader {
public:
	Flags flags;
	DWORD seq;	// must begin from 0
};

class SenderSynHeader {
public:
	SenderDataHeader sdh;
	LinkProperties lp;
};

class ReceiverHeader {
public:
	Flags flags;
	DWORD recvWnd;	// receiver window for flow control (in pkts)
	DWORD ackSeq;	// ack value = next expected sequence
};

#pragma pack(pop)  // restore old packing