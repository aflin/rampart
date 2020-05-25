#ifndef TX_AUTHNEGOTIATE_H
#  define TX_AUTHNEGOTIATE_H

typedef struct TXsasl_tag       TXsasl;
typedef struct TXgssapi_tag     TXgssapi;
typedef struct TXkerberos_tag   TXkerberos;
typedef struct TXsspi_tag       TXsspi;

typedef struct TXnegotiateObj_tag
{                                       /* (A) = alloced */
  TXPMBUF               *pmbuf;
  int                   traceAuth;      /* traceauth & 0x4 */
  const HTPSPACE        *pspace;        /* protection space */
  char                  *localIPPort;   /* (A) local IP and port of socket */
  char                  *remoteHost;    /* (A) remote hostname */
  char                  *remoteIPPort;  /* (A) remote IP and port of socket */
  HTBUF                 *outBuf;        /* next data to send to peer */
  void                  *securityApiObj;/* TXsasl or TXsspi object */
}
TXnegotiateObj;
#  define TXnegotiateObjPN      ((TXnegotiateObj *)NULL)

TXkerberos *TXkerberosOpen(TXnegotiateObj *authObj);
int TXkerberosClose(TXkerberos *kerberosObj);
void *TXkerberosGetPrincipal(TXkerberos *kerberosObj);
void *TXkerberosGetCredentialsCache(TXkerberos *kerberosObj);

TXgssapi *TXgssapiOpen(TXnegotiateObj *authObj);
int TXgssapiClose(TXgssapi *gssapiObj);
void *TXgssapiGetCredentials(TXgssapi *gssapiObj);

#  define API_PROTOTYPES(tok)                                           \
TX##tok *TX##tok##Open(TXnegotiateObj *authObj);                        \
int TX##tok##Close(TX##tok *tok##Obj);                                  \
int TX##tok##Authenticate(TXnegotiateObj *authObj, TX##tok *tok##Obj,   \
         int respCode, int authenticateServer, const byte *inData,      \
         size_t inDataSz, HTBUF *outBuf, HTERR *hterrno);               \
int TX##tok##SocketClosed(TX##tok *tok##Obj)

API_PROTOTYPES(sasl);
API_PROTOTYPES(sspi);

int     TXsaslProcessInit(TXPMBUF *pmbuf);
int TXsaslSetPluginPath(TXPMBUF *pmbuf, const char *pluginPath,
                        int traceAuth);
char *TXsaslGetPluginPath(TXPMBUF *pmbuf);
int TXsaslSetMechanismList(TXPMBUF *pmbuf, const char *mechanismList,
                           int traceAuth);
char *TXsaslGetMechanismList(TXPMBUF *pmbuf);
char **TXsaslGetMechanismsAvailable(TXPMBUF *pmbuf, int traceAuth);
int TXsaslGetLibCheckVersion(TXPMBUF *pmbuf);
int TXsaslSetLibCheckVersion(TXPMBUF *pmbuf, int yes);

int TXsspiSetPackageList(TXPMBUF *pmbuf, const char *packageList,
                         int traceAuth);
char *TXsspiGetPackageList(TXPMBUF *pmbuf);
char **TXsspiGetPackagesAvailable(TXPMBUF *pmbuf, int traceAuth);

extern const char       TXcannotCompleteNegotiateAuth[];

#endif /* TX_AUTHNEGOTIATE_H */
