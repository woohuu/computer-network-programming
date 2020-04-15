/*
* Yi Liu
* UIN: 827008592
* CSCE612 Spring 2019
*/

#pragma once
#include "stdafx.h"
#include "connectionTest.h"

#pragma comment(lib, "ws2_32.lib")


#define INITIAL_BUF_SIZE 8192
#define THRESHOLD 256
#define ROBOTS_MAX_SIZE 16384
#define PAGE_MAX_SIZE 2097152

int connectionTest(struct sockaddr_in server, char *request, int type, char **buf, bool *valid)
{
	int max_size; // maximum data download size
	int iRet = 0;
	char validCode;
	*valid = false;

	if (type == 1)
	{
		printf("\tConnecting on robots... ");
		max_size = ROBOTS_MAX_SIZE;
		validCode = '4';
	}

	else if (type == 2)
	{
		printf("%7s Connecting on page... ", "*");
		max_size = PAGE_MAX_SIZE;
		validCode = '2';
	}
	else
	{
		printf("Undefined type\n");
		printf("Acceptable method type: 1 for ROBOTS; 2 for PAGE\n");
		return 1;
	}

	// open a TCP socket
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		printf("Socket() generated error %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	// get execution time of an operation
	int tSpent = 0;
	clock_t start = 0;
	
	start = clock();
	if (connect(sock, (struct sockaddr *)(&server), sizeof(struct sockaddr_in)) == SOCKET_ERROR)
	{
		printf("failed with %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}
	tSpent = (int)(1000 * (clock() - start) / (double)CLOCKS_PER_SEC);
	printf("done in %d ms\n", tSpent);

	// send request
	if (send(sock, request, (int)strlen(request), 0) == SOCKET_ERROR)
	{
		printf("Send error: %d\n", WSAGetLastError());
		closesocket(sock);
		WSACleanup();
		return 1;
	}

	// variables initiation
	char *recvbuf = new char[INITIAL_BUF_SIZE]; // receive buffer
	int iResult, bytes = -1, curPos = 0, allocatedSize = INITIAL_BUF_SIZE;
	fd_set fd; // file descriptor set structure
	struct timeval timeout;
	timeout.tv_sec = 10; // set timeout to be 10 seconds
	timeout.tv_usec = 0;

	// receive bytes returned from server
	printf("\tLoading... ");
	start = clock();
	while (true)
	{
		// initialize fd_set to zero and then set sock for every loop
		FD_ZERO(&fd);
		FD_SET(sock, &fd);
		// wait to see if socket has any data
		if ((iResult = select(0, &fd, 0, 0, &timeout)) > 0)
		{
			// new data available, read it into recvbuf
			// keep receiving until the peer close the connection
			if ((bytes = recv(sock, recvbuf + curPos, allocatedSize - curPos, 0)) > 0)
			{
				// update current writing position of recvbuf
				curPos += bytes;
				tSpent = (int)(1000 * (clock() - start) / (double)CLOCKS_PER_SEC);
				// check download speed, abort slow download
				if (tSpent > 10000)
				{
					printf("failed with slow download\n");
					iRet = 1;
					break;
				}

				// check download data size, abort pages above maximum allowed size
				if (curPos > max_size)
				{
					printf("failed with exceeding max\n");
					iRet = 1;
					break;
				}

				// double memory size for recvbuf once available space is less than THRESHOLD
				if (allocatedSize - curPos < THRESHOLD)
				{
					// allocate a new buffer with larger size and copy all bytes of old buffer 
					// into new and free the old buffer, then let recvbuf point to the new buffer
					allocatedSize *= 2;
					char *temp = new char[allocatedSize];
					memcpy(temp, recvbuf, curPos);
					delete recvbuf;
					recvbuf = temp;
				}

			}
			else if (bytes == 0)
			{
				// no more bytes to receive, connection closed
				recvbuf[curPos] = '\0'; // terminate the buffer
				tSpent = (int)(1000 * (clock() - start) / (double)CLOCKS_PER_SEC);
				// check if response has HTTP header
				if (strncmp(recvbuf, "HTTP/", 5) == 0)
				{
					*buf = recvbuf;
					printf("done in %d ms with %d bytes\n", tSpent, curPos);
					
					// Verifying status code in header
					printf("\tVerifying header... status code %c%c%c\n", recvbuf[9], recvbuf[10], recvbuf[11]);
					// check if status code is valid for specified requesting page type
					if (recvbuf[9] == validCode)
						*valid = true;
				}
				else
				{
					printf("failed with non-HTTP header\n");
					iRet = 1;
				}

				break;
			}
			else
			{
				// recv() return with error
				printf("failed with %d on recv\n", WSAGetLastError());
				iRet = 1;
				break;
			}
		}
		else if (iResult == 0)
		{
			printf("Connection timed out\n");
			iRet = 1;
			break;
		}
		else
		{
			printf("select() failed with %d\n", WSAGetLastError());
			iRet = 1;
			break;
		}
	}

	closesocket(sock);

	return iRet;
}