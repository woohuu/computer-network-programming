#pragma once
#include <WS2tcpip.h>
#include <IPHlpApi.h>
#include "stdafx.h"
#include "DNS.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "IPHLPAPI.lib")

#define MAX_HOP_COUNT 30
#define MAX_PROBE_COUNT 3
#define MAX_TIMEOUT 5	// timeout all pending DNS requests after 5s

u_short ip_checksum(u_short *, int);

using namespace std;

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("Usage: %s HOSTNAME/IP\n", argv[0]);
		printf("HOSTNAME/IP -- hostname or IP address of destination\n");
		printf("Must have 1 argument\n");
		return -1;
	}

	char *host = argv[1];

	// initialize WinSock, once per program
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup error %d\n", WSAGetLastError());
		return -1;
	}

	// creates an ICMP socket
	SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sock == INVALID_SOCKET)
	{
		printf("Unable to create a raw socket: error %d\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	// destination information
	struct sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;

	// get destination host IP
	// struct for DNS lookup
	struct hostent *remote;

	// assume host string is a dot-formatted IP address
	DWORD IP = inet_addr(host);
	if (IP == INADDR_NONE)
	{
		// host string is not an IP address
		// remove http:// if exists
		if (strstr(host, "http://") != NULL)
		{
			host = host + 7;
		}
		if ((remote = gethostbyname(host)) == NULL)
		{
			printf("ERROR - Invalid hostname %d\n", WSAGetLastError());
			closesocket(sock);
			WSACleanup();
			int input = 0;
			std::cin >> input;
			return -1;
		}
		else
		{
			memcpy(&(dest.sin_addr), remote->h_addr, remote->h_length);
		}
	}
	else
	{
		// a valid IP, store it into server
		dest.sin_addr.S_un.S_addr = IP;
	}
	printf("Tracerouting to %s...\n", inet_ntoa(dest.sin_addr));

	

	// get DNS server IP in the local network
	FIXED_INFO *pFixedInfo;
	ULONG ulFiBufSize;
	pFixedInfo = (FIXED_INFO *)malloc(sizeof(FIXED_INFO));
	if (pFixedInfo == NULL)
	{
		printf("Error allocating memory needed to call GetNetworkParams\n");
		WSACleanup();
		return -1;
	}
	ulFiBufSize = sizeof(FIXED_INFO);
	// make an initial call to GetNetworkParams to get necessary size into ulFiBufSize
	if (GetNetworkParams(pFixedInfo, &ulFiBufSize) == ERROR_BUFFER_OVERFLOW)
	{
		free(pFixedInfo);
		pFixedInfo = (FIXED_INFO *)malloc(ulFiBufSize);
		if (pFixedInfo == NULL)
		{
			printf("Error allocating memory needed to call GetNetworkParams\n");
			WSACleanup();
			return -1;
		}
	}
	char dnsServerIP[16];	// DNS server IP address string for the local network
	// get network params
	if (GetNetworkParams(pFixedInfo, &ulFiBufSize) == NO_ERROR)
	{
		strcpy(dnsServerIP, pFixedInfo->DnsServerList.IpAddress.String);
	}
	else
	{
		printf("GetNetworkParams failed with error: %d\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	DNS dnsObj;						// create an instance of DNS object
	dnsObj.initialize(dnsServerIP);	// use local network DNS server

	// handle ICMP and DNS response packets
	HANDLE icmpSockRcvReady = CreateEvent(NULL, false, false, NULL);	// set manual-reset to be false
	HANDLE dnsSockRcvReady = CreateEvent(NULL, false, false, NULL);
	HANDLE events[] = { icmpSockRcvReady, dnsSockRcvReady };

	if (WSAEventSelect(sock, icmpSockRcvReady, FD_READ) == SOCKET_ERROR)
	{
		printf("WSAEventSelect() failed with %d\n", WSAGetLastError());
		return -1;
	}

	if (WSAEventSelect(dnsObj.dns_sock, dnsSockRcvReady, FD_READ) == SOCKET_ERROR)
	{
		printf("WSAEventSelect() failed with %d\n", WSAGetLastError());
		return -1;
	}

	// stats variables and initialization
	char routerName[MAX_HOP_COUNT][MAX_HOST_SIZE + 1];
	for (int j = 0; j < MAX_HOP_COUNT; j++)
	{
		strcpy(routerName[j], "*");		// initialize to "*"
	}
	char routerIP[MAX_HOP_COUNT][16];	// max number of characters in an IP address string is 15
	double rtt[MAX_HOP_COUNT] = { 0 };	// rtt for each ICMP packet
	unordered_map<string, int> seqMap;	// map reversed-IP query to packet seq number
	int destSeq = -1;					// destination seq number

	// variables for setting timeout for each hop
	int timeout[MAX_HOP_COUNT];	// in millisecond
	// set initial RTO for every hop to be 500 ms
	for (int j = 0; j < MAX_HOP_COUNT; j++)
	{
		timeout[j] = 500;
	}
	DWORD rto;	// time out for waitformultipleobjects
	LARGE_INTEGER selectNow;
	LONGLONG timerExpire;

	// variables for parsing ICMP reply packets
	char recv_buf[MAX_REPLY_SIZE];	// receive buffer
	struct sockaddr_in response;	// struct for recvfrom()
	int resSize = sizeof(response);
	// header pointers to parse receive buffer
	IPHeader *router_ip_hdr = (IPHeader *)recv_buf;
	ICMPHeader *router_icmp_hdr;
	IPHeader *orig_ip_hdr;
	ICMPHeader *orig_icmp_hdr;

	int dnsSentCount = 0, dnsRcvCount = 0;	// dns packet sent and received count
	int maxSeqSeen = 1, maxDnsSeqSeen = 1;	// max seq seen in ICMP and DNS reply packets, respectively
	bool dnsRcvDone = false;				// a flag to indicate whether all DNS queries have been replied
	bool allPktsProcessed = false;			// a flag to indicate whether all packets have either replied or rtxed and timedout
	double totalTime = 0;					// total execution time

	unordered_set<int> seqSeen;				// a set for all seq numbers that have either been replied or rtxed for 3 times
	int base = 1, prevLargest, nextSmallest;	// the next two closest seen neighbors for an unseen hop

	
	// QPC variables to get time for an operation
	LARGE_INTEGER startTime, endTime, frequency;

	u_char send_buf[MAX_ICMP_SIZE];

	memset(send_buf, 0, MAX_ICMP_SIZE);
	ICMPHeader *icmp = (ICMPHeader *)send_buf;
	// set up the echo request
	icmp->type = ICMP_ECHO_REQUEST;
	icmp->code = 0;
	icmp->id = (u_short)GetCurrentProcessId();	// set up ID field using process ID

	int sentCount[MAX_HOP_COUNT] = { 0 };	// count of how many times a probe packet has been sent

	int i = 1;		// probe packet number
	// send all probe packets at once
	while (i <= MAX_HOP_COUNT)
	{
		icmp->seq = i;	// sequence field of ICMP packet
						// initialize checksum to zero
		icmp->checksum = 0;
		int packet_size = sizeof(ICMPHeader);
		// calculate checksum for the entire packet
		icmp->checksum = ip_checksum((u_short *)send_buf, packet_size);

		// set proper TTL
		int ttl = i;	// each probe i has the TTL value set to i
		if (setsockopt(sock, IPPROTO_IP, IP_TTL, (const char *)&ttl, sizeof(ttl)) == SOCKET_ERROR)
		{
			printf("setsockopt() failed with %d\n", WSAGetLastError());
			WSACleanup();
			return -1;
		}

		// send ICMP packet
		if (sendto(sock, (char *)send_buf, packet_size, 0, (struct sockaddr *)&dest, sizeof(dest)) == SOCKET_ERROR)
		{
			printf("sendto() failed with %d\n", WSAGetLastError());
			WSACleanup();
			return -1;
		}
		sentCount[i - 1]++;

		i++;
	}
	QueryPerformanceFrequency(&frequency);	// record frequency
	QueryPerformanceCounter(&startTime);	// record start time

	LONGLONG sentTime[MAX_HOP_COUNT];	// record packet sent time for each packet
	for (int j = 0; j < MAX_HOP_COUNT; j++)
	{
		sentTime[j] = startTime.QuadPart;
	}

	// a minheap to store all timers
	priority_queue<LONGLONG, vector<LONGLONG>, greater<LONGLONG>> minHeap;
	// a map to retrieve pkt seq # based on timerExpire
	unordered_map<LONGLONG, int> timerMap;

	LONGLONG initialTimerExpire = (LONGLONG)(timeout[0] * frequency.QuadPart / 1000 + startTime.QuadPart);
	minHeap.push(initialTimerExpire);
	timerMap[initialTimerExpire] = 1;

	// push MAX_TIMEOUT for DNS which is 5 seconds onto minHeap
	LONGLONG maxTimerExpire = (LONGLONG)(MAX_TIMEOUT * frequency.QuadPart + startTime.QuadPart);
	minHeap.push(maxTimerExpire);

	int timeoutPktCount = 0;		// count of packets that time out after 3 times of rtx

	while (true)
	{
		QueryPerformanceCounter(&selectNow);
		if (selectNow.QuadPart < minHeap.top())
		{
			rto = (DWORD)((minHeap.top() - selectNow.QuadPart) * 1000.0 / frequency.QuadPart);
		}
		else if (selectNow.QuadPart >= maxTimerExpire)
		{
			totalTime = (double)(selectNow.QuadPart - startTime.QuadPart) / frequency.QuadPart * 1000.0;
			break;	// break while loop after 5s DNS max timeout
		}
		else
		{
			rto = 0;
		}

		int ret = WaitForMultipleObjects(2, events, false, rto);
		switch (ret)
		{
		case WAIT_TIMEOUT:
		{
			// find the packet seq number that needs to be retransmitted
			int toSeq = 0;
			if (timerMap.find(minHeap.top()) != timerMap.end())
			{

				toSeq = timerMap[minHeap.top()];
				if (seqSeen.count(toSeq))
				{
					// for timer whose packet has seen reply
					// let the timer expire and do nothing, pop the timer value and continue to next loop
					minHeap.pop();
					break;
				}
				if (sentCount[toSeq - 1] < MAX_PROBE_COUNT)
				{
					// retransmit packet of # toSeq
					icmp->seq = toSeq;	// sequence field of ICMP packet
										// initialize checksum to zero
					icmp->checksum = 0;
					int packet_size = sizeof(ICMPHeader);
					// calculate checksum for the entire packet
					icmp->checksum = ip_checksum((u_short *)send_buf, packet_size);

					// set proper TTL
					int ttl = toSeq;	// each probe i has the TTL value set to i
					if (setsockopt(sock, IPPROTO_IP, IP_TTL, (const char *)&ttl, sizeof(ttl)) == SOCKET_ERROR)
					{
						printf("setsockopt() failed with %d\n", WSAGetLastError());
						WSACleanup();
						return -1;
					}

					// send ICMP packet
					if (sendto(sock, (char *)send_buf, packet_size, 0, (struct sockaddr *)&dest, sizeof(dest)) == SOCKET_ERROR)
					{
						printf("sendto() failed with %d\n", WSAGetLastError());
						WSACleanup();
						return -1;
					}
					sentCount[toSeq - 1]++;
					// update minHeap with new timer
					minHeap.pop();
					QueryPerformanceCounter(&selectNow);
					sentTime[toSeq - 1] = selectNow.QuadPart;	// update sent time due to rtx
					timerExpire = (LONGLONG)(timeout[toSeq - 1] * frequency.QuadPart / 1000 + selectNow.QuadPart);	// calculate new timer
					minHeap.push(timerExpire);
					timerMap[timerExpire] = toSeq;	// map timerExpire to hop/seq #
				}
				else
				{
					timeoutPktCount++;
					minHeap.pop();
					seqSeen.insert(toSeq);	// mark toSeq as seen after 3 times of timeout
				}
			}
			break;
		}
		case WAIT_OBJECT_0:
		{
			// ICMP socket data ready to receive
			int recvSize = -1;
			if ((recvSize = recvfrom(sock, recv_buf, MAX_REPLY_SIZE, 0, (struct sockaddr *)&response, &resSize)) > 0)
			{
				if (recvSize >= 56)		// 56 is normal size of a ICMP packet with four headers, ready to handle a larger IP header
				{
					// parse receive buffer
					// handle variable IP header length
					int iphdr_size = router_ip_hdr->h_len * sizeof(DWORD);
					router_icmp_hdr = (ICMPHeader *)(recv_buf + iphdr_size);
					orig_ip_hdr = (IPHeader *)(router_icmp_hdr + 1);
					int orig_iphdr_size = orig_ip_hdr->h_len * sizeof(DWORD);
					orig_icmp_hdr = (ICMPHeader *)((char *)orig_ip_hdr + orig_iphdr_size);
					
					if (router_icmp_hdr->type == ICMP_TTL_EXPIRED && router_icmp_hdr->code == 0)
					{
						if (orig_ip_hdr->proto == ICMP)
						{
							if (orig_icmp_hdr->id == GetCurrentProcessId())
							{
								// reply packet has ID of probe packet, proceed to further parsing
								// update stats
								int seq = orig_icmp_hdr->seq;
								// calculate rtt
								QueryPerformanceCounter(&endTime);
								rtt[seq - 1] = (double)(endTime.QuadPart - sentTime[seq - 1]) * 1000.0 / frequency.QuadPart;
								// record router IP
								u_long ip = router_ip_hdr->source_ip;
								struct sockaddr_in temp;	// a temp sockaddr_in variable to convert u_long IP into string format
								memcpy(&temp.sin_addr.s_addr, &ip, sizeof(ip));
								char *ipStr = inet_ntoa(temp.sin_addr);
								strcpy(routerIP[seq - 1], ipStr);	// save IP string of router

								// DNS lookup for router hostname by IP address
								// create DNS query
								dnsObj.createDnsPTrQuery(ipStr);
								// use reversed IP string as key and seq as value to create an entry in map
								// for later DNS reply packet parse, use reversed IP string to retrieve seq number
								seqMap.insert({ string(dnsObj.query), seq });
								// send dns query packet to server
								dnsObj.send();
								dnsSentCount++;

								// algorithm to set timeout per-hop
								seqSeen.insert(seq);	// insert seq into set
								maxSeqSeen = max(maxSeqSeen, seq);
								int unseenCount = 0;
								for (int j = base; j <= maxSeqSeen; j++)
								{
									if (!seqSeen.count(j))
									{
										// packet j not seen before
										unseenCount++;
										prevLargest = j - unseenCount;
										for (int k = j + 1; k <= maxSeqSeen; k++)
										{
											if (seqSeen.count(k))
											{
												nextSmallest = k;
												break;
											}
										}
										// update the timeout for hop j which did not see a TTL expired reply
										// assuming a linear increase of rtt per hop, set timeout to be 2 times of estimated rtt
										timeout[j - 1] = 2 * (rtt[prevLargest - 1] + (rtt[nextSmallest - 1] - rtt[prevLargest - 1]) / (nextSmallest - prevLargest) * (j - prevLargest));
										timerExpire = (LONGLONG)(timeout[j - 1] * frequency.QuadPart / 1000 + startTime.QuadPart);
										minHeap.push(timerExpire);
										timerMap[timerExpire] = j;	// map timerExpire to hop/seq j
									}
								}
								base = maxSeqSeen;	// update processing base
							}
						}
					}
					else
					{
						// handle ICMP error type and code
						if (orig_ip_hdr->proto == ICMP)
						{
							if (orig_icmp_hdr->id == GetCurrentProcessId())
							{
								int seq = orig_icmp_hdr->seq;
								sprintf(routerName[seq - 1], "other error: code %d type %d", router_icmp_hdr->code, router_icmp_hdr->type);
							}
						}
					}
				}
				else if (recvSize == 28)		// 28 is size of echo reply ICMP pkt with only two headers
				{
					// destination echo reply, only IP and ICMP header from router
					router_icmp_hdr = (ICMPHeader *)(router_ip_hdr + 1);
					if (router_icmp_hdr->type == ICMP_ECHO_REPLY && router_icmp_hdr->code == 0)
					{
						if (router_icmp_hdr->id == GetCurrentProcessId())
						{
							int seq = router_icmp_hdr->seq;
							if (destSeq == -1)
							{
								destSeq = seq;	// update destination the first time an echo reply pkt received
							}
							// only handle echo packet with seq number no larger than current destSeq, larger seq ignored
							if (seq <= destSeq)
							{
								// handle echo reply packets reordering
								if (seq < destSeq)
								{
									// update destSeq when a smaller echo reply seq found
									// eventualy destSeq will be the smallest hop count that sends echo reply packet
									destSeq = seq;	
								}
								QueryPerformanceCounter(&endTime);
								rtt[seq - 1] = (double)(endTime.QuadPart - sentTime[seq - 1]) / frequency.QuadPart * 1000.0;
								u_long ip = router_ip_hdr->source_ip;
								struct sockaddr_in temp;	// a temp sockaddr_in variable to convert u_long IP into string format
								memcpy(&temp.sin_addr.s_addr, &ip, sizeof(ip));
								char *ipStr = inet_ntoa(temp.sin_addr);
								strcpy(routerIP[seq - 1], ipStr);	// save IP string of router

								// DNS lookup for router hostname by IP address
								// create DNS query
								dnsObj.createDnsPTrQuery(ipStr);
								// use reversed IP string as key and seq as value to create an entry in map
								// for later DNS reply packet parse, use reversed IP string to retrieve seq number
								seqMap.insert({ string(dnsObj.query), seq });
								// send dns query packet to server
								dnsObj.send();
								dnsSentCount++;

								// algorithm to set timeout per-hop
								seqSeen.insert(seq);	// insert seq into set
								maxSeqSeen = max(maxSeqSeen, seq);
								int unseenCount = 0;
								for (int j = base; j <= maxSeqSeen; j++)
								{
									if (!seqSeen.count(j))
									{
										// packet j not seen before
										unseenCount++;
										prevLargest = j - unseenCount;
										for (int k = j + 1; k <= maxSeqSeen; k++)
										{
											if (seqSeen.count(k))
											{
												nextSmallest = k;
												break;
											}
										}
										// update the timeout for hop j which did not see a TTL expired reply
										// assuming a linear increase of rtt per hop, set timeout to be 2 times of estimated rtt
										timeout[j - 1] = 2 * (rtt[prevLargest - 1] + (rtt[nextSmallest - 1] - rtt[prevLargest - 1]) / (nextSmallest - prevLargest) * (j - prevLargest));
										timerExpire = (LONGLONG)(timeout[j - 1] * frequency.QuadPart / 1000 + startTime.QuadPart);
										minHeap.push(timerExpire);
										timerMap[timerExpire] = j;	// map timerExpire to hop/seq j
									}
								}
								base = maxSeqSeen;	// update processing base
							}
						}
					}
					else
					{
						// handle ICMP error type and code
						if (orig_ip_hdr->proto == ICMP)
						{
							if (orig_icmp_hdr->id == GetCurrentProcessId())
							{
								int seq = orig_icmp_hdr->seq;
								sprintf(routerName[seq - 1], "other error: code %d type %d", router_icmp_hdr->code, router_icmp_hdr->type);
							}
						}
					}
				}
				break;
			}
			else if (recvSize == SOCKET_ERROR)
			{
				printf("recvfrom failed with %d\n", WSAGetLastError());
				break;
			}
		}
		case WAIT_OBJECT_0 + 1:
		{
			// DNS socket data ready to receive
			dnsObj.parseReply();
			// retrieve seq number for the packet whose IP got a DNS reply
			// use the question name parsed from dns reply packet as key to get value in the map
			int replySeq = seqMap[string(dnsObj.qName)];
			// store the hostname parsed from DNS reply packet
			strcpy(routerName[replySeq - 1], dnsObj.hostName);
			dnsRcvCount++;
			maxDnsSeqSeen = max(maxDnsSeqSeen, replySeq);
			break;
		}
		default:
			// wait failed
			// error handling
			printf("WaitForMultipleObjects() failed with error %d\n", WSAGetLastError());
		}

		if (destSeq != -1 && seqSeen.size() == maxSeqSeen)
		{
			allPktsProcessed = true;
		}
		if (destSeq != -1 && maxDnsSeqSeen >= destSeq && dnsRcvCount >= maxSeqSeen - timeoutPktCount)
		{
			dnsRcvDone = true;
		}
		if (dnsRcvDone && allPktsProcessed)
		{
			// once all DNS queries have been replied
			// and all packets has been replied or rtxed, break while loop for an early exit
			// otherwise will have to wait till 5s max timeout
			QueryPerformanceCounter(&endTime);
			totalTime = (double)(endTime.QuadPart - startTime.QuadPart) / frequency.QuadPart * 1000.0;
			break;
		}
	}

	for (int j = 0; j < destSeq; j++)
	{
		if (strstr(routerName[j], "*") != NULL)
		{
			printf("%2d  *\n", j + 1);
		}
		else if (strstr(routerName[j], "other error:") != NULL)
		{
			printf("%2d  %s\n", j + 1, routerName[j]);
		}
		else
		{
			printf("%2d  %s (%s) %.3f ms (%d)\n", j + 1, routerName[j], routerIP[j], rtt[j], sentCount[j]);
		}
	}

	printf("\nTotal execution time: %d ms\n", (int)totalTime);
	
	if (pFixedInfo)
	{
		free(pFixedInfo);
	}

	// cleaning
	CloseHandle(events[0]);
	CloseHandle(events[1]);
	closesocket(sock);
	WSACleanup();

	int freeze;
	std::cin >> freeze;	// freeze output window

	return 0;

}