#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"
#include "txlic.h"


int
TXsetTexisApicpDefaults(cp, setBuiltin, setTexis5)
APICP	*cp;		/* (in/out) APICP to modify */
int	setBuiltin;	/* (in) nonzero: set builtin not texis.ini defaults */
int	setTexis5;	/* (in) nonzero: set Texis 5- defaults, else current*/
/* Modifies `cp' to have Texis defaults.  Assumes `cp' was just opened
 * via openapicp().  Note that other, non-APICP Texis defaults (eg.
 * allineardict) may be set elsewhere.
 * Returns 0 on error.
 */
{
	char	*t;
	int	ret;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	/* Texis defaults common to version 5- and 6+: */

	/* KNG 20090630 Only set Texis defaults that have not been
	 * overridden/set via texis.ini:
	 */

	if (setBuiltin || !TxApicpDefaultIsFromTexisIni.prefixproc)
		cp->prefixproc = 0;

	if (setBuiltin || !TxApicpDefaultIsFromTexisIni.keepnoise)
		cp->keepnoise = 0;

	if (setBuiltin || !TxApicpDefaultIsFromTexisIni.keepeqvs)
		cp->keepeqvs = 0;

	if (setBuiltin || !TxApicpDefaultIsFromTexisIni.minwordlen)
		cp->minwordlen = 255;

	if (setBuiltin || !TxApicpDefaultIsFromTexisIni.sdexp)
	{
		cp->sdexp = TXfree(cp->sdexp);
		cp->sdexp = (byte *)TXstrdup(pmbuf, __FUNCTION__, "");
	}

	if (setBuiltin || !TxApicpDefaultIsFromTexisIni.edexp)
	{
		cp->edexp = TXfree(cp->edexp);
		cp->edexp = (byte *)TXstrdup(pmbuf, __FUNCTION__, "");
	}

	if (setBuiltin)
	{
		if (!(t = TXstrdup(pmbuf, __FUNCTION__,
				   (char *)TxApicpBuiltinDefault.eqprefix)))
			goto err;
		cp->eqprefix = TXfree(cp->eqprefix);
		cp->eqprefix = (byte *)t;
	}
	/* Bug 6933: else do not set from shmem, even if
	 * !TxApicpDefaultIsFromTexisIni.eqprefix: we want our
	 * --install-dir and/or texis.ini to take effect (may be moot
	 * for eqprefix default `builtin' but still...), which it
	 * already did during TXinitApp(); and shmem strings are being
	 * deprecated in favor of texis.ini and command line.
	 */

	if (setBuiltin)
	{
		if (!(t = TXstrdup(pmbuf, __FUNCTION__,
				   (char *)TxApicpBuiltinDefault.ueqprefix)))
			goto err;
		cp->ueqprefix = TXfree(cp->ueqprefix);
		cp->ueqprefix = (byte *)t;
	}
	/* Bug 6933: else do not set from shmem, even if
	 * !TxApicpDefaultIsFromTexisIni.ueqprefix: we want our
	 * --install-dir and/or texis.ini to take effect, which it
	 * already did during TXinitApp(); and shmem strings are being
	 * deprecated in favor of texis.ini and command line.
	 */

	if (
#ifndef EPI_ENABLE_APICP_6
	    TxEpiEnableApicp6 == 1 &&		/* Version 6+ enabled */
#endif /* !EPI_ENABLE_APICP_6 */
	    !setTexis5)
	{
		/* Version 6+: defaults same as Vortex, to avoid confusion.
		 * See also source/vortex/vtexis.c updateapicp(), and
		 * source/net/{napi3.n,ntexis.n} setquerysettings():
		 */
		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.denymode)
			cp->denymode = API3DENYWARNING;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.alpostproc)
			cp->alpostproc = 0;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.allinear)
			cp->allinear = 0;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.alwild)
			cp->alwild = 1;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.alnot)
			cp->alnot = 1;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.alwithin)
			cp->alwithin = 0;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.alintersects)
			cp->alintersects = 0;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.alequivs)
			cp->alequivs = 0;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.exactphrase)
			cp->exactphrase = 0;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.qminwordlen)
			cp->qminwordlen = 2;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.qminprelen)
			cp->qminprelen = 2;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.qmaxsets)
			cp->qmaxsets = MAXSELS;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.qmaxsetwords)
			cp->qmaxsetwords = 500;

		if (setBuiltin || !TxApicpDefaultIsFromTexisIni.qmaxwords)
			cp->qmaxwords = 1100;
		/* wtf TXallineardict = 0; */
	}

	ret = 1;				/* success */
	goto done;

err:
	ret = 0;
done:
	return(ret);
}

APICP *
TXopenapicp()
{
	APICP *cp;

	cp = openapicp();
	if(!cp)
		return cp;

	TXsetTexisApicpDefaults(cp, 0, 0);
	return cp;
}
