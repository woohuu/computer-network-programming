#pragma once
#include <Windows.h>

// ICMP protocol number
#define ICMP			1

#define IP_HDR_SIZE		20
#define ICMP_HDR_SIZE	8
// max payload size of an ICMP message originated in the program
#define MAX_SIZE 65200
// max size of an IP datagram
#define MAX_ICMP_SIZE	(MAX_SIZE + ICMP_HDR_SIZE)
// max ICMP rely message size
#define MAX_REPLY_SIZE	(IP_HDR_SIZE + ICMP_HDR_SIZE + MAX_ICMP_SIZE)

// ICMP packet types
#define ICMP_ECHO_REPLY		0
#define ICMP_DEST_UNREACH	3
#define ICMP_TTL_EXPIRED	11
#define ICMP_ECHO_REQUEST	8

#pragma pack(push, 1)
// define IP header (20 bytes)
class IPHeader {
public:
	u_char h_len : 4;	// lower 4 bits: lenth of the header in dwords
	u_char version : 4;	// upper 4 bits: version of IP, i.e., 4
	u_char tos;			// type of servie (TOS), ignore
	u_short len;		// length of packet
	u_short ident;		// unique identifier
	u_short flags;		// flags together with fragment offset - 16 bits
	u_char ttl;			// time to live
	u_char proto;		// protocol number (6=TCP, 17=UDP, etc.)
	u_short checksum;	// IP header checksum
	u_long source_ip;
	u_long dest_ip;
};

// define ICMP header (8 bytes)
class ICMPHeader {
public:
	u_char type;		// ICMP packet type
	u_char code;		// type subcode
	u_short checksum;	// checksum of the ICMP
	u_short id;			// application-specific ID
	u_short seq;		// application-specific sequence
};

#pragma pack(pop)