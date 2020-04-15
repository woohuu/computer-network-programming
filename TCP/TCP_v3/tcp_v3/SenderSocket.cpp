#include <WinSock2.h>
#include "stdafx.h"

#define MAX_SYN_ATTEMPT 3
#define MAX_ATTEMPT 5

SenderSocket::SenderSocket(int sndWnd)
{
	sock = INVALID_SOCKET;
	memset(&server, 0, sizeof(server));
	start = clock();
	prevTime = 0;
	curTime = 0;
	rto.tv_sec = 1;	// set initial RTO to 1 second
	rto.tv_usec = 0;
	openEndTime = 0;
	elapsedTime = 0;
	rtoInSec = 0;
	bytesACKed = 0;
	W = sndWnd;
	senderBase = 0;
	nextSeq = 0;
	nextToSend = 0;
	lastReleased = 0;
	newReleased = 0;
	pendingPkt = new Packet[W];	// allocate space for sending queue

	// event handles
	eventQuit = CreateEvent(NULL, false, false, NULL);	// auto-reset
	sendFinished = CreateEvent(NULL, true, false, NULL);
	arr = new HANDLE[4];

	// create semaphores with initial values of 0
	empty = CreateSemaphore(NULL, 0, W, NULL);
	full = CreateSemaphore(NULL, 0, W, NULL);
}

SenderSocket::~SenderSocket()
{
	// check if worker and stats thread are still running
	// terminate them before destruction
	WaitForMultipleObjects(2, arr, true, INFINITE);
	CloseHandle(arr[0]);
	CloseHandle(arr[1]);

	WaitForMultipleObjects(2, arr + 2, true, INFINITE);
	CloseHandle(arr[2]);
	CloseHandle(arr[3]);
	// delete shared data objects
	delete[] pendingPkt;
	delete[] arr;
}

void SenderSocket::updateTime()
{
	prevTime = curTime;	// store previous time
	curTime = (clock() - start) / (float)CLOCKS_PER_SEC;	// update current time
}

