#ifndef JDBF_I_H
#define JDBF_I_H


struct JDBF_struct
{
	int	fd;
	char	*filename;
	EPI_OFF_T	current, next, prev;
	char	*currentdata;
	size_t	datasz;
	size_t	currentdatabufsz;
	unsigned long	PayloadCRC;
	EPI_OFF_T	s_at, r_at;
};

typedef struct JDBF_HEAD
{
	EPI_OFF_T	BlockID;
	int		BlockType;
	int		PayloadSize;
	EPI_OFF_T	DataLoc;
	EPI_OFF_T	NextBlock;
	EPI_OFF_T	PrevBlock;
	unsigned long	HeadCRC;
	unsigned long	PayloadCRC;
} JDBF_HEAD;

enum JDBF_BLOCK_TYPE
{
JDBF_UNUSED,
JDBF_DATA,
JDBF_REDIR,
JDBF_FREE,
};

typedef enum JDBF_READMODE
{
JDBF_READTOBUF,
JDBF_ALLOCREAD,
JDBF_READTMP
} JDBF_README;

typedef enum JDBF_NETCOMMAND
{
JDBF_ENDOFF,
JDBF_TAILOFF,
JDBF_READHEAD,
JDBF_READDATA,
JDBF_WRITEHEAD,
JDBF_WRITEDATA
} JDBF_NETCOMMAND;

#define JDBFOPEN(a,b,c) open((a),(b),(c))
#define JDBFCLOSE(a)  close((a)->fd)
#define JDBFWRITE(a,b,c)  write((a)->fd, (b), (c))
#define JDBFREAD(a,b,c)  read((a)->fd, (b), (c))

#define jdbf_readhead(a,b,c) jdbf_net_readhead((a),(b),(c))
#define jdbf_readdata(a,b,c) jdbf_net_readdata((a),(b),(c))
#define jdbf_writehead(a,b,c) jdbf_net_writehead((a),(b),(c))
#define jdbf_write_data_block(a,b,c,d,e) jdbf_net_write_data_block((a),(b),(c),(d),(e))
#define jdbf_tailoff(a) jdbf_net_tailoff((a))
#define jdbf_close_int(a) jdbf_net_close_int((a))
#define jdbf_open_int(a,b) jdbf_net_open_int((a),(b))

#define JDBFPN  ((JDBF *)NULL)


void   jdbf_stats ARGS((void));

/*
EPI_OFF_T  jdbf_beginalloc ARGS((JDBF *df));
int        jdbf_contalloc ARGS((JDBF *df, byte *buf, size_t sz));
size_t     jdbf_undoalloc ARGS((JDBF *df, byte **bufp));
EPI_OFF_T  jdbf_endalloc ARGS((JDBF *df, size_t *szp));

size_t jdbf_readchunk ARGS((JDBF *df, EPI_OFF_T at, byte *buf, size_t sz));
size_t jdbf_nextblock ARGS((JDBF *df, EPI_OFF_T *at, byte **buf, size_t *bsz,
                            byte **data, EPI_OFF_T *dat, size_t *dtot));
EPI_OFF_T  jdbf_tell  ARGS((JDBF *df));
int    jdbf_flush ARGS((JDBF *df));
char *jdbf_proff_t ARGS((EPI_OFF_T at));
*/

#endif /* !JDBF_I_H */
