/*
* Yi Liu
* UIN: 827008592
* CSCE612 Spring 2019
*/

#include "stdafx.h"
#include "urlPreprocessor.h"

int urlPreprocessor(char *url, int *portN, char **rqst)
{
	printf("URL: %s\n", url);

	// check if URL begins with "http://"
	if (strstr(url, "http://") != url)
	{
		printf("\tParsing URL... failed with invalid scheme\n");
		return 1;
	}

	// strip the beginning "http://"
	url += 7;
	int urlSize = (int)strlen(url);

	// truncate after # if there is fragment in URL
	char *poundPos = strchr(url, '#');
	if (poundPos != NULL)
		// set '#' as null character to terminate url string
		*poundPos = '\0';

	// extract for query if it exists
	char *query = strchr(url, '?');
	if (query != NULL)
		// set '?' as null character and increment the pointer
		*query++ = '\0';

	// extract path if it exists
	char *path = strchr(url, '/');
	if (path != NULL)
		// set '/' as null character and increment the pointer
		*path++ = '\0';

	// extract port number if it exists
	int portNum = 80; // initialize with default port number for http
	char *port = strchr(url, ':');
	if (port != NULL)
	{
		*port++ = '\0';
		portNum = atoi(port);
		// check if port number is an integer and in valid range
		if (portNum == 0 || portNum > 65535)
		{
			printf("\tParsing URL... failed with invalid port\n");
			return 1;
		}
	}
	*portN = portNum;

	// url becomes host after stripping fragment, query, path and port number
	int hostSize = (int)strlen(url);

	// check if host length is too long
	if (hostSize > MAX_HOST_LEN)
	{
		printf("Host length exceeds %d, the maximum host length allowed\n", MAX_HOST_LEN);
		return 1;
	}

	// construct http request
	char *request = new char[urlSize - hostSize + 2];
	strcpy(request, "/"); // default root path

	// append path extracted after '/'
	if (path != NULL)
		strcat(request, path);

	// append query extracted after '?'
	if (query != NULL)
	{
		strcat(request, "?");
		strcat(request, query);
	}

	// check if request length is too long
	if ((int)strlen(request) > MAX_REQUEST_LEN)
	{
		printf("Request length exceeds %d, the maximum host length allowed\n", MAX_REQUEST_LEN);
		return 1;
	}

	printf("\tParsing URL... host %s, port %d\n", url, portNum);

	// modify rqst
	*rqst = request;

	return 0;
}