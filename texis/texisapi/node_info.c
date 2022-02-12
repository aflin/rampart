#include "texint.h"

static DBTBL *
showopentables(DDIC *ddic) {
	TXLockRequest *request = NULL, *response = NULL;
  json_t *j_resp, *t, *value, *value2, *j_name, *j_table, *j_refcnt;
	size_t index, index2;
	char *res = NULL;
	DBTBL *outdbtbl = NULL;
	DD *dd = NULL;
	FLD *fld_name, *fld_refcnt, *fld_exclusive, *fld_pending;

	if(ddic && ddic->dblock && ddic->dblock->lockServerSocket) {
		dd = opennewdd(4);
		putdd(dd, "NAME", "varchar", DDNAMESZ, 1);
		putdd(dd, "REFCNT", "int64", 1, 1);
		putdd(dd, "EXCLUSIVE", "int64", 1, 1);
		putdd(dd, "PENDING", "int64", 1, 1);
		outdbtbl = createdbtbl(ddic, dd, NULL, "infotable", "", 'T');
		dd = closedd(dd);
		if(!outdbtbl) goto done;
		fld_name = dbnametofld(outdbtbl, "NAME");
		fld_refcnt = dbnametofld(outdbtbl, "REFCNT");
		fld_exclusive = dbnametofld(outdbtbl, "EXCLUSIVE");
		fld_pending = dbnametofld(outdbtbl, "PENDING");
		request = TXlockRequest_CreateStaticString("{\"status\":null}\n", -1);
		response = TXlockRequest(ddic->dblock->lockServerSocket, request);
    if(response) {
      j_resp = TXlockRequest_GetJson(response);
			res = json_dumps(j_resp, JSON_INDENT(3));
			t = TXjsonPath(j_resp, ".resources.children", NULL);
			if(t) {
				res = json_dumps(t, JSON_INDENT(3));
				json_array_foreach(t, index, value) {
					res = json_dumps(value, JSON_INDENT(3));
					j_name = TXjsonPath(value, ".name", NULL);
					if(!strcmp(json_string_value(j_name), ddic->pname)) {
						j_table = TXjsonPath(value, ".children", NULL);
						res = json_dumps(j_table, JSON_INDENT(3));
						json_array_foreach(j_table, index2, value2) {
							j_refcnt = TXjsonPath(value2, ".refcnt", NULL);
							if(json_number_value(j_refcnt) > 0.5) {
								const char *t_name;
								json_t *t;
								ft_int64 t_refcnt, t_excl, t_pend;
								int lock_status;

								j_name = TXjsonPath(value2, ".name", NULL);
								t_name = json_string_value(j_name);
								t_refcnt = (ft_int64)json_number_value(j_refcnt);
								putfld(fld_name, (void *)t_name, strlen(t_name) + 1);
								putfld(fld_refcnt, &t_refcnt, 1);
								t = TXjsonPath(value2, ".locks.pending", NULL);
								t_pend = json_array_size(t);
								putfld(fld_pending, &t_pend, 1);
								lock_status = json_number_value(TXjsonPath(value2, ".locks.current_state.as_int", NULL));
								t_excl = (lock_status & 0xAA ? 1 : 0);
								putfld(fld_exclusive, &t_excl, 1);
								putdbtblrow(outdbtbl, NULL);
							}
						}
						TXrewinddbtbl(outdbtbl);
					}
				}
			}
      json_decref(j_resp);
    }
	}
done:
	return outdbtbl;
}


DBTBL *
TXinfotableopen(DDIC *ddic, char *name) {
	if(!strcmpi(name, "showopentables")) {
		return showopentables(ddic);
	}
	return NULL;
}

DBTBL *
TXnode_info_prep(IPREPTREEINFO *prepinfo, QNODE *query, QNODE *parentquery, int *success)
{
	DDIC *ddic = prepinfo->ddic;
	QUERY *q = query->q;
	DBTBL *rc = TXinfotableopen(ddic, query->tname);

	q->out = rc;

	return rc;
}
/****************************************************************************/

int
TXnode_info_exec(QNODE *query, FLDOP *fo, int direction, int offset, int verbose)
/* Returns -1 at EOF, 0 if row obtained.
 */
{
	QUERY *q = query->q;
	DBTBL *rc;

	int locked;
	int skipped;

	query->state = QS_ACTIVE;
	q->state = QS_ACTIVE;

	/*
	 * Actually read the row from the table
	 */
	rc = tup_read(q->out, fo, direction, offset, &skipped, &query->countInfo);
	q->nrows += skipped;

	if (rc == (DBTBL *)NULL)
	{
		if(verbose)
			putmsg(MINFO, NULL, "No more rows [%d] from %s", q->nrows, q->out->rname);
		return -1;
	}
	else
	{
		q->nrows += 1;
		if(verbose)
			putmsg(MINFO, NULL, "Read %d rows so far from %s", q->nrows, q->out->rname);
		return 0;
	}
}
