/* This gets included into merge.c several times for several versions
 * of the merge_onepass() function.
 */

static int FUNC(
merge_onepass)(MERGE *m, PILE *out)
/* Does one-pass merge of current piles (merge_newpile() and
 * merge_addpile() ones) to `out'.  Returns 0 on error.
 */
{
  int           ret;
  PILE          *p, *p1 = NULL, *p2 = NULL;
  MERGECMP      *cmp;
  void          *usr;
  int           (*insert) ARGS((FHEAP *fh, void *key));
  void          (*deletetop) ARGS((FHEAP *fh));

  insert = m->heap->insert;                     /* alias for speed */
  deletetop = m->heap->deletetop;
  m->doneitems = (EPI_HUGEINT)0;
  while (fheap_num(m->heap) > 2)                /* merge heap until small */
    {
      p = (PILE *)fheap_top(m->heap);
      /* wtf save pile_put() return value in case do-merge flag returned?: */
      /* pile_put() may update meter via merge_incdone(): */
      if (!pile_put(out, p)) goto err;          /* write out top block */
      deletetop(m->heap);                       /*   and delete it */
#ifdef MERGE_METER
      m->doneitems++;                           /* we put an item */
      METER_UPDATEDONE(m->meter, m->doneitems);
#endif /* MERGE_METER */
      switch (pile_get(p))
        {
        case 1:                                 /* more data: re-insert */
          if (!insert(m->heap, p))
            {
              closepile(p);                     /* close "orphan" pile */
              goto err;
            }
          break;
        case 0:                                 /* EOF */
          closepile(p);
          break;
        default:                                /* error */
          closepile(p);
          goto err;
        }
    }

  /* 2 piles or less in heap: merge in-line without heap, to save time.
   * Might also save time at 3+ piles, but that's unlikely to occur with
   * a large number of items; 1-2 piles occurs often (eg. index update,
   * token file merge).
   */
  cmp = m->cmp;                                 /* alias for speed */
  usr = m->usr;
  switch (fheap_num(m->heap))
    {
    case 2:                                     /* 2-pile merge */
      p1 = (PILE *)fheap_top(m->heap);
      deletetop(m->heap);                       /* remove p1 from heap */
      p2 = (PILE *)fheap_top(m->heap);
      deletetop(m->heap);                       /* remove p2; heap empty */
      p = p1;
      goto put;                                 /* already know p1 < p2 */
      for (;;)
        {
#ifdef MERGE_WTIX
          /* >>> NOTE: see also fheap.c/fheapaux.c, fdbim.c <<< */
#  ifdef WTIX_HUGEUINT_CMP                      /* int-compare optimization*/
          p = (*(EPI_HUGEUINT *)p1->blk < *(EPI_HUGEUINT *)p2->blk ? p1 :
               (*(EPI_HUGEUINT *)p1->blk > *(EPI_HUGEUINT *)p2->blk ? p2 :
                (memcmp(p1->blk, p2->blk, (p1->blksz < p2->blksz ? p1->blksz :
                                           p2->blksz)) < 0 ? p1 : p2)));
#  else /* !WTIX_HUGEUINT_CMP */
          p = (memcmp(p1->blk, p2->blk, (p1->blksz < p2->blksz ? p1->blksz :
                                         p2->blksz)) < 0 ? p1 : p2);
#  endif /* !WTIX_HUGEUINT_CMP */
#else /* !MERGE_WTIX */
          p = (cmp(p1, p2, usr) < 0 ? p1 : p2);
#endif /* !MERGE_WTIX */
        put:
          /* pile_put() may update meter via merge_incdone(): */
          if (!pile_put(out, p)) goto err2;
#ifdef MERGE_METER
          m->doneitems++;                       /* we put an item */
          METER_UPDATEDONE(m->meter, m->doneitems);
#endif /* MERGE_METER */
          switch (pile_get(p))
            {
            case 1:                             /* more data: continue */
              break;
            case 0:                             /* EOF: down to 1 pile */
              if (p == p1)
                {
                  p1 = closepile(p1);
                  p = p2;
                }
              else
                {
                  p2 = closepile(p2);
                  p = p1;
                }
              goto onepile;                     /* continue with other pile */
            default:
            err2:
              goto err;
            }
        }
      break;
    case 1:                                     /* 1-pile merge */
      p = p1 = (PILE *)fheap_top(m->heap);
      deletetop(m->heap);                       /* remove `p' from heap */
    onepile:
      for (;;)
        {
          /* pile_put() may update meter via merge_incdone(): */
          if (!pile_put(out, p)) goto err;
#ifdef MERGE_METER
          m->doneitems++;                       /* we put an item */
          METER_UPDATEDONE(m->meter, m->doneitems);
#endif /* MERGE_METER */
          switch (pile_get(p))
            {
            case 1:                             /* more data: continue */
              break;
            case 0:                             /* EOF: all done */
              goto passDone;
            default:
              goto err;
            }
        }
      break;
    }

passDone:
  /* Bug 7019: pile_put() might be manually merging items not in our
   * piles/heap (i.e. original index data during update index);
   * might still have some of that data left to merge.  Let it finish,
   * and finish here so that it can update the meter (via merge_incdone()):
   */
  if (out->funcs && out->funcs->mergeFinished && !pile_mergeFinished(out))
    goto err;

#ifdef MERGE_METER
  /* Used to always flush to `totalitems + 1' here, but that was to
   * cover OBOBs that should have been fixed by Bug 7019 fixes.
   * One other legit case (`totalitems' 0) handled by meter_end() now:
   */
  meter_end(m->meter);
#endif /* MERGE_METER */

  m->totalitems = (EPI_HUGEINT)0;
  m->heap = closefheap(m->heap);                /* reopen heap to unfrag mem */
  if (cmp == MERGECMP_WTIX)
    m->heap = TXfheapOpen(FHCMPPN, TXfheapInsertWtix, TXfheapDeleteTopWtix,
                          m, 0);
  else
    m->heap = openfheap((FHCMP *)cmp, m, 0);
  if (m->heap == FHEAPPN) goto err;
  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  p1 = closepile(p1);
  p2 = closepile(p2);
  return(ret);
}
