#include <string>
#include "makeDNSquestion.h"

void makeDNSquestion(char *question, char *host)
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