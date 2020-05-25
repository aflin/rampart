#ifndef JTREADEX_H
#define JTREADEX_H

#include <stdio.h>
#include <sys/types.h>

#define FREADX struct tagFREADX
FREADX {
	uchar	*end;
	size_t	tailsz;
	size_t	len;
	FILE	*fh;
	uchar	*buf;
	FFS	*ex;
} ;

int	filereadex ARGS((FREADX *));

#endif /* JTREADEX_H */