int SenderSocket::Open(char *host, int port)
{
	// create a UDP socket
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		printf("socket() failed with %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.S_un.S_addr = htonl(INADDR_ANY);  // bind to all local interfaces
	local.sin_port = htons(0);  // let OS select next available port

	// bind the socket
	if (bind(sock, (struct sockaddr *)(&local), sizeof(local)) == SOCKET_ERROR)
	{
		printf("bind() failed with %d\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	// set server info
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	// get server IP
	// struct for DNS lookup
	struct hostent *remote;

	// assume host string is a dot-formatted IP address
	DWORD IP = inet_addr(host);
	if (IP == INADDR_NONE)
	{
		// host string is not an IP address
		if ((remote = gethostbyname(host)) == NULL)
		{
			updateTime();
			printf("[%6.3f] --> target %s is invalid\n", curTime, host);
			WSACleanup();
			return INVALID_NAME;
		}
		else
		{
			memcpy(&(server.sin_addr), remote->h_addr, remote->h_length);
		}
	}
	else
	{
		// a valid IP, store it into server
		server.sin_addr.S_un.S_addr = IP;
	}

	// create SYN packet in pending queue
	Packet *pkt = pendingPkt + nextSeq % W;
	pkt->seq = nextSeq;
	pkt->type = SYN_PKT;
	pkt->size = sizeof(SenderSynHeader);
	SenderSynHeader *ssh = (SenderSynHeader *)(pkt->buf);
	ssh->sdh.seq = 0;
	ssh->sdh.flags = Flags();
	ssh->sdh.flags.SYN = 1;
	linkP->bufferSize = W + MAX_SYN_ATTEMPT;	// update bufferSize
	memcpy(&(ssh->lp), linkP, sizeof(LinkProperties));

	if (2 * linkP->RTT > 1.0)
	{
		// update RTO for SYN packet
		rto.tv_sec = (long)(2 * linkP->RTT);
		rto.tv_usec = (long)((2 * linkP->RTT - rto.tv_sec) * 1e6);
	}

	// variables for sendto and recvfrom
	int i = 1; // number of attempts
	int iResult = -1;	// return for select()
	fd_set fd;
	ReceiverHeader rh;	// receiver packet
	struct sockaddr_in response;
	int resSize = sizeof(response);

	while (i <= MAX_SYN_ATTEMPT)
	{
		if (sendto(sock, pkt->buf, pkt->size, 0, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
		{
			updateTime();
			printf("[%6.3f] --> failed sendto with %d\n", curTime, WSAGetLastError());
			WSACleanup();
			return FAILED_SEND;
		}
		updateTime();
		pkt->txTime = clock();	// transmission time

		// get ready to receive
		FD_ZERO(&fd);
		FD_SET(sock, &fd);
		if ((iResult = select(0, &fd, NULL, NULL, &rto)) > 0)
		{
			// new data available, read it into receiver packet
			int bytes = -1;
			if ((bytes = recvfrom(sock, (char *)&rh, sizeof(rh), 0, (struct sockaddr *)&response, &resSize)) > 0)
			{
				updateTime();
				openEndTime = curTime;
				float rtt = curTime - prevTime;
				// update RTO
				rto.tv_sec = (long)(rtt + 0.040);
				rto.tv_usec = (long)((rtt + 0.040 - rto.tv_sec) * 1e6);

				//flow control
				lastReleased = min(W, rh.recvWnd);
				ReleaseSemaphore(empty, lastReleased, NULL);
				lastReleased++; // adjust this number to make sure not over release semaphore which will cause senderBase got overwritten

				SetEvent(eventQuit);
				return STATUS_OK;
			}
			else if (bytes == SOCKET_ERROR)
			{
				updateTime();
				printf("[%6.3f] <-- failed recvfrom with %d\n", curTime, WSAGetLastError());
				WSACleanup();
				return FAILED_RECV;
			}
		}
		else if (iResult == 0)
		{
			if (i == MAX_SYN_ATTEMPT)
			{
				// timeout after maximum attempts
				return TIMEOUT;
			}
		}
		else
		{
			printf("select() failed with %d\n", WSAGetLastError());
		}

		i++;
	}

	return -1;
}

void SenderSocket::Send(char *sndBuf, int size)
{
	// wait for events
	HANDLE arr[] = { eventQuit, empty };
	WaitForMultipleObjects(2, arr, false, INFINITE);

	// create data packet in the pending Packet queue
	Packet *pkt = pendingPkt + nextSeq % W;
	pkt->seq = nextSeq;
	pkt->type = DATA_PKT;
	pkt->size = size;
	SenderDataHeader *sdh = (SenderDataHeader *)(pkt->buf);
	sdh->flags = Flags();
	sdh->seq = nextSeq;
	memcpy(sdh + 1, sndBuf, size);
	nextSeq++;
	ReleaseSemaphore(full, 1, NULL);
}

int SenderSocket::WorkerRun(Params *p)
{
	// increase sender/receiver buffer size
	int kernelBuffer = 20e6; // 20 meg
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&kernelBuffer, sizeof(int)) == SOCKET_ERROR)
	{
		printf("setsockopt() failed with %d\n", WSAGetLastError());
		return -1;
	}
	kernelBuffer = 20e6; // 20 meg
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&kernelBuffer, sizeof(int)) == SOCKET_ERROR)
	{
		printf("setsockopt() failed with %d\n", WSAGetLastError());
		return -1;
	}
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	
	HANDLE socketReceiveReady = CreateEvent(NULL, false, false, NULL);	// need to choose auto-reset attribute
	HANDLE events[] = { socketReceiveReady, full };

	if (WSAEventSelect(sock, socketReceiveReady, FD_READ) == SOCKET_ERROR)
	{
		printf("WSAEventSelect() failed with %d\n", WSAGetLastError());
		return -1;
	}

	ReceiverHeader rh;	// receiver packet
	struct sockaddr_in response;
	int resSize = sizeof(response);
	int bytes = -1;	// return result for recvfrom()
	int dupACK = 0;	// counter for duplicate ACKs
	int dataSize = MAX_PKT_SIZE - sizeof(SenderDataHeader);	// data size in a single packet (last packet may be different)

	float rtt = curTime - prevTime;	// measured sample RTT, initialize it with SYN packet RTT
	DWORD timeout = 0;
	// initialize rtoInSec with RTO from SYN packet
	rtoInSec = rto.tv_sec + rto.tv_usec * 1e-6;
	clock_t timerExpire = rtoInSec * CLOCKS_PER_SEC + clock();	// initialize timer with rto

	while (true)
	{
		if (senderBase < nextToSend)
		{
			if (timerExpire < clock())
			{
				// set timeout to be zero if timer has expired
				timeout = 0;
			}
			else
			{
				timeout = (timerExpire - clock()) / (float)CLOCKS_PER_SEC * 1000;
			}
			
		}
		else
		{
			timeout = INFINITE;
		}

		int ret = WaitForMultipleObjects(2, events, false, timeout);
		switch (ret)
		{
		case WAIT_TIMEOUT:
		{
			// timeout happened, retransmit packet at senderBase
			p->timeOutPkt += 1;
			if (sendto(sock, pendingPkt[senderBase % W].buf, pendingPkt[senderBase % W].size + sizeof(SenderDataHeader), 0, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
			{
				updateTime();
				printf("[%6.3f] --> failed sendto with %d\n", curTime, WSAGetLastError());
				WSACleanup();
				return FAILED_SEND;
			}
			// update packet txTime
			pendingPkt[senderBase % W].txTime = clock();
			timerExpire = rtoInSec * CLOCKS_PER_SEC + clock();	// restart timer
			break;
		}
		case WAIT_OBJECT_0:
		{
			// new data available from socket, read it into receiver packet
			if ((bytes = recvfrom(sock, (char *)&rh, sizeof(rh), 0, (struct sockaddr *)&response, &resSize)) > 0)
			{
				// check if ACKseq of receiver packet is larger than senderBase
				if (rh.ackSeq > senderBase)
				{
					// update RTT only when the original senderBase did not have prior retransmissions
					// i.e., its transmission time is smaller than the next packet
					if (senderBase < nextToSend - 1 && pendingPkt[senderBase % W].txTime < pendingPkt[(senderBase + 1) % W].txTime)
					{
						// use packet rh.ackSeq - 1 to calculate RTT
						rtt = (clock() - pendingPkt[(rh.ackSeq - 1) % W].txTime) / (float)CLOCKS_PER_SEC;
					}
					// move senderBase, update stats
					senderBase = rh.ackSeq;
					p->base = senderBase;
					dupACK = 0;

					// calculate bytes of data ACKed
					if (senderBase <= p->bufferSize / dataSize)
					{
						// current packet and all previous ones have same default size
						bytesACKed = senderBase * dataSize;
					}
					else
					{
						// this is the last data packet
						bytesACKed = p->bufferSize;	// total buffer has been ACKed
					}
					p->amountACKed = bytesACKed * 1e-6;	// data amount in Mb
					p->effectiveWin = min(W, rh.recvWnd);
					p->nextSeq = nextSeq;

					// how much we can advance the semaphore
					newReleased = senderBase + p->effectiveWin - lastReleased;
					ReleaseSemaphore(empty, newReleased, NULL);
					lastReleased += newReleased;
					// update estimated RTT based on equation from course slides
					if (senderBase == 1)
					{
						p->estimatedRTT = rtt;
					}
					else
					{
						p->estimatedRTT = 0.875 * p->estimatedRTT + 0.125 * rtt;
						p->devRTT = 0.75 * p->devRTT + 0.25 * abs(rtt - p->estimatedRTT);
					}
					// update RTO based on given equation in hw3p2 pdf
					rtoInSec = p->estimatedRTT + 4 * max(p->devRTT, 0.010);
					// update timer
					if (senderBase < nextToSend)
					{
						timerExpire = rtoInSec * CLOCKS_PER_SEC + pendingPkt[senderBase % W].txTime;	// update timer
					}
					else if (senderBase == nextToSend)
					{
						timerExpire = rtoInSec * CLOCKS_PER_SEC + clock();	// restart timer
					}

					if (bytesACKed >= p->bufferSize)
					{
						// all bytes in buffer have been tranferred
						p->sendQuit = true;
						SetEvent(sendFinished);
						updateTime();
						elapsedTime = curTime - openEndTime;

						return STATUS_OK;
					}
					
					break;
				}
				else if (rh.ackSeq == senderBase)
				{
					// duplicate ACK received
					dupACK++;
					if (dupACK == 3)
					{
						// fast retransmission triggered
						p->fastRtxPkt += 1;
						// retransmit Packet at senderBase
						if (sendto(sock, pendingPkt[senderBase % W].buf, pendingPkt[senderBase % W].size + sizeof(SenderDataHeader), 0, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
						{
							updateTime();
							printf("[%6.3f] --> failed sendto with %d\n", curTime, WSAGetLastError());
							WSACleanup();
							return FAILED_SEND;
						}
						// update packet txTime
						pendingPkt[senderBase % W].txTime = clock();
						timerExpire = rtoInSec * CLOCKS_PER_SEC + clock();	// restart timer
					}

					break;
				}
			}
			else if (bytes == SOCKET_ERROR)
			{
				updateTime();
				printf("[%6.3f] <-- failed recvfrom with %d\n", curTime, WSAGetLastError());
				return FAILED_RECV;
			}
		}
		case WAIT_OBJECT_0 + 1:
		{
			// Packet ready to be sent in the pendingPkt queue
			if (sendto(sock, pendingPkt[nextToSend % W].buf, pendingPkt[nextToSend % W].size + sizeof(SenderDataHeader), 0, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
			{
				updateTime();
				printf("[%6.3f] --> failed sendto with %d\n", curTime, WSAGetLastError());
				return FAILED_SEND;
			}
			// record packet txTime
			pendingPkt[nextToSend % W].txTime = clock();
			nextToSend++;
			
			break;
		}
		default:
			// WAIT_FAILED
			printf("WaitForMultipleObjects() failed with %d\n", WSAGetLastError());
		}
	}

	return -1;
}

int SenderSocket::Close()
{
	WaitForSingleObject(sendFinished, INFINITE);
	// create FIN packet
	SenderDataHeader sdh;
	sdh.seq = senderBase;
	sdh.flags.FIN = 1;

	// variables for sendto and recvfrom
	int i = 1;	// number of attempts
	int iResult = -1;	// return for select()
	fd_set fd;
	ReceiverHeader rh;	// receiver packet
	struct sockaddr_in response;
	int resSize = sizeof(response);

	while (i <= MAX_ATTEMPT)
	{
		if (sendto(sock, (char *)&sdh, sizeof(sdh), 0, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
		{
			updateTime();
			printf("[%6.3f] --> failed sendto with %d\n", curTime, WSAGetLastError());
			WSACleanup();
			return FAILED_SEND;
		}

		// get ready to receive
		FD_ZERO(&fd);
		FD_SET(sock, &fd);
		if ((iResult = select(0, &fd, NULL, NULL, &rto)) > 0)
		{
			// new data available, read it into receiver packet
			int bytes = -1;
			if ((bytes = recvfrom(sock, (char *)&rh, sizeof(rh), 0, (struct sockaddr *)&response, &resSize)) > 0)
			{
				updateTime();
				printf("[%.3f] <-- FIN-ACK %d window %X\n", curTime, rh.ackSeq, rh.recvWnd);
				closesocket(sock);
				return STATUS_OK;
			}
			else if (bytes == SOCKET_ERROR)
			{
				updateTime();
				printf("[%6.3f] <-- failed recvfrom with %d\n", curTime, WSAGetLastError());
				WSACleanup();
				return FAILED_RECV;
			}
		}
		else if (iResult == 0)
		{
			if (i == MAX_ATTEMPT)
			{
				// timeout after maximum attempts
				return TIMEOUT;
			}
		}
		else
		{
			printf("select() failed with %d\n", WSAGetLastError());
		}

		i++;
	}

	return -1;
}