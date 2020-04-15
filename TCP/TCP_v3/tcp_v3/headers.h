#pragma once
#include <Windows.h>
#include <time.h> 

#define FORWARD_PATH 0
#define RETURN_PATH 1

#define MAX_PKT_SIZE (1500 - 28)

#define MAGIC_PROTOCOL 0x8311AA

// packet type
#define SYN_PKT 0
#define DATA_PKT 1
#define FIN_PKT 2

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

class Params {
public:
	DWORD base;	// sender base
	float amountACKed;	// data that has been ACKed by the receiver in MB
	DWORD nextSeq;	// next sequence number
	DWORD timeOutPkt;	// number of packets with timeouts
	DWORD fastRtxPkt;	// number of packets with fast retransmission
	DWORD effectiveWin;	// effective window size
	float speed;	// speed at which the application consumes data at the receiver (i.e., goodput), in Mbps
	float estimatedRTT;
	float devRTT;	// deviation of SampleRTT from EstimatedRTT
	clock_t start;	// time when a SenderSocket object was created
	bool sendQuit;	// signal if total buffer is tranferred
	UINT64 bufferSize;	// buffer size

	Params()
	{
		memset(this, 0, sizeof(*this));
		timeOutPkt = 0;
		fastRtxPkt = 0;
		devRTT = 0;
		sendQuit = false;
	}
};

class Checksum {
public:
	DWORD crc_table[256];

	Checksum();
	DWORD CRC32(unsigned char*, size_t);
};

class Packet {
public:
	int seq;	// for easy access in worker thread
	int type;	// SYN, FIN, data
	int size;	// for retx in worker thread
	clock_t txTime;	// transmission time
	char *buf;	// actual packet with header

	Packet()
	{
		buf = new char[MAX_PKT_SIZE];
		memset(buf, 0, MAX_PKT_SIZE);
	}
	~Packet()
	{
		delete[] buf;
	}
};


#pragma pack(pop)  // restore old packing