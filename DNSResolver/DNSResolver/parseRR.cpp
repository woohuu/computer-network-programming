#pragma once
#include "stdafx.h"

int parseRR(char *buf, int curPos, int n, char *banner, int pktSize)
{
	printf("%s\n", banner);

	char name[MAX_NAME_SIZE + 1];  // store parsed <domain-name>
	int jumpPos = -1;
	int i = 0; // loop count

	for (; i < n; i++)
	{
		// parse <domain-name> first
		curPos += parseName(buf + curPos, name, &jumpPos);
		// check if curPos is beyond the packet end
		if (curPos > pktSize)
		{
			if ((unsigned char)buf[pktSize - 1] >= 0xC0)
			{
				// last byte is a leading byte for a pointer
				printf("  ++ invalid record: truncated jump offset\n");
			}
			else
			{
				printf("  ++ invalid record: truncated name\n");
			}
			return -1;
		}
		// handle compression, retrieve domain name pointed by jumpPos until there is no pointer met
		int jumpCount = 0;
		while (jumpPos != -1)
		{
			// validate jumpPos
			if (jumpPos < sizeof(FixedDNSheader))
			{
				printf("  ++ invalid record: jump into fixed header\n");
				return -1;
			}
			if (jumpPos >= pktSize)
			{
				printf("  ++ invalid record: jump beyond packet boundary\n");
				return -1;
			}
			int pos = jumpPos;
			jumpPos = -1;  // reset to -1 every time before calling parseName
			// call parseName to retrieve name pointed by jumpPos
			// append name retrieved to old name string with a dot in between
			parseName(buf + pos, name + strlen(name), &jumpPos);
			jumpCount++;
			if (jumpCount >= 10) {
				printf("  ++ invalid record: jump loop\n");
				return -1;
			}
		}
		name[strlen(name) - 1] = '\0';  // remove the trailing dot

		if (curPos + sizeof(DNSanswerHeader) > pktSize)
		{
			printf("  ++ invalid record: truncated fixed RR header\n");
			return -1;
		}
		// get the fixed-sized header in RR
		DNSanswerHeader *rah = (DNSanswerHeader *)(buf + curPos);
		// update curPos location
		curPos += sizeof(DNSanswerHeader);

		char rrType[6]; // store RR type string
		char *rData = NULL;
		switch (ntohs(rah->type))
		{
		case 1:
			strcpy(rrType, "A");
			break;
		case 2:
			strcpy(rrType, "NS");
			break;
		case 5: strcpy(rrType, "CNAME");
			break;
		case 12: strcpy(rrType, "PTR");
			break;
		default:
		{
			rrType[0] = '\0';
			curPos += ntohs(rah->len);
			if (curPos > pktSize)
			{
				printf("  ++ invalid record: RR value length beyond packet\n");
				return -1;
			}
		}
		}

		// parse the ending RDATA part whose format depends on type of RR
		if (strcmp(rrType, "A") == 0)
		{
			// type A, RDATA is just four bytes of IP address
			DWORD IP = *(LPDWORD)(buf + curPos); // convert four bytes into u_long
			struct sockaddr_in rd;
			memcpy(&rd.sin_addr.s_addr, &IP, sizeof(IP));
			rData = inet_ntoa(rd.sin_addr);
			printf("\t%s %s %s TTL = %d\n", name, rrType, rData, ntohl(rah->ttl));
			// update curPos to next record
			curPos += ntohs(rah->len);
			if (curPos > pktSize)
			{
				printf("  ++ invalid record: RR value length beyond packet\n");
				return -1;
			}
		}
		else if (rrType[0] != 0)
		{
			// type NS & CNAME & PTR, RDATA is a <domain-name>
			// call parseName whenever a <domain-name> comes up
			rData = new char[4 * strlen(name)];
			curPos += parseName(buf + curPos, rData, &jumpPos);
			// check if curPos is beyond the packet end
			if (curPos > pktSize)
			{
				if ((unsigned char)buf[pktSize - 1] >= 0xC0)
				{
					// last byte is a leading byte for a pointer
					printf("  ++ invalid record: truncated jump offset\n");
				}
				else
				{
					printf("  ++ invalid record: RR value length beyond packet\n");
				}
				return -1;
			}
			// handle compression, retrieve domain name pointed by jumpPos until there is no pointer met
			int jumpCount = 0;
			while (jumpPos != -1)
			{
				// validate jumpPos
				if (jumpPos < sizeof(FixedDNSheader))
				{
					printf("  ++ invalid record: jump into fixed header\n");
					return -1;
				}
				if (jumpPos >= pktSize)
				{
					printf(" ++ invalid record: jump beyond packet boundary\n");
					return -1;
				}
				int pos = jumpPos;
				jumpPos = -1;  // reset to -1 every time before calling parseName
				// call parseName to retrieve name pointed by jumpPos
				// append name retrieved to old name string every time
				parseName(buf + pos, rData + strlen(rData), &jumpPos);
				jumpCount++;
				if (jumpCount >= 10) {
					printf("  ++ invalid record: jump loop\n");
					return -1;
				}
			}
			rData[strlen(rData) - 1] = '\0';  // replace the trailing dot
			printf("\t%s %s %s TTL = %d\n", name, rrType, rData, ntohl(rah->ttl));

			delete[] rData;
		}

		if (curPos == pktSize)
		{
			// all records has been read successfully and no more data left in packet
			break;
		}
	}

	if (i < n - 1)
	{
		// number of RRs parsed in the section is not smaller than the count of RR in header field
		printf("  ++ invalid section: not enough records\n");
		return -1;
	}

	return curPos;
}