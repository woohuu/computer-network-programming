/*
* Yi Liu
* UIN: 827008592
* CSCE612 Spring 2019
*/

#include "stdafx.h"

int openFile(char *fileName, char **fileBuf)
{
	// create file handle
	HANDLE hFile = CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		printf("CreateFile failed with %d\n", GetLastError());
		return 1;
	}

	// get file size
	LARGE_INTEGER li;
	if (GetFileSizeEx(hFile, &li) == 0)  // GetFileSizeEx returns a boolean value
	{
		printf("GetFileSizeEx error %d\n", GetLastError());
		return 1;
	}

	// read file into buffer
	int fSize = (DWORD)li.QuadPart;
	DWORD bytesRead;
	char *buf = new char[fSize];
	BOOL bRet = ReadFile(hFile, buf, fSize, &bytesRead, NULL);
	if (bRet == 0 || bytesRead != fSize)
	{
		printf("ReadFile failed with %d\n", GetLastError());
		return 1;
	}
	*fileBuf = buf;

	// close file handle after reading into buffer is done
	if (CloseHandle(hFile) == 0)
	{
		printf("CloseHandle failed with %d\n", GetLastError());
		return 1;
	}

	printf("Opened %s with the size %d\n", fileName, fSize);

	return 0;
}