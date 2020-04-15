#include "stdafx.h"

#define MAX_SYN_ATTEMPT 3
#define MAX_ATTEMPT 5

SenderSocket::SenderSocket()
{
	sock = INVALID_SOCKET;
	memset(&server, 0, sizeof(server));
	start = clock();
	prevTime = 0;
	curTime = 0;
	rto.tv_sec = 1;	// set initial RTO to 1 second
	rto.tv_usec = 0;
	openEndTime = 0;
	transT = 0;
	elapsedTime = 0;
	bytesSent = 0;
	base = 0;
}

void SenderSocket::updateTime()
{
	prevTime = curTime;	// store previous time
	curTime = (clock() - start) / (float)CLOCKS_PER_SEC;	// update current time
}

int SenderSocket::Open(char *host, int port, int sndWnd, LinkProperties *lp)
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
	// struct for DNS lookups
	struct hostent *remote;

	// assume host string is an dot-formatted IP address
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

	// create SYN packet
	SenderSynHeader ssh;
	ssh.sdh.seq = 0;
	ssh.sdh.flags.SYN = 1;
	lp->bufferSize = sndWnd + MAX_SYN_ATTEMPT;	// update bufferSize
	memcpy(&ssh.lp, lp, sizeof(*lp));

	if (2 * lp->RTT > 1.0)
	{
		// update RTO for SYN packet
		rto.tv_sec = (long)(2 * lp->RTT);
		rto.tv_usec = (long)((2 * lp->RTT - rto.tv_sec) * 1e6);
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
		if (sendto(sock, (char *)&ssh, sizeof(ssh), 0, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
		{
			updateTime();
			printf("[%6.3f] --> failed sendto with %d\n", curTime, WSAGetLastError());
			WSACleanup();
			return FAILED_SEND;
		}
		updateTime();

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

int SenderSocket::Send(char *sndBuf, int size, Params *p)
{
	// create data packet which is made of two parts: SenderDataHeader and actual data
	SenderDataHeader sdh;
	sdh.seq = p->base;
	char packet[MAX_PKT_SIZE];
	memset(packet, 0, MAX_PKT_SIZE);
	memcpy(packet, &sdh, sizeof(sdh));
	memcpy(packet + sizeof(sdh), sndBuf, size);

	// variables for sendto and recvfrom
	int i = 1;	// number of attempts
	int iResult = -1;	// return for select()
	fd_set fd;
	ReceiverHeader rh;	// receiver packet
	struct sockaddr_in response;
	int resSize = sizeof(response);
	int dupACK = 0;

	while (true)
	{
		if (sendto(sock, packet, sizeof(sdh) + size, 0, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
		{
			updateTime();
			printf("[%6.3f] --> failed sendto with %d\n", curTime, WSAGetLastError());
			WSACleanup();
			return FAILED_SEND;
		}
		// record time after packet has been sent
		updateTime();

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
				float rtt = curTime - prevTime;

				// check if ACKseq of receiver packet is expected
				if (rh.ackSeq == sdh.seq + 1)
				{
					// update stats
					p->base = rh.ackSeq;
					base = p->base;
					dupACK = 0;
					p->nextSeq = p->base + 1;
					p->amountACKed += size * 1e-6;
					p->speed = 8 * size / rtt * 1e-6;
					p->effectiveWin = min(p->effectiveWin, rh.recvWnd);
					// update estimated RTT based on equation from course slides
					if (sdh.seq == 0)
					{
						p->estimatedRTT = rtt;
					}
					else
					{
						p->estimatedRTT = 0.875 * p->estimatedRTT + 0.125 * rtt;
						p->devRTT = 0.75 * p->devRTT + 0.25 * abs(rtt - p->estimatedRTT);
					}
					// update RTO based on given equation in hw3p2 pdf
					float rtoInSec = p->estimatedRTT + 4 * max(p->devRTT, 0.010);
					rto.tv_sec = (long)rtoInSec;
					rto.tv_usec = (long)((rtoInSec - rto.tv_sec) * 1e6);
					
					bytesSent += size;
					if (bytesSent >= p->bufferSize)
					{
						// all bytes in buffer have been tranferred
						p->sendQuit = true;
						elapsedTime = curTime - openEndTime;
					}
					return STATUS_OK;
				}
				else if (rh.ackSeq == sdh.seq)
				{
					dupACK++;
					if (dupACK == 3)
					{
						// fast retransmission triggered
						p->fastRtxPkt += 1;
						continue;
					}
				}
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
			// timeout happened
			p->timeOutPkt += 1;
		}
		else
		{
			printf("select() failed with %d\n", WSAGetLastError());
		}
	}

	return -1;
}

int SenderSocket::Close()
{
	updateTime();
	transT = curTime - openEndTime;	// get the duration time of a successful transfer
	// create FIN packet
	SenderDataHeader sdh;
	sdh.seq = base;
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
				printf("[%.2f] <-- FIN-ACK %d window %X\n", curTime, rh.ackSeq, rh.recvWnd);
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