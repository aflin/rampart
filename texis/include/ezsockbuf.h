#ifndef EZSOCKBUF_H
#define EZSOCKBUF_H

typedef struct TXEZsockbuf {
  int socket;
  char *buffer;
  size_t bufsz;
  char *head;
  char *tail;
  size_t availsz;
  size_t freesz;
} TXEZsockbuf;

TXEZsockbuf *TXEZsockbuf_client(char *host, int port, TXPMBUF *pmbuf);
TXEZsockbuf *TXEZsockbuf_close(TXEZsockbuf *ezsb);

char *TXEZsockbuf_getline(TXEZsockbuf *ezsb);
int TXEZsockbuf_putbuffer(TXEZsockbuf *ezsb, char *data, size_t len);
int TXEZsockbuf_putline(TXEZsockbuf *ezsb, char *data, size_t len);

#endif /* EZSOCKBUF_H */
