#include "DNS.h"

DNS::DNS()
{
	dns_sock = INVALID_SOCKET;
	memset(&server, 0, sizeof(server));
	pkt_size = 0;
}


DNS::~DNS()
{
	closesocket(dns_sock);
}

int DNS::initialize(char *sIP)
{
	// create a UDP socket
	dns_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (dns_sock == INVALID_SOCKET)
	{
		printf("Unable to create UDP socket - error %d\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.S_un.S_addr = htonl(INADDR_ANY);  // bind to all locl interfaces
	local.sin_port = htons(0);  // let OS select next available port

	// bind the socket
	if (bind(dns_sock, (struct sockaddr *)(&local), sizeof(local)) == SOCKET_ERROR)
	{
		printf("bind() failed with %d\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	// struct for DNS server info
	server.sin_family = AF_INET;
	server.sin_addr.S_un.S_addr = inet_addr(sIP);
	server.sin_port = htons(53);

}

void DNS::createDnsPTrQuery(char *ip)
{
	memset(query, 0, 30);	// reset query
	// create a copy of ip string to work on
	char *ipCopy = new char[strlen(ip) + 1];
	strcpy(ipCopy, ip);
	// convert IP into desired format
	// reverse IP and append it with ".in-addr.arpa"
	char *dotPos = NULL, *endPos = ipCopy + strlen(ipCopy);
	int cur = 0;
	// search for '.' from end of string
	while ((dotPos = strrchr(ipCopy, '.')) != NULL)
	{
		memcpy(query + cur, dotPos + 1, endPos - dotPos - 1);
		cur += endPos - dotPos - 1;
		query[cur++] = '.';
		endPos = dotPos;
		*endPos = '\0';
	}
	query[cur] = '\0'; // null terminate string
	strcat(query, ipCopy); // append the beginning word left in ip string
	strcat(query, ".in-addr.arpa");

	delete[] ipCopy;
}

void DNS::makeDNSquestion(char *question, char *host)
{
	char *word = host, *dotPos = NULL;
	int i = 0;
	while ((dotPos = strchr(word, '.')) != NULL)
	{
		question[i++] = dotPos - word; // copy string size to length octet in question buf first
		memcpy(question + i, word, dotPos - word); // copy string to question buf
		i += dotPos - word;
		word = dotPos + 1;  // move to next word
	}

	// don't forget to copy last word (no dot at end of host string)
	int lastWordLen = strlen(host) - (word - host);
	question[i++] = lastWordLen;
	memcpy(question + i, word, lastWordLen);
	i += lastWordLen;
	// terminate question with 0
	question[i] = 0;
}

int DNS::send()
{
	// create a DNS query packet
	pkt_size = strlen(query) + 2 + sizeof(FixedDNSheader) + sizeof(QueryHeader);
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
	qh->qType = htons(DNS_PTR); // query type
	qh->qClass = htons(DNS_INET);  // query class

	// create question for query packet
	makeDNSquestion((char *)(dh + 1), query);

	// send packet
	if (sendto(dns_sock, packet, pkt_size, 0, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
	{
		printf("sendto() failed with %d\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}
}

int DNS::parseName(char *buf, char *name, int *jump)
{
	int curPos = 0, i = 0;
	unsigned char len;  // length of a following label or it could be the first byte of a pointer for compression

	// question or response name is represented as a sequence of labels terminated with zero octet
	// each label has a label length octet preceeding it
	// loop until the ending zero octet reached
	while ((len = buf[curPos]) != 0)
	{
		// check if curPos is a pointer which means there is compression
		if (len >= 0xC0)
		{
			// offset specified by two-byte pointer is made up of the last 14 bits
			*jump = ((len & 0x3F) << 8) + (unsigned char)buf[++curPos];
			break;
		}
		else
		{
			memcpy(name + i, buf + curPos + 1, len);  // copy one label
			i += len;
			name[i++] = '.';  // separate labels with dot
			curPos += len + 1;
		}

	}
	name[i] = '\0';  // terminate the extracted name string, notice there is a trailing dot

	return ++curPos;  // move off location to next byte after terminating zero octet or pointer
}

int DNS::parseReply()
{
	struct sockaddr_in response;
	int resSize = sizeof(response);

	int recvSize = -1;
	memset(recvBuf, 0, MAX_DNS_LEN);
	if ((recvSize = recvfrom(dns_sock, recvBuf, MAX_DNS_LEN, 0, (struct sockaddr *)&response, &resSize)) > 0)
	{
		// parse DNS reply packet
		FixedDNSheader *rfh = (FixedDNSheader *)recvBuf;
		int nQuestion = ntohs(rfh->questions);

		// parse question section
		int off = sizeof(FixedDNSheader);  // current parsing location from beginning of recvBuf
		int jumpPos = -1;
		memset(qName, 0, 30);	// reset question name
		// use for loop to parse mutlple questions if they exist which is a rare situation though
		for (int i = 0; i < nQuestion; i++)
		{
			// parse question name and update off location
			off += parseName(recvBuf + off, qName, &jumpPos);
			QueryHeader *rqh = (QueryHeader *)(recvBuf + off);
			// retrieve domain name pointed by jumpPos until there is no pointer met
			while (jumpPos != -1)
			{
				int pos = jumpPos;
				jumpPos = -1;  // reset to -1 every time before calling parseName
				// append name retrieved by pointer to old name string with a dot in between
				parseName(recvBuf + pos, qName + strlen(qName), &jumpPos);
			}
			qName[strlen(qName) - 1] = '\0';  // remove the trailing dot
			// update location after parsing type and class
			off += sizeof(QueryHeader);
		}

		// parse first Resource Record (RR) of answer section
		// parse <domain-name> first
		char *domainName = new char[MAX_HOST_SIZE + 1];
		off += parseName(recvBuf + off, domainName, &jumpPos);
		while (jumpPos != -1)
		{
			int pos = jumpPos;
			jumpPos = -1;  // reset to -1 every time before calling parseName
			// append name retrieved by pointer to old name string with a dot in between
			parseName(recvBuf + pos, domainName + strlen(domainName), &jumpPos);
		}
		if (strlen(domainName) > 0)
		{
			domainName[strlen(domainName) - 1] = '\0';  // remove the trailing dot
		}

		// get the fixed-sized header in RR
		DNSanswerHeader *rah = (DNSanswerHeader *)(recvBuf + off);
		// update current parsing location
		off += sizeof(DNSanswerHeader);

		memset(hostName, 0, MAX_HOST_SIZE + 1);	// reset hostName string
		// parse Rdata part in RR
		int rType = ntohs(rah->type);
		int rLen = ntohs(rah->len);
		if (rLen > 0 && rType == DNS_PTR)
		{
			// for DNS PTR query, Rdata is a <domain-name> which is the hostname that we want
			// call parseName whenever a <domain-name> comes up
			off += parseName(recvBuf + off, hostName, &jumpPos);
			while (jumpPos != -1)
			{
				int pos = jumpPos;
				jumpPos = -1;  // reset to -1 every time before calling parseName
				// append name retrieved by pointer to old name string with a dot in between
				parseName(recvBuf + pos, hostName + strlen(hostName), &jumpPos);
			}
			hostName[strlen(hostName) - 1] = '\0';  // remove the trailing dot
		}
		else
		{
			// no DNS entry for the IP queried
			strcpy(hostName, "<no DNS entry>");
		}

		delete[] domainName;
	}
	else if (recvSize == SOCKET_ERROR)
	{
		printf("recvfrom() error %d\n", WSAGetLastError());
		return -1;
	}

	return 0;
}
