/*
* Yi Liu
* UIN: 827008592
* CSCE612 Spring 2019
*/

#pragma once
#include "stdafx.h"

#pragma comment(lib, "ws2_32.lib")

// DNS query types
#define DNS_A 1
#define DNS_NS 2
#define DNS_CNAME 5
#define DNS_PTR 12

// query classes
#define DNS_INET 1

// flags
#define DNS_QUERY (0 << 15)   // 0 = query
#define DNS_RESPONSE (1 << 15)  // 1 = response
#define DNS_STDQUERY (0 << 11)  // opcode - 4 bits
#define DNS_AA (1 << 10)  // authoritative answer
#define DNS_TC (1 << 9)  // truncated
#define DNS_RD (1 << 8)  // recursion desired
#define DNS_RA (1 << 7) // recursion available

#define MAX_DNS_LEN 512
#define MAX_ATTEMPTS 3

using namespace std;

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		printf("Usage: %s hostname/IP DNS_server_IP\n", argv[0]);
		printf("Must have two arguments\n");
		return 1;
	}

	// initialize WinSock, once per program
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup error %d\n", WSAGetLastError());
		return 1;
	}

	char *host = argv[1];
	char *rHost = new char[strlen(host) + 14];
	char *dns_server_IP = argv[2];
	USHORT dnsType;

	// determine DNS type by check if host is a hostname or IP address
	if (inet_addr(host) == INADDR_NONE) {
		dnsType = DNS_A;
	}
	else {
		dnsType = DNS_PTR;
		// create a copy of host string to work on
		char *hostCopy = new char[strlen(host) + 1];
		strcpy(hostCopy, host);
		// convert IP into desired format
		// reverse IP and append it with ".in-addr.arpa"
		char *dotPos = NULL, *endPos = hostCopy + strlen(hostCopy);
		int cur = 0;
		// search for '.' from end of string
		while ((dotPos = strrchr(hostCopy, '.')) != NULL)
		{
			memcpy(rHost + cur, dotPos + 1, endPos - dotPos - 1);
			cur += endPos - dotPos - 1;
			rHost[cur++] = '.';
			endPos = dotPos;
			*endPos = '\0';
		}
		rHost[cur] = '\0'; // null terminate rHost string
		strcat(rHost, hostCopy); // append the beginning word left in host string
		strcat(rHost, ".in-addr.arpa");

		//update host to point to the newly-formatted rHost
		host = rHost;
		delete[] hostCopy;
	}

	// create a DNS query
	char packet[MAX_DNS_LEN];
	int pkt_size = strlen(host) + 2 + sizeof(FixedDNSheader) + sizeof(QueryHeader);
	// initialize all bytes to be 0
	memset(packet, 0, pkt_size);

	// fixed header fields initialization
	FixedDNSheader *dh = (FixedDNSheader *)packet;
	QueryHeader *qh = (QueryHeader *)(packet + pkt_size - sizeof(QueryHeader));
	// genarate a random unsigned short for transaction ID (TXID)
	srand((unsigned)time(0));
	USHORT txid = rand() % 65536;
	dh->ID = htons(txid);
	dh->flags = htons(DNS_QUERY | DNS_RD | DNS_STDQUERY);
	dh->questions = htons(1); // one question
	qh->qType = htons(dnsType); // query type
	qh->qClass = htons(DNS_INET);  // qurey class
	
	// create question for query packet
	makeDNSquestion((char *)(dh + 1), host);

	printf("%-8s: %s\n", "Lookup", argv[1]);
	printf("%-8s: %s, type %d, TXID 0x%.4X\n", "Query", host, dnsType, txid);
	printf("%-8s: %s\n", "Server", dns_server_IP);
	printf("*********************************\n");

	// create a UDP socket
	SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		printf("socket() generated error %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.S_un.S_addr = htonl(INADDR_ANY);  // bind to all locl interfaces
	local.sin_port = htons(0);  // let OS select next available port

	// bind the socket
	if (bind(sock, (struct sockaddr *)(&local), sizeof(local)) == SOCKET_ERROR)
	{
		printf("bind() failed with %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	// struct for DNS server info
	struct sockaddr_in remote;
	memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;
	remote.sin_addr.S_un.S_addr = inet_addr(dns_server_IP);
	remote.sin_port = htons(53);

	// variable initialization
	int iRet = 0;  // main return value
	int count = 0, iResult;
	fd_set fd;
	struct timeval timeout;
	timeout.tv_sec = 10; // set timeout to be 10 seconds
	timeout.tv_usec = 0;
	char recvBuf[MAX_DNS_LEN]; // receive buf
	struct sockaddr_in response;
	int resSize = sizeof(response);

	// get execution time of an operation
	int tSpent = 0;
	clock_t start = 0;

	while (count < MAX_ATTEMPTS)
	{	
		printf("Attempt %d with %d bytes... ", count, pkt_size);
		start = clock();
		// send query request to DNS server
		if (sendto(sock, packet, pkt_size, 0, (struct sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
		{
			printf("sendto() failed with %d\n", WSAGetLastError());
			WSACleanup();
			return 1;
		}
		
		//get ready to receive
		FD_ZERO(&fd);
		FD_SET(sock, &fd);
		if ((iResult = select(0, &fd, NULL, NULL, &timeout)) > 0)
		{
			// new data available, read it into recvBuf
			int recvSize = -1;
			if ((recvSize = recvfrom(sock, recvBuf, MAX_DNS_LEN, 0, (struct sockaddr *)&response, &resSize)) > 0)
			{
				tSpent = (int)(1000 * (clock() - start) / (double)CLOCKS_PER_SEC);
				printf("response in %d ms with %d bytes\n", tSpent, recvSize);
				
				// check if this packet came from the server to which we sent the query earlier
				if (response.sin_addr.s_addr != remote.sin_addr.s_addr || response.sin_port != remote.sin_port)
				{
					printf("Bogus response, discarded\n");
					iRet = 1;
					break;
				}

				if (recvSize < sizeof(FixedDNSheader))
				{
					printf("  ++ invalid reply: smaller than fixed header\n");
					iRet = 1;
					break;
				}

				// start parsing the response packet
				// parse DNS fixed header
				FixedDNSheader *rfh = (FixedDNSheader *)recvBuf;
				int nQuestion = ntohs(rfh->questions);
				int nAnswer = ntohs(rfh->answers);
				int nAuthority = ntohs(rfh->authority);
				int nAdditional = ntohs(rfh->additional);
				printf("  TXID 0x%.4X flags 0x%.4X questions %d answers %d authority %d additional %d\n",
					ntohs(rfh->ID), ntohs(rfh->flags), nQuestion, 
					nAnswer, nAuthority, nAdditional);
				// check if response TXID matches with query TXID
				if (ntohs(rfh->ID) != txid)
				{
					printf("  ++ invalid reply: TXID mismatch, sent 0x%.4X, received 0x%.4X\n", txid, ntohs(rfh->ID));
					iRet = 1;
					break;
				}

				// get the Rcode -- last 4 bits of flag
				unsigned char Rcode = (ntohs(rfh->flags) & 0x0f);
				if (Rcode == 0)
					printf("  succeeded with Rcode = %d\n", Rcode);
				else
				{
					printf("  failed with Rcode = %d\n", Rcode);
					iRet = 1;
					break;
				}

				// parse and display questions section
				printf("  ------------ [questions] ------------\n");
				int off = sizeof(FixedDNSheader);  // current parsing location from beginning of recvBuf
				int jumpPos = -1;
				char *name = new char[4 * strlen(host)]; // store <domain-name>
				// use for loop to parse mutlple questions if they exist which is a rare situation though
				for (int i = 0; i < nQuestion; i++)
				{	
					// parse question name and update off location
					off += parseName(recvBuf + off, name, &jumpPos);
					QueryHeader *rqh = (QueryHeader *)(recvBuf + off);
					// retrieve domain name pointed by jumpPos until there is no pointer met
					int jumpCount = 0;
					while (jumpPos != -1)
					{
						// validate jumpPos
						if (jumpPos < sizeof(FixedDNSheader))
						{
							printf("  ++ invalid record: jump into fixed header\n");
							iRet = 1;
							break;
						}
						if (jumpPos >= recvSize)
						{
							printf(" ++ invalid record: jump beyond packet boundary\n");
							iRet = 1;
							break;
						}
						int pos = jumpPos;
						jumpPos = -1;  // reset to -1 every time before calling parseName
						// append name retrieved by pointer to old name string with a dot in between
						parseName(recvBuf + pos, name + strlen(name), &jumpPos);
						jumpCount++;
						if (jumpCount >= 10) 
						{
							printf("  ++ invalid record: jump loop\n");
							iRet = 1;
							break;
						}
					}
					name[strlen(name) - 1] = '\0';  // remove the trailing dot
					printf("\t%s type %d class %d\n", name, ntohs(rqh->qType), ntohs(rqh->qClass));
					// update location after parsing type and class
					off += sizeof(QueryHeader);
				}

				// parse RRs in answers, authority and additional sections
				char ansBanner[] = "  ------------ [answers] --------------";
				if ((off = parseRR(recvBuf, off, nAnswer, ansBanner, recvSize)) < 0)
				{
					iRet = 1;
					break;
				}

				if (nAuthority > 0)
				{
					char authBanner[] = "  ------------ [authority] ------------";
					if ((off = parseRR(recvBuf, off, nAuthority, authBanner, recvSize)) < 0)
					{
						iRet = 1;
						break;
					}
				}
				if (nAdditional > 0)
				{
					char addBanner[] = "  ------------ [additional] -----------";
					if ((off = parseRR(recvBuf, off, nAdditional, addBanner, recvSize)) < 0)
					{
						iRet = 1;
						break;
					}
				}
				
				delete[] name;
				break;
			}
			else if (recvSize == SOCKET_ERROR)
			{
				printf("socket error %d\n", WSAGetLastError());
				break;
			}

		}
		else if (iResult == 0)
		{
			printf("timeout in %d ms\n", 1000 * timeout.tv_sec);
		}
		else
		{
			printf("select() failed with %d\n", WSAGetLastError());
		}

		count++;
	}


	delete[] rHost;
	cin >> tSpent;

	return iRet;

}
