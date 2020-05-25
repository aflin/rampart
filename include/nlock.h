#ifndef OLD_LOCKING

#define LOCK_AVAILABLE	0
#define LOCK_PENDING	1
#define LOCK_GRANTED	2
#define LOCK_KEEPALIVE	3
#define LOCK_CLOSE1	4

#define MAX_LOCKS	2000

#define SERVER_CHAIN	1
#define TABLE_CHAIN	2

typedef struct LOCK
{
#ifdef STEAL_LOCKS
	short	status;
	byte	nr;
	byte	nw;
#else
	int	status;	/* What is the status of the lock */
#endif
	int	type;	/* What type of lock it is */
	ulong	sid;	/* Which server */
	PID_T	pid;	/* Which process owns the lock */
	long	nsl;	/* Next lock in server chain */
	long	psl;	/* Prev lock in server chain */
	long	ntl;	/* Next lock in table chain */
	long	ptl;	/* Prev lock in table chain */
	long	tbl;	/* Table the lock belongs to */
} LOCK;

typedef struct LTABLE
{
	char	name[40];
	int	status;
	long	hlock, tlock;
	ft_counter	twrite, iwrite;
	int	usage;
} LTABLE;

typedef struct LSERVER
{
	PID_T	pid;
	long	hlock, tlock;
} LSERVER;

#endif /* OLD_LOCKING */
