#pragma once
#include "stdafx.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

void statsT(LPVOID *param) {

	Params *p = (Params *)param;
	while (true)
	{
		// wait for 2 sec at the very beginning
		while ((clock() - p->start) / (float)CLOCKS_PER_SEC < 2.0)
			;
		float curT = (clock() - p->start) / (float)CLOCKS_PER_SEC;
		printf("[%2d] B%8d (%5.1f MB) N%8d T %d F %d W %d S %.3f Mbps RTT %.3f\n",
			(int)(round(curT)), p->base, p->amountACKed, p->nextSeq, p->timeOutPkt,
			p->fastRtxPkt, p->effectiveWin, p->speed, p->estimatedRTT);
		Sleep(2000);
		if (p->sendQuit)
			break;
	}

}


int main(int argc, char **argv)
{
	if (argc != 8) 
	{
		printf("Usage: %s Host inBufSize sndWnd RTT fLossRate rLossRate bnSpeed\n", argv[0]);
		printf("Must have 7 arguments\n");
		printf("Host -- hostname/IP of destination server\n");
		printf("inBufSize -- a power-of-2 buffer size to be transimitted (in DWORDS)\n");
		printf("sndWnd -- sender window (in packets)\n");
		printf("RTT -- round trip propagation delay (in seconds)\n");
		printf("fLossRate -- loss rate in forward direction\n");
		printf("rLossRate -- loss rate in return direction\n");
		printf("bnSpeed -- speed of the bottleneck link (in Mbps)\n");
		return 1;
	}

	// parse command-line arguments
	char *targetHost = argv[1];
	int power = atoi(argv[2]);
	int sndWnd = atoi(argv[3]);
	LinkProperties lp;
	lp.RTT = atof(argv[4]);
	lp.pLoss[FORWARD_PATH] = atof(argv[5]);
	lp.pLoss[RETURN_PATH] = atof(argv[6]);
	lp.speed = 1e6 * atof(argv[7]);	// convert from megabits to bits
	
	// print input arguments
	printf("%-7s sender W = %d, RTT %.3f sec, loss %g / %g, link %s Mbps\n",
		"Main:", sndWnd, lp.RTT, lp.pLoss[FORWARD_PATH], lp.pLoss[RETURN_PATH], argv[7]);

	// get execution time of an operation
	int tSpent = 0;
	clock_t start = 0;

	// initializing DWORD array
	UINT64 dwordBufSize = (UINT64)1 << power;
	DWORD *dwordBuf = new DWORD[dwordBufSize];	// user requested buffer

	char *charBuf = (char*)dwordBuf;	// this buffer goes into socket
	UINT64 byteBufferSize = dwordBufSize << 2;	// convert to bytes

	start = clock();
	for (UINT64 i = 0; i < dwordBufSize; i++)	// required initialization
	{
		dwordBuf[i] = i;
	}
	tSpent = (int)(1000 * (clock() - start) / (double)CLOCKS_PER_SEC);
	printf("%-7s initializing DWORD array with 2^%d elements... done in %d ms\n", "Main:", power, tSpent);

	// initialize WinSock, once per program
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup error %d\n", WSAGetLastError());
		return 1;
	}

	SenderSocket ss;

	Params* p = new Params;
	p->start = ss.start;
	p->effectiveWin = sndWnd;
	p->bufferSize = byteBufferSize;

	HANDLE statsHdl;
	statsHdl = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)statsT, (LPVOID)p, 0, NULL);
	int status, pktSize = MAX_PKT_SIZE;

	if ((status = ss.Open(targetHost, MAGIC_PORT, sndWnd, &lp)) != STATUS_OK)
	{
		printf("%-7s connect failed with status %d\n", "Main:", status);
		return -1;
	}
	printf("%-7s connected to %s in %.3f second, pkt size %d bytes\n",
		"Main:", targetHost, ss.curTime - ss.prevTime, pktSize);

	
	UINT64 off = 0;	// current position in the buffer
	

	while (off < byteBufferSize)
	{
		// decide the size of next chunk
		int bytes = min(byteBufferSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));
		// send chunk into socket
		if ((status = ss.Send(charBuf + off, bytes, p)) != STATUS_OK)
		{
			// error handling
			printf("%-7s send failed with status %d\n", "Main:", status);
			return -1;
		}
		off += bytes;
	}

	if ((status = ss.Close()) != STATUS_OK)
	{
		// error handling
		printf("%-7s finish failed with status %d\n", "Main:", status);
		return -1;
	}

	// print overall stats for this data transfer
	float avgSpeed = 8 * byteBufferSize / ss.elapsedTime * 1e-3;
	Checksum cs;
	DWORD check = cs.CRC32((unsigned char *)charBuf, byteBufferSize);
	printf("%-7s transfer finished in %.3f sec, %.2f Kbps, checksum %X\n", "Main:", ss.elapsedTime, avgSpeed, check);
	
	float idealRate = 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) / (p->estimatedRTT) * 1e-3;
	printf("%-7s estRTT %.3f, ideal rate %.2f Kbps\n", "Main:", p->estimatedRTT, idealRate);
	

	delete[] dwordBuf;
	delete p;
	WSACleanup();

	return 0;
}

