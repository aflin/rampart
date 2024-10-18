#ifndef TX_STRBUF_H
#define TX_STRBUF_H


typedef struct TXSTRBUF{
	size_t	len;
	size_t	alloced;
	char *data;
} STRBUF;
#define STRBUFPN        ((STRBUF *)NULL)

STRBUF *openstrbuf ARGS((void));
STRBUF *closestrbuf ARGS((STRBUF *));
int	addstrbuf ARGS((STRBUF *, char *, int));
int	resetstrbuf ARGS((STRBUF *));
int	lenstrbuf ARGS((STRBUF *));

#endif
