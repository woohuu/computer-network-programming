#include "SenderSocket.h"

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
		printf("[%6.3f] --> SYN %d (attempt %d of %d, RTO %.3f) to %s\n",
			curTime, ssh.sdh.seq, i, MAX_SYN_ATTEMPT, (rto.tv_sec + (float)rto.tv_usec / 1e6), inet_ntoa(server.sin_addr));

		// get ready to receive
		FD_ZERO(&fd);
		FD_SET(sock, &fd);
		if ((iResult = select(0, &fd, NULL, NULL, &rto)) > 0)
		{
			// new data available, read it into receiver packet
			int bytes = -1;
			if ((bytes = recvfrom(sock, (char *)&rh, sizeof(rh), 0, (struct sockaddr *)&response, &resSize)) > 0)
			{
				// update RTO
				updateTime();
				openEndTime = curTime;
				float rtt = curTime - prevTime;
				// set RTO to be 3 times of RTT
				rto.tv_sec = (long)(3 * rtt);
				rto.tv_usec = (long)((3 * rtt - (long)(3 * rtt)) * 1e6);
				printf("[%6.3f] <-- SYN-ACK %d window %d; setting initial RTO to %.3f\n",
					curTime, rh.ackSeq, rh.recvWnd, 3 * rtt);
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

int SenderSocket::Send(char *sndBuf, int size)
{

	return 0;
}

int SenderSocket::Close()
{
	updateTime();
	transT = curTime - openEndTime;	// get the duration time of a successful transfer
	// create FIN packet
	SenderDataHeader sdh;
	sdh.seq = 0;
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
		updateTime();
		printf("[%6.3f] --> FIN %d (attempt %d of %d, RTO %.3f)\n",
			curTime, sdh.seq, i, MAX_ATTEMPT, (rto.tv_sec + (float)rto.tv_usec / 1e6));

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
				printf("[%6.3f] <-- FIN-ACK %d window %d\n", curTime, rh.ackSeq, rh.recvWnd);
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