/*
* Yi Liu
* UIN: 827008592
* CSCE612 Spring 2019
* part of code adapted from 463-sample.zip
*/


#include "stdafx.h"
#include "winsock_test.h"

#pragma comment(lib, "ws2_32.lib")

#define INITIAL_BUF_SIZE 8192
#define THRESHOLD 256

void winsock_test(char *host, int port, char *request)
{
	WSADATA wsaData;

	// initialize WinSock, once per program
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup error %d\n", WSAGetLastError());
		return;
	}

	// open a TCP socket
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		printf("Socket() generated error %d\n", WSAGetLastError());
		WSACleanup();
		return;
	}

	// structure for DNS lookups
	struct hostent *remote;
	// structure for server info
	struct sockaddr_in server;
	// get execution time of an operation
	int tSpent = 0;
	clock_t start = 0;

	printf("\tDoing DNS... ");
	// assume host string is an dot-formatted IP address
	DWORD IP = inet_addr(host);
	if (IP == INADDR_NONE)
	{
		// if host name is not an IP address, do a DNS lookup
		start = clock();
		if ((remote = gethostbyname(host)) == NULL)
		{
			printf("failed with %d\n", WSAGetLastError());
			WSACleanup();
			return;
		}
		else {
			tSpent = (int)(1000 * (clock() - start) / (double)CLOCKS_PER_SEC);
			memcpy((char *)&(server.sin_addr), remote->h_addr, remote->h_length);
			printf("done in %d ms, found %s\n", tSpent, inet_ntoa(server.sin_addr));
		}
	}
	else
	{
		// if a valid IP, directly store its binary version into sin_addr
		server.sin_addr.S_un.S_addr = IP;
		printf("done in 0 ms, found %s\n", host);
	}	

	// set up server parameters of address family and port
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	// connect socket to server
	printf("%7s Connecting on page... ", "*");
	start = clock();
	if (connect(sock, (struct sockaddr *)(&server), sizeof(struct sockaddr_in)) == SOCKET_ERROR)
	{
		printf("failed with %d\n", WSAGetLastError());
		WSACleanup();
		return;
	}
	tSpent = (int)(1000 * (clock() - start) / (double)CLOCKS_PER_SEC);
	printf("done in %d ms\n", tSpent);

	// send request
	if (send(sock, request, (int)strlen(request), 0) == SOCKET_ERROR)
	{
		printf("Send error: %d\n", WSAGetLastError());
		closesocket(sock);
		WSACleanup();
		return;
	}

	// variables initiation
	char *recvbuf = new char[INITIAL_BUF_SIZE]; // receive buffer
	int iResult, bytes = -1, curPos = 0, allocatedSize = INITIAL_BUF_SIZE;
	fd_set fd; // file descriptor set structure
	struct timeval timeout;
	timeout.tv_sec = 10; // set timeout to be 10 seconds
	timeout.tv_usec = 0;
	bool httpHeader = false; // http header flag

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
				if (allocatedSize - curPos < THRESHOLD)
				{
					// resize memory for recvbuf to increment its size by INITIAL_BUF_SIZE
					// allocate a new buffer with larger size and copy all bytes of old buffer 
					// into new and free the old buffer, then let recvbuf point to the new buffer
					allocatedSize *= 2;
					/* char *newbuf = (char *)realloc(recvbuf, allocatedSize);
					if (newbuf == NULL)
					{
						printf("realloc() failed with %d\n", WSAGetLastError());
						return;
					}
					recvbuf = newbuf; */
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
					printf("done in %d ms with %d bytes\n", tSpent, curPos);
					httpHeader = true;
				}
				else
					printf("failed with non-HTTP header\n");
				break;
			}
			else
			{
				// recv() return with error
				printf("failed with %d on recv\n", WSAGetLastError());
				break;
			}
		}
		else if (iResult == 0)
		{
			printf("Connection timed out\n");
			break;
		}
		else
		{
			printf("select() failed with %d\n", WSAGetLastError());
			break;
		}
	}

	// parse response only when there is http header
	if (httpHeader)
	{
		// print HTTP status code in response
		printf("\tVerifying header... status code %c%c%c\n", recvbuf[9], recvbuf[10], recvbuf[11]);
		
		// find the position where http header part ends, search for "\r\n\r\n"
		char *headerEnd = strstr(recvbuf, "\r\n\r\n");
		
		// proceed to parse HTML only if status code is 2XX
		if (recvbuf[9] == '2')
		{
			printf("%7s Parsing page... ", "+");
			start = clock();
			// parse html code for links
			// create a new parser object
			HTMLParserBase *parser = new HTMLParserBase;
			char *htmlBegin = headerEnd + 4; // html code buffer
			// create base url
			int baseUrlSize = (int)strlen(host) + 8;
			char *baseUrl = new char[baseUrlSize];
			strcpy(baseUrl, "http://");
			strcat(baseUrl, host);

			int nLinks; // number of links found
			parser->Parse(htmlBegin, (int)strlen(htmlBegin), baseUrl, baseUrlSize - 1, &nLinks);
			
			tSpent = (int)(1000 * (clock() - start) / (double)CLOCKS_PER_SEC);
			// check for errors as indicated by negative values
			if (nLinks < 0)
				nLinks = 0;
			printf("done in %d ms with %d links\n", tSpent, nLinks);
			// free memory
			delete baseUrl, parser;

		}

		// cut off HTML code, print only http header part
		*headerEnd = '\0';
		printf("\n----------------------------------------------\n");
		printf("%s\n", recvbuf);
	}

	// free memory allocated for recvbuf
	delete recvbuf;

	closesocket(sock);
	WSACleanup();

}