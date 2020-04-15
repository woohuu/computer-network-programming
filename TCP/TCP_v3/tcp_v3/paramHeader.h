#pragma once
#include "SenderSocket.h"

class PdParams {
public:
	UINT64 bufSize;
	char *data;
	SenderSocket *sndSock;
};

class WkParams {
public:
	Params *params;
	SenderSocket *sndSock;
};