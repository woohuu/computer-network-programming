#pragma once
#include "headers.h"

#define MAGIC_PORT 22345

// possible status codes from ss.Open, ss.Send, ss.Close
#define STATUS_OK 0 // no error
#define ALREADY_CONNECTED 1 // second call to ss.Open() without closing connection
#define NOT_CONNECTED 2 // call to ss.Send()/Close() without ss.Open()
#define INVALID_NAME 3 // ss.Open() with targetHost that has no DNS entry
#define FAILED_SEND 4 // sendto() failed in kernel
#define TIMEOUT 5 // timeout after all retx attempts are exhausted
#define FAILED_RECV 6 // recvfrom() failed in kernel

class SenderSocket {
public:
	SOCKET sock;
	struct sockaddr_in server;	// store server info
	clock_t start;	// record start time when instance is created
	float prevTime; // time when previous operation was done
	float curTime;	// current time in seconds
	float openEndTime;	// time when Open() returns
	float elapsedTime;	//  duration between the transmission of the first data packet and receipt of the last data ACK
	float rtoInSec;	// RTO in second unit
	struct timeval rto;	// retransmission timeout in timeval struct
	UINT64 bytesACKed;	// total bytes of data transferred to receiver
	LinkProperties *linkP;	// link properties
	DWORD W;	// sender window size
	DWORD senderBase;	// senderBase
	DWORD nextSeq;	// nextSequence
	DWORD nextToSend; // next Packet to be sent
	DWORD lastReleased;	// cumulative released spaces
	DWORD newReleased;	// current released spaces
	Packet *pendingPkt;	// pending queue for packets

	// event handles
	HANDLE eventQuit;	// ss.Open() finished
	HANDLE sendFinished;
	HANDLE *arr;

	// two sempaphores for packet sending queue
	HANDLE empty;
	HANDLE full;

	SenderSocket(int);
	~SenderSocket();
	int Open(char*, int);
	void Send(char*, int);
	int WorkerRun(Params *p);
	int Close();
	void updateTime();

};