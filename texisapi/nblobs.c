#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"

/*
 *	WTF:  This version of the module implements blobs in terms
 *	of indirect.
 */


/******************************************************************/
/*	Create a new blob.
 *
 *	This will create a new blob associated with the specified
 *	database and table, and copy the buffer passed into the
 *	blob.
 *
 *	Returns the newly created blob.
 */

BLOB	*newblob(database, table, buf, n)
char *database;		/* Database the table is in */
char *table;		/* Table the blob will be in */
char *buf;		/* Data to put in the blob */
int n;			/* Size of buf in bytes */
{
	BLOB	*blob;
	FILE	*fh;

	blob = (BLOB *)calloc(1, sizeof(BLOB));
	blob->filename = newindirect(database, table, (char *)NULL);
	blob->off = 0;
	if (blob->filename == (char *)NULL)
	{
		free(blob);
		return (BLOB *)NULL;
	}
	fh = fopen(blob->filename, "wb");
	fwrite(buf, 1, n, fh);
	fclose(fh);
	return blob;
}

/******************************************************************/
/*	Put data into a blob
 *
 *	This will put the contents of the buffer into the blob.
 *
 *	Returns the blob where the data was actually put.
 *
 *	Warning: Return blob may be different than that passed to
 *	putblob.
 */

BLOB	*putblob(bl, buf, n)
BLOB *bl;	/* The blob to stuff */
char *buf;	/* The data to put in there */
int n;		/* The size of the data */
{
	FILE	*fh;

	if (bl == (BLOB *)NULL)
		return bl;
	fh = fopen(bl->filename, "wb");
	fwrite(buf, 1, n, fh);
	fclose(fh);
	return bl;
}

/******************************************************************/
/*	Retrieve data from a blob.
 *
 *	This will read in the data from the blob, and return a
 *	allocated buffer containing the data, and the number of
 *	bytes actually read in.
 *
 *	Returns 1 on success, 0 on failure.
 */

int	 getblob(bl, buf, n)
BLOB *bl;	/* Blob to read from */
char **buf;	/* Where to put the buf pointer */
int *n;		/* Where to put the size of the buf */
{
	FILE	*fh;

	if (bl == (BLOB *)NULL)
		return 0;
	fh = fopen(bl->filename, "wb");
	FSEEKO(fh, (off_t)0, SEEK_END);
	*n = FTELLO(fh);                        /* WTF overflow */
	FSEEKO(fh, (off_t)0, SEEK_SET);
	*buf = (char *)malloc(*n);
	if (*buf == (char *)NULL)
	{
		fclose(fh);
		return 0;
	}
	fread(*buf, 1, *n, fh);
	fclose(fh);
	return 1;

}

/******************************************************************/
/*	Create a url.
 *
 *	This generates a url that can be used to store data in on
 *	the server.  If url is NULL this will create a url which
 *	can be used to store data in.  If url is not NULL then
 *	url can be used.
 *
 *	Returns a url that can be stored if url is NULL, otherwise
 *	returns url.  The url that is returned is allocated by
 *	newindirect, and so MUST be freed by the user.
 *
 *	WTF:  What is correct behaviour on non NULL?
 */

char	*newindirect(database, table, url)
char *database;		/* The database to hold the url */
char *table;		/* The table that will hold the url */
char *url;		/* The old url */
{
	EPI_STAT_S	statb;
	char		fname[PATH_MAX], *uname;
	int		f;

	if (url != (char *) NULL)
		return strdup(url);
	if (EPI_STAT(database, &statb) == -1)
		return (char *)NULL;
	strcpy(fname, database);
	strcat(fname, PATH_SEP_S);
	strcat(fname, table);
	if (EPI_STAT(fname, &statb) == -1)
	{
		if (mkdir(fname, 0777) == -1)
			return (char *)NULL;
	}
	else
	{
		if ((statb.st_mode & S_IFDIR) == 0)
			return (char *)NULL;
	}
	do {
		uname = TXtempnam(fname, ".turl", CHARPN);
		f = open(uname, O_RDWR | O_CREAT | O_EXCL, 0666);
	} while (f == -1 && errno == EEXIST);
	if (f != -1)
	{
		close(f);
		return uname;
	}
	else
		return (char *)NULL;
}

/******************************************************************/

