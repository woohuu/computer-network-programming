#pragma once
#include "stdafx.h"

// DNS query type
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
#define MAX_HOST_SIZE 255

class DNS
{
private:
	struct sockaddr_in server;
	char packet[MAX_DNS_LEN];
	int pkt_size;
	char recvBuf[MAX_DNS_LEN];
	
	void makeDNSquestion(char *, char *);
	int parseName(char *, char *, int *);

public:
	SOCKET dns_sock;
	char query[30];		// max size of IP address + suffix "in-addr.arpa"
	// parsed query question string which is a repeat of question in query packet
	// in a reversed IP + suffix format, max size smaller than 30
	char qName[30];
	char hostName[MAX_HOST_SIZE + 1];

	DNS();
	~DNS();
	int initialize(char *);
	void createDnsPTrQuery(char *);
	int send();
	int parseReply();

};

