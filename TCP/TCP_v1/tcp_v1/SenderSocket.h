#pragma once
#include "stdafx.h"
#include "Header.h"

#define MAGIC_PORT 22345
#define MAX_PKT_SIZE (1500 - 28)

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
	float transT;	// time spent for transferring
	struct timeval rto;	// retransmission timeout

	SenderSocket();
	int Open(char*, int, int, LinkProperties*);
	int Send(char*, int);
	int Close();
	void updateTime();

};