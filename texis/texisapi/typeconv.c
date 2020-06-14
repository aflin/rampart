#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include "os.h"
#include "dbquery.h"

int
dbttosqlt(t)
int t;
{
	switch(t)
	{
		case FTN_BYTE :
			return SQL_BINARY ;
		case FTN_CHAR :
			return SQL_CHAR ;
		case FTN_DECIMAL :
			return SQL_DECIMAL;
		case FTN_DOUBLE :
			return SQL_DOUBLE;
		case FTN_DWORD :
			return SQL_INTEGER;
		case FTN_FLOAT :
			return SQL_FLOAT;
		case FTN_INT :
		case FTN_INTEGER :
		case FTN_LONG :
			return SQL_INTEGER;
		case FTN_SHORT :
		case FTN_SMALLINT :
		case FTN_WORD :
			return SQL_SMALLINT;
		case FTN_BLOB :
			return SQL_LONGVARBINARY;
		case FTN_HANDLE :
			return SQL_INTEGER;
	}
	return 0;
}
