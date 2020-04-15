/*
* Yi Liu
* UIN: 827008592
* CSCE612 Spring 2019
*/

#pragma once
#include "stdafx.h"

int main(int argc, char *argv[])
{
	// take URL as a single command-line argument
	if (argc != 2)
	{
		printf("Usage: %s a-URL\n", argv[0]);
		printf("Please input only one URL as an argument\n");
		return 1;
	}

	char *host = NULL, *fullRequest = NULL;
	int portNum;

	// preprocess and validate URL, extract host, port and generate http request
	if (urlPreprocessor(argv[1], &host, &portNum, &fullRequest) != 0)
		return 1;

	// connect to a server, send HTTP request and parse response
	winsock_test(host, portNum, fullRequest);
	
	delete fullRequest;

	return 0;

}

