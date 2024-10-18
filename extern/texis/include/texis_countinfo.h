#ifndef TEXIS_COUNTINFO_H
#define TEXIS_COUNTINFO_H
/************************************************************************/
/* Stats for Vortex $rows.min, $rows.max etc.; -1 if unknown.
 * `rows...' values may change as rows are fetched:
 */
typedef struct TXCOUNTINFO
{
	/* rowsMatchedMin/Max: The min/max number of table rows matching
	 * the query, *before* GROUP BY, likeprows, aggregates (count(*))
	 * limit the number of returned rows (or multivaluetomultirow
	 * increase the number returned rows), and as if no skip/max.
	 * -1, -2 if unknown (less than zero to indicate unknown, yet
	 * min != max to also indicate exact count unknown):
	 */
	EPI_HUGEINT	rowsMatchedMin;
	EPI_HUGEINT	rowsMatchedMax;
	/* rowsReturnedMin/Max: The min/max number of rows that will
	 * actually be returned by n_gettx(), ie. after GROUP BY etc.
	 * but still as if no skip/max.  -1, -2 if unknown:
	 */
	EPI_HUGEINT	rowsReturnedMin;/* min. n_gettx()-returned rows */
	EPI_HUGEINT	rowsReturnedMax;/* max. "" */
	/* indexCount: Number of matching records obtained from index.
	 * -1 if unknown:
	 */
	EPI_HUGEINT	indexCount;
}
TXCOUNTINFO;
#define TXCOUNTINFOPN	((TXCOUNTINFO *)NULL)

#define TX_ISVALIDCOUNT(n)	((n) >= (EPI_HUGEINT)0)

#endif /* TEXIS_COUNTINFO_H */
