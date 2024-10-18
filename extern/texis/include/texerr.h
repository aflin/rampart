#ifndef _TEXERR_H
#define _TEXERR_H

/******************************************************************/

#define MAKEERROR(mod,error)	(((mod)<<24)+error)

/******************************************************************/

#define MOD_LOCK	1
#define MOD_TUP		2

/******************************************************************/
/* Error Codes for locking */

#define LOCK_SUCCESS	0
#define LOCK_RETRY	1	/* Lock temporarily unavailable */
#define LOCK_RECONNECT	2	/* Restart transaction */
#undef LOCK_FAIL                /* AIX defines it  KNG 990210 */
#define LOCK_FAIL	3	/* Permanent failure (restart won't help) */
#define LOCK_TIMEOUT	4	/* This connection has exceeded allowed time */
#define LOCK_DEADLOCK	5	/* Deadlock condition suspected, retry */

/******************************************************************/
/* Error Codes for tup handling */

#define TUP_SUCCESS	0
#define TUP_READ	1	/* Read failed. */

/******************************************************************/

#ifndef HAVE_TXERRNO
extern	int	TXerrno;
#endif

#define TXEFORMAT	1001

/******************************************************************/

#define	S0002	2	/* - Base Table undefined */
#define S1000	1000	/* - Unspecified Error */
#define S1001	1001	/* - No memory */
#define S1002	1002	/* - Invalid column number */
#define S1009	1009    /* - Invalid Handle */
#define S1010	1010	/* Function Sequence Error */
#define S1012	1012	/* - Invalid Transaction operation */
#define S1091	1091	/* - Descriptor out of range */
#define S1092	1092	/* - Option type out of range */
#define S1093	1093	/* - Invalid parameter number */
#define S1094	1094	/* - Invalid scale value */
#define	S1C00	1900	/* - Driver Not Capable */

#define N01002  51002	/* - Disconnect Error */
#define N01004  51004	/* - Data truncated */
#define N07001	7001	/* - Wrong number of parameters */
#define N08001	8001	/* - Couldn't connect */
#define N08002	8002	/* - Connection in use */
#define N08003	8003	/* - Connection not open */
#define N08004	8004	/* - Data source rejected the establishment of connection */
#define N08S01  8901    /* - Link Failed */
#define N28000	28000	/* Invalid Authorization */
#define N34000	34000	/* - Invalid cursor name */
#define N3C000	33000	/* - Cursor already exists */

#define IM001	10001	/* - Function Not Supported */

#endif /* _TEXERR_H */
