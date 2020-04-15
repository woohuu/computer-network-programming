/*
* Yi Liu
* UIN: 827008592
* CSCE612 Spring 2019
*/

#pragma once
#include "stdafx.h"

#define ROBOTS 1
#define PAGE 2

using namespace std;

int main(int argc, char **argv)
{
	if (argc == 1 || argc > 3)
	{
		printf("Usage: %s [nThreads=1] fileName\n", argv[0]);
		printf("Program takes 1 or 2 command-line arguments\n");
		return 1;
	}

	if (argc == 3 && strcmp(argv[1], "1") != 0)
	{	
		printf("Usage: %s [nThreads=1] fileName\n", argv[0]);
		printf("nThreads has to be 1\n");
		return 1;
	}

	char *fileName = NULL, *fileBuf = NULL;

	if (argc == 2)
		fileName = argv[1];
	else
		fileName = argv[2];

	// open file and read data into fileBuf
	if (openFile(fileName, &fileBuf) != 0)
	{
		printf("File open failed\n");
		return 1;
	}

	WSADATA wsaData;
	// initialize WinSock, once per program
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup error %d\n", WSAGetLastError());
		return 1;
	}


	// structure for DNS lookups
	struct hostent *remote;
	// structure for server info
	struct sockaddr_in server;
	DWORD IP;
	// get execution time of an operation
	int tSpent = 0;
	clock_t tStart = 0;

	char *host = NULL, *request = NULL;
	int urlSize, port;
	unordered_set<string> seenHosts;
	unordered_set<DWORD> seenIPs;
	seenHosts.clear();
	seenIPs.clear();
	int hostSetSize = 0, IPSetSize = 0;
	char *newlinePos = NULL, *start = fileBuf, *recvBuf = NULL;
	char url[MAX_HOST_LEN + 1];
	bool isValid;

	while ((newlinePos = strstr(start, "\r\n")) != NULL)
	{
		// get URL line by line
		strncpy(url, start, newlinePos - start);
		url[newlinePos - start] = '\0'; // terminate the url string
		urlSize = (int)strlen(url);

		// URL validation, extract host, port from URL and create http request
		urlPreprocessor(url, &port, &request);
		host = url + 7; // strip the beginning "http://"

		// check host uniqueness
		printf("\tChecking host uniqueness... ");
		string h(host); // convert host in C-string form to C++ string object
		seenHosts.insert(h);
		if (seenHosts.size() > hostSetSize)
		{
			// host not seen
			hostSetSize++;
			printf("passed\n");

			// Doing DNS lookup
			printf("\tDoing DNS... ");
			// assume host string is an dot-formatted IP address
			IP = inet_addr(host);
			if (IP == INADDR_NONE)
			{
				// if host name is not an IP address, do a DNS lookup
				tStart = clock();
				if ((remote = gethostbyname(host)) == NULL)
				{
					printf("failed with %d\n", WSAGetLastError());
					// abort current url and continue to next one
					delete request;
					start = newlinePos + 2;
					continue;
				}
				else {
					tSpent = (int)(1000 * (clock() - tStart) / (double)CLOCKS_PER_SEC);
					memcpy((char *)&(server.sin_addr), remote->h_addr, remote->h_length);
					printf("done in %d ms, found %s\n", tSpent, inet_ntoa(server.sin_addr));
					IP = inet_addr(inet_ntoa(server.sin_addr));
				}
			}
			else
			{
				// if a valid IP, directly store its binary version into sin_addr
				server.sin_addr.S_un.S_addr = IP;
				printf("done in 0 ms, found %s\n", host);
			}

			// check IP uniqueness
			printf("\tChecking IP uniqueness... ");
			seenIPs.insert(IP);
			if (seenIPs.size() > IPSetSize)
			{
				// IP is unique
				IPSetSize++;
				printf("passed\n");

				// set up server parameters of address family and port
				server.sin_family = AF_INET;
				server.sin_port = htons(port);

				// construct robots request with HEAD
				char *robotsRequest = new char[urlSize + 90];
				sprintf(robotsRequest, "HEAD /robots.txt HTTP/1.0\r\n"
					"User-agent: ylTAMUcrawler/1.1\r\n"
					"Host: %s\r\n"
					"Connection: close\r\n\r\n",
					host);
				
				// connect to robots and download 
				if (connectionTest(server, robotsRequest, ROBOTS, &recvBuf, &isValid) == 0 && isValid)
				{	
					delete recvBuf; // release recvBuf for robots downloading

					// downloading robots successfully and status code for robots valid
					// proceed to load page
					// construct page request with GET
					char *pageRequest = new char[urlSize + 90];
					sprintf(pageRequest, "GET %s HTTP/1.0\r\n"
						"User-agent: ylTAMUcrawler/1.1\r\n"
						"Host: %s\r\n"
						"Connection: close\r\n\r\n",
						request, host);

					// connect to page and download
					if (connectionTest(server, pageRequest, PAGE, &recvBuf, &isValid) == 0 && isValid)
					{
						// proceed to parse html for links
						printf("%7s Parsing page... ", "+");
						tStart = clock();

						// create a new parser object
						HTMLParserBase *parser = new HTMLParserBase;
						char *htmlBegin = strstr(recvBuf, "\r\n\r\n") + 4; // html code buffer
						// create base url
						int baseUrlSize = (int)strlen(host) + 8;
						char *baseUrl = new char[baseUrlSize];
						strcpy(baseUrl, "http://");
						strcat(baseUrl, host);

						int nLinks; // number of links found
						parser->Parse(htmlBegin, (int)strlen(htmlBegin), baseUrl, baseUrlSize - 1, &nLinks);

						tSpent = (int)(1000 * (clock() - tStart) / (double)CLOCKS_PER_SEC);
						// check for errors as indicated by negative values
						if (nLinks < 0)
							nLinks = 0;
						printf("done in %d ms with %d links\n", tSpent, nLinks);
						// free memory
						delete baseUrl, parser;
					}
					delete pageRequest;
				}
				delete robotsRequest, recvBuf;
			}
			else
			{
				// IP duplicate, continue to next url
				printf("failed\n");
			}
		}
		else
		{
			// host seen before, continue to next url
			printf("failed\n");
		}

		delete request;
		start = newlinePos + 2; // move starting point to next line

	}

	delete fileBuf;
	WSACleanup();

	cin >> urlSize; // stop program exiting once execution done, to check results displayed
	return 0;
}