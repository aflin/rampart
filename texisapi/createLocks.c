#include "txcoreconfig.h"
#include "texint.h"

#ifdef _WIN32
# define HAVE_CREATE_LOCKS_MONITOR
#else
# undef HAVE_CREATE_LOCKS_MONITOR
#endif


DBLOCK *
TXdblockOpenViaMethods(TXPMBUF *pmbuf, const char *path,
		       TXCREATELOCKSMETHOD *methods, int timeout, int *sid)
/* Attempts to open locks for database `path' via `methods'.
 * Returns lock structure (and sets `*sid') on success, NULL on error.
 * Note: caller must set DBLOCK.ddic in return value.
 */
{
	static const char	fn[] = "TXdblockOpenViaMethods";
	int			i, res, haveMoreMethods, didSomething = 0;
	DBLOCK			*dblock = NULL;
	TXPMBUF			*pmbufSave = TXPMBUFPN;
	TXPMBUF			*pmbufToUse = pmbuf;	/* alias */
	const char		*curMethodName;
	size_t			curMethodMsgStartIdx = 0;

	*sid = 0;
	for (i = 0;
	     i < (int)TXCREATELOCKSMETHOD_NUM &&
		   (unsigned)methods[i] < (unsigned)TXCREATELOCKSMETHOD_NUM &&
		     !dblock;
	     i++)
	{					/* for each method */
		/* Note the start of this method's msgs: */
		if (pmbufSave)
			curMethodMsgStartIdx = txpmbuf_nmsgs(pmbufSave);

		curMethodName = TXcreateLocksMethodToStr(methods[i]);

		/* If we have more methods to try after this one,
		 * cache any putmsgs until after all methods have been
		 * tried, so that we can suppress errors that can be
		 * ignored (from previous failed methods, after a
		 * successful method), or show all errors in order
		 * (once we are caching errors).
		 */
		haveMoreMethods =
			(i + 1 < (int)TXCREATELOCKSMETHOD_NUM &&
			 (unsigned)methods[i + 1] <
			 (unsigned)TXCREATELOCKSMETHOD_NUM);
		/* But do not cache if verbose, so we see failures
		 * that might otherwise be suppressed (e.g. from early
		 * methods):
		 */
		if (haveMoreMethods && !pmbufSave &&
		    !TX_CREATELOCKS_VERBOSE_IS_SET() &&
		    !TXgetlockverbose())
		{
			pmbufSave = txpmbuf_open(TXPMBUF_NEW);
			curMethodMsgStartIdx = 0;
			txpmbuf_setflags(pmbufSave, TXPMBUFF_PASS, 0);
			txpmbuf_setflags(pmbufSave, TXPMBUFF_SAVE, 1);
			pmbufToUse = pmbufSave;
		}

		if (TX_CREATELOCKS_VERBOSE_IS_SET())
			txpmbuf_putmsg(pmbufToUse, MINFO, fn,
			       "Creating locks for database %s via %s method",
				       path, curMethodName);
		switch (methods[i])
		{
		case TXCREATELOCKSMETHOD_UNKNOWN:
			break;
#ifdef HAVE_CREATE_LOCKS_MONITOR
		case TXCREATELOCKSMETHOD_MONITOR:
			didSomething = 1;
			res = TXcreateLocksViaMonitor(pmbufToUse, path,
						      timeout);
			/* Yap about intermediate success (if
			 * verbose), in case lock open fails below: we
			 * still want to know we talked to monitor ok:
			 */
			if (TX_CREATELOCKS_VERBOSE_IS_SET())
				txpmbuf_putmsg(pmbufToUse, MINFO, fn,
		      "Request to monitor to create locks for database %s %s",
					       path,
					      (res ? "succeeded" : "failed"));
			if (!res) break;	/* failed */
			/* Fall through and open locks (which should
			 * attach to just-created-by-monitor locks):
			 */
#endif
		case TXCREATELOCKSMETHOD_DIRECT:
			didSomething = 1;
			dblock = TXdblockOpenDirect(pmbufToUse, path, sid,
					     TXbool_False /* !readOnly */);
			break;
		default:
			txpmbuf_putmsg(pmbufToUse, MERR + UGE, fn,
				       "Invalid createlocks method #%d (%s)",
				       (int)methods[i], curMethodName);
			break;
		}

		if (TX_CREATELOCKS_VERBOSE_IS_SET())
		{
			if (dblock)
				txpmbuf_putmsg(pmbufToUse, MINFO, fn,
		   "Successfully created locks for database %s via %s method",
					       path, curMethodName);
			else
			{
				const char	*aux1, *aux2;

				if (haveMoreMethods)
				{
					aux1 = ", will try ";
					aux2 =
				     TXcreateLocksMethodToStr(methods[i + 1]);
				}
				else
				{
					aux1 = ", and no more methods available";
					aux2 = "";
				}
				txpmbuf_putmsg(pmbufToUse, MINFO, fn,
		   "Could not create locks for database %s via %s method%s%s",
					       path, curMethodName,
					       aux1, aux2);
			}
		}
	}

	/* Flush/suppress buffered messages, as appropriate: */
	if (TX_CREATELOCKS_VERBOSE_IS_SET())
		/* If verbose, there should be no `pmbufSave',
		 * but flush all of it just in case:
		 */
		TXpmbufCopyMsgs(pmbuf, pmbufSave, 0, 0);
	else if (dblock)			/* success */
		/* We can suppress all the previous (failed) methods'
		 * messages, as they may be confusing in light of the
		 * last method's success.  But at least print the last
		 * method's messages, in case any info/warning
		 * messages (e.g. user added without a password):
		 */
		TXpmbufCopyMsgs(pmbuf, pmbufSave, 0, curMethodMsgStartIdx);
	else					/* complete failure */
		/* Print all messages: */
		TXpmbufCopyMsgs(pmbuf, pmbufSave, 0, 0);
	pmbufSave = txpmbuf_close(pmbufSave);	/* all done with it */
	pmbufToUse = pmbuf;
	curMethodMsgStartIdx = 0;

	if (!dblock)				/* failed */
	{
		txpmbuf_putmsg(pmbufToUse, MERR, fn,
			 "Could not open locking mechanism for database %s%s",
			       path, (didSomething ? "" :
				      ": No valid createlocks methods set"));
		goto err;
	}
	goto done;

err:
	dblock = closedblock(pmbufToUse, dblock, *sid,
			     TXbool_False /* !readOnly */);
	*sid = 0;
done:
	return(dblock);
}
