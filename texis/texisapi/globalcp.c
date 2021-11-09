#include "texint.h"

static void
TXglobalcpchanged()
{
  if(TXApp->fmtcp) {
    TXApp->fmtcp->apicp = globalcp;
  }
}
APICP *
TXget_globalcp()
{
  if(!globalcp) {
    globalcp = TXopenapicp();
    TXglobalcpchanged();
  }
  return globalcp;
}

APICP *
TXreinit_globalcp()
{
  APICP *newcp;

  if(globalcp) {
    globalcp=closeapicp(globalcp);
    TXglobalcpchanged();
  }
  newcp = TXget_globalcp();

  return newcp;
}
