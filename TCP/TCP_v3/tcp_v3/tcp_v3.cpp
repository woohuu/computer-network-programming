#pragma once
#include "stdafx.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

// event handles
HANDLE statsQuit = CreateEvent(NULL, true, false, NULL);
HANDLE workerQuit = CreateEvent(NULL, true, false, NULL);

void statsT(LPVOID *param) {

	Params *p = (Params *)param;
	DWORD oldBase = 0;
	float oldTime = 0;
	while (true)
	{
		// wait for 2 sec at the very beginning
		while ((clock() - p->start) / (float)CLOCKS_PER_SEC < 2.0)
			;
		float curT = (clock() - p->start) / (float)CLOCKS_PER_SEC;
		// calculate transfer speed
		p->speed = 8 * (p->base - oldBase) * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) / (curT - oldTime) * 1e-6;
		oldBase = p->base;
		oldTime = curT;
		printf("[%3d] B%8d (%5.1f MB) N%8d T %d F %d W %d S %.3f Mbps RTT %.3f\n",
			(int)(round(curT)), p->base, p->amountACKed, p->nextSeq, p->timeOutPkt,
			p->fastRtxPkt, p->effectiveWin, p->speed, p->estimatedRTT);
		Sleep(2000);
		if (p->sendQuit)
			break;
	}
	SetEvent(statsQuit);
}

void producer(LPVOID *pdParam)
{
	PdParams *p = (PdParams *)pdParam;
	UINT64 off = 0;
	while (off < p->bufSize)
	{
		// decide the size of next chunk
		int bytes = min(p->bufSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));
		// send chunk into socket
		p->sndSock->Send(p->data + off, bytes);

		off += bytes;
	}
}

void worker(LPVOID *wkParam)
{
	WkParams *p = (WkParams *)wkParam;
	int status;
	if ((status = p->sndSock->WorkerRun(p->params)) != STATUS_OK)
	{
		// error handling
		printf("%-7s send failed with status %d\n", "Main:", status);
	}
	SetEvent(workerQuit);
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

	SenderSocket ss(sndWnd);
	ss.linkP = &lp;
	memcpy(ss.arr, &statsQuit, sizeof(HANDLE));
	memcpy(ss.arr + 1, &workerQuit, sizeof(HANDLE));

	Params* statsP = new Params;
	statsP->start = ss.start;
	statsP->effectiveWin = sndWnd;
	statsP->bufferSize = byteBufferSize;

	HANDLE statsHdl;
	statsHdl = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)statsT, (LPVOID)statsP, 0, NULL);
	int status, pktSize = MAX_PKT_SIZE;

	if ((status = ss.Open(targetHost, MAGIC_PORT)) != STATUS_OK)
	{
		printf("%-7s connect failed with status %d\n", "Main:", status);
		delete[] dwordBuf;
		delete statsP;
		closesocket(ss.sock);
		WSACleanup();
		return -1;
	}
	printf("%-7s connected to %s in %.3f second, pkt size %d bytes\n",
		"Main:", targetHost, ss.curTime - ss.prevTime, pktSize);

	PdParams *pdParam = new PdParams;
	pdParam->bufSize = byteBufferSize;
	pdParam->data = charBuf;
	pdParam->sndSock = &ss;
	HANDLE pdHdl = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)producer, (LPVOID)pdParam, 0, NULL);
	
	WkParams *wkParam = new WkParams;
	wkParam->params = statsP;
	wkParam->sndSock = &ss;
	HANDLE workerHdl = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)worker, (LPVOID)wkParam, 0, NULL);
	memcpy(ss.arr + 2, &statsHdl, sizeof(HANDLE));
	memcpy(ss.arr + 3, &workerHdl, sizeof(HANDLE));

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
	
	float idealRate = 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) * sndWnd / (statsP->estimatedRTT) * 1e-3;
	printf("%-7s estRTT %.3f, ideal rate %.2f Kbps\n", "Main:", statsP->estimatedRTT, idealRate);
	
	delete[] dwordBuf;
	delete statsP, pdParam, wkParam;
	WSACleanup();

	return 0;
}

