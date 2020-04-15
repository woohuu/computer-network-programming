#include <string>
#include "parseName.h"

int parseName(char *buf, char *name, int *jump)
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