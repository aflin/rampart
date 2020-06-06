#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "texint.h"

int
TXstrcmp(const char *a, const char *b)
{
	if(NULL == a)
	{
		if(NULL == b)
		{
			return 0;
		}
		else
		{
			return 1;
		}
	}
	if(NULL == b)
	{
		return -1;
	}
	return strcmp(a, b);
}
