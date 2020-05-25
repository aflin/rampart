#include "txcoreconfig.h"
#include "texint.h"
#include "cgi.h"


HTPFOBJ *
closehtpfobj(HTPFOBJ *htpfobj)
{
  return (HTPFOBJ *)TXfree(htpfobj);
}

HTPFOBJ *
duphtpfobj(HTPFOBJ *htpfobj)
{
  HTPFOBJ *newobj = HTPFOBJPN;
  if(htpfobj) {
    newobj = TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(HTPFOBJ));
  }
  if(newobj) {
    *newobj = *htpfobj;
  }
  return newobj;
}
HTCSCFG *
htpfgetcharsetconfigobj(HTPFOBJ *htpfobj)
{
  if(htpfobj) {
    return htpfobj->charsetconfig;
  }
  return HTCSCFGPN;
}

TXPMBUF *
htpfgetpmbuf(HTPFOBJ *htpfobj)
{
  if(htpfobj) {
    return htpfobj->pmbuf;
  }
  return TXPMBUFPN;
}
