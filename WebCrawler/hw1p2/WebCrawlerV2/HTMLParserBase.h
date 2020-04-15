#pragma once

#ifdef _WIN64
#ifdef _DEBUG
#pragma comment (lib, "HTMLParser_debug_x64.lib")
#else
#pragma comment (lib, "HTMLParser_release_x64.lib")
#endif
#else
#ifdef _DEBUG
#pragma comment (lib, "HTMLParser_debug_win32.lib")
#else
#pragma comment (lib, "HTMLParser_release_win32.lib")
#endif
#endif

#define MAX_HOST_LEN		256
#define MAX_URL_LEN			2048
#define MAX_REQUEST_LEN		2048

class HTMLParserBase
{
public:
	HTMLParserBase();
	~HTMLParserBase();

	// input: 
	//   - htmlCode: a pointer to the page buffer
	//   - codeSize: html page size
	//   - baseURL: base URL (starting with "http://")
	//   - urlLen: size of base URL
	// output:
	//   - nLinks: number of parsed URLs, could be negative in case of error
	// return: a pointer to the buffer of parsed URLs, this buffer is allocated internally, 
	//         do not attempt to release it
	char*	Parse (char *htmlCode, int codeSize, char *baseURL, int urlLen, int *nLinks);

private:
	void	*parser;
	void	*buffer;
};