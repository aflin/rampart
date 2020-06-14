#ifndef DBLOCK_H
#define DBLOCK_H


/******************************************************************/
/*                                                                */
/*                    WARNING! WARNING!                           */
/*                                                                */
/*  When modifying the structures in this file make sure you      */
/*  also make sure that you modify the string in texver.c         */
/*                                                                */
/*  Failure to heed this warning may have SEVERE consequences     */
/*  as far as other versions of Texis trying to execute in        */
/*  this directory.                                               */
/*                                                                */
/*  You have been warned.  No liability accepted for failing to   */
/*  follow this warning.                                          */
/*                                                                */
/*  ---                                                           */
/*    The Management                                              */
/*                                                                */
/*                                                                */
/******************************************************************/

#include <sys/types.h>

#ifdef USE_SYSV_SEM
#include <sys/ipc.h>
#include <sys/sem.h>
#endif
#ifdef USE_POSIX_SEM
#include <semaphore.h>
#endif
#ifdef USE_SHM                                        /* MAW 02-10-94 */
# ifndef OLD_LOCKING
key_t TEXISLOCKSKEY ARGS((char *));
# else
#  define TEXISLOCKSKEY(a) (0x54455849)             /* SHM key 'TEXI' */
# endif
#endif
#define TEXISLOCKSFILE "SYSLOCKS"     /* MAW 02-10-94 - macroize this */

/* #include "dbtable.h" Already included */

#ifndef PATH_MAX
#  ifdef _POSIX_PATH_MAX
#    define PATH_MAX _POSIX_PATH_MAX
#  else
#    if defined(_MAX_PATH)
#      define PATH_MAX _MAX_PATH
#    else
#      define PATH_MAX 255
#    endif
#  endif
#endif

/******************************************************************/

/* Define the type that represents a process id. */

#ifndef PID_T
#ifdef unix
#  define PID_T		pid_t
#else
#  ifdef WINNT
#    define PID_T	DWORD
#  else
#    define PID_T	long
#  endif
#endif
#endif

/******************************************************************/

#ifdef NOLOCK
#define LTABLES		40
#define NSERVERS	7
#else
#define LTABLES		400
#define NSERVERS	2048
#endif
#define R_LCK		1
#define W_LCK		2
#define W_WNT		4

#define INDEX_READ	8
#define INDEX_WRITE	16
#define INDEX_VERIFY	32

#define V_LCK		64

typedef struct tabSEM {
#ifdef HAVE_SEM
	int	wwant;
	int	writel;
	int	nreader;
	unsigned int	iwwant;
	unsigned int	iwrite;
	unsigned int	irwant;
	unsigned int	iread;
#else
	int	sem[NSERVERS];
	int	value;
	int	lastid;
#endif
} SEM ;

typedef struct tagTBLOCK {
	char	name[DDNAMESZ];
	SEM	dbflock;
#ifdef NEVER
	SEM	tblockr;
	SEM	tblockw;
#endif
	int	usage;
#ifndef NEVER
	ft_counter tread;
	ft_counter twrite;
	ft_counter iread;
	ft_counter iwrite;
#endif
} TBLOCK ;

#ifdef USE_POSIX_SEM
typedef sem_t	*TXSEMID;
#elif defined(USE_NTSHM)
typedef HANDLE	TXSEMID;
#else /* !USE_POSIX_SEM && !USE_NTSHM */
typedef int	TXSEMID;
#endif /* !USE_POSIX_SEM && !USE_NTSHM */

typedef struct tagIDBLOCK {
	char	verstr[12];
	int	nservers;
	TXSEMID	semid;
	ft_counter	lcount;
#ifndef OLD_LOCKING
	LSERVER	servers[NSERVERS];
	LTABLE  tblock[LTABLES];
	LOCK	locks[MAX_LOCKS];
	PID_T	curpid;
#ifdef NEVER
	char	path[PATH_MAX];
#else
	char	path[255];
#endif
#ifdef USE_SHM                                        /* MAW 02-10-94 */
	int	shmid;
#endif /* USE_SHM */
#else /* OLD_LOCKING */
	TBLOCK	tblock[LTABLES];
	unsigned short	serid[NSERVERS];
	PID_T	curpid;
	char	path[PATH_MAX];
#ifdef USE_SHM                                        /* MAW 02-10-94 */
	int	shmid;
#endif /* USE_SHM */
#ifndef NEVER
	unsigned int	nwwant[NSERVERS];
	unsigned int	nwrite[NSERVERS];
	unsigned int	nread[NSERVERS];
#else /* NEVER */
	unsigned short	state[NSERVERS];
#endif /* NEVER */
	PID_T	pid[NSERVERS];
	PID_T	cleanpid;
	unsigned int	iwwant[NSERVERS];
	unsigned int	iwrite[NSERVERS];
	unsigned int	irwant[NSERVERS];
	unsigned int	iread[NSERVERS];
#endif /* OLOLDOCKING */
} IDBLOCK ;

#  ifdef USE_NTSHM
typedef HANDLE	TXLOCKOBJHANDLE;
#    define TXLOCKOBJHANDLE_INVALID_VALUE	((void *)(-1))
#  else /* !USE_NTSHM */
typedef int	TXLOCKOBJHANDLE;		/* semid or file descriptor */
#    define TXLOCKOBJHANDLE_INVALID_VALUE	(-1)
#  endif /* !USE_NTSHM */

typedef struct tagDBLOCK {
	IDBLOCK	*idbl;				/* Shared mem or file map */
	TXSEMID	semid;  /* Needed when semid can be different between procs */
	TXSEMID	lsemid; /* The semaphore I locked, make sure we unlock same */
	TXLOCKOBJHANDLE  hFileMap;		/* only for Windows */
	int	dumponbad;
	struct DDIC_tag	*ddic;
} DBLOCK ;

#define TX_DBLOCK_PATH(dblock)	((dblock) && (dblock)->idbl ?   \
                                 (dblock)->idbl->path : NULL)

#endif
