#include <Windows.h>

// set struct padding/alignment to 1 byte
#pragma pack(push, 1)
class QueryHeader {
public:
	USHORT qType;
	USHORT qClass;
};

class FixedDNSheader {
public:
	USHORT ID;
	USHORT flags;
	USHORT questions;
	USHORT answers;
	USHORT authority;
	USHORT additional;
};

class DNSanswerHeader {
public:
	USHORT type;
	USHORT aClass;
	UINT ttl;
	USHORT len;
};

#pragma pack(pop) // restore old packing