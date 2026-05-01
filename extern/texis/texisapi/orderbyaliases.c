/* orderbyaliases.c — pre-prep pass that rewrites SELECT-list aliases
 * referenced in ORDER BY clauses into the equivalent positional form.
 *
 * Background:
 *
 *   The parser accepts `ORDER BY <alias>` syntactically, but at eval
 *   time the resolver only consults the FROM table's columns — not the
 *   SELECT list — and the query silently returns 0 rows with a
 *   "Field non-existent" message in errMsg.  Meanwhile `ORDER BY <pos>`
 *   (e.g. `ORDER BY 1`) works because it goes through the ORDERNUM_OP /
 *   numtrans path which consults the prepared projection's data
 *   dictionary.
 *
 *   This pass runs after the parser has built the QNODE tree but
 *   before TXpreparetree sees it.  For each ORDER_OP node:
 *     1. Walk the right subtree (the inner SELECT) collecting aliases
 *        from RENAME_OP nodes, with their 1-based position in the
 *        projection list.
 *     2. Walk the left subtree (the ORDER BY list) and rewrite each
 *        NAME_OP whose tname matches an alias — convert it in place to
 *        a NAMENUM_OP with the matching position.
 *     3. After step 2, if every order-by item is now NAMENUM_OP, change
 *        the parent's op from ORDER_OP to ORDERNUM_OP so existing
 *        downstream code (preptree.c:numtrans) routes through the
 *        already-working positional path.
 *
 *   The output tree is byte-identical to one the parser would have
 *   produced for the equivalent positional query, so no downstream
 *   code path needs to know this rewrite happened.
 *
 *   Mixed cases (some order-by items are aliases, others are real
 *   column names) intentionally do NOT trigger the ORDER_OP →
 *   ORDERNUM_OP conversion — the surrounding ORDER_OP path is left to
 *   handle real columns as before, and the alias items are left as
 *   NAME_OP (current behavior, fail at eval).  Documented as a v1
 *   limitation; safer than half-translating the tree.
 *
 *   Subqueries are handled naturally: each ORDER_OP's "right" subtree
 *   is its own SELECT scope, so alias resolution is per-statement.
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"

/* ------------------------------------------------------------------ */
/* alias map: small fixed-size, since SELECT lists are typically <= 50. */

#define VEC_ALIAS_MAX  256

typedef struct {
    const char *names[VEC_ALIAS_MAX];   /* alias name string (borrowed) */
    int         count;                  /* number of entries */
    int         next_pos;               /* next 1-based position to assign */
} alias_map_t;

static void alias_map_init(alias_map_t *m)
{
    m->count = 0;
    m->next_pos = 1;
}

/* Returns 1-based position of `name`, or 0 if not present. */
static int alias_map_lookup(const alias_map_t *m, const char *name)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < m->count; i++) {
        if (m->names[i] && !strcmp(m->names[i], name))
            return i + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Walk the projection list of a SELECT subtree, collecting aliases.
 *
 * The projection is shaped as a tree of items: typically a LIST_OP
 * whose left/right are sub-projections, or a single item.  Items at
 * the leaves are either bare expressions or RENAME_OP nodes whose
 * tname is the alias.  We do an in-order traversal so position
 * numbering matches what the parser would emit for `ORDER BY <pos>`.
 */
static void
collect_aliases(QNODE *projection, alias_map_t *map)
{
    if (!projection) return;
    if (map->count >= VEC_ALIAS_MAX) return;

    switch (projection->op) {
    case LIST_OP:
        /* In-order: left then right, so positions ascend. */
        collect_aliases(projection->left, map);
        collect_aliases(projection->right, map);
        return;
    case RENAME_OP:
        /* tname is the alias name.  Even if the alias is empty/null,
         * still bump the position counter so subsequent positions are
         * correct. */
        if (projection->tname && map->count < VEC_ALIAS_MAX) {
            map->names[map->count++] = (const char *)projection->tname;
        } else if (map->count < VEC_ALIAS_MAX) {
            map->names[map->count++] = NULL;
        }
        map->next_pos++;
        return;
    default:
        /* Bare expression or column reference — counts as a position
         * but has no alias.  Record NULL as a placeholder so positions
         * stay aligned. */
        if (map->count < VEC_ALIAS_MAX)
            map->names[map->count++] = NULL;
        map->next_pos++;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Walk the ORDER BY list counting items that would and would NOT be
 * rewritten as aliases.  Pure inspection — does not mutate the tree.
 *
 * Sets:
 *   *aliases  — number of NAME_OP nodes whose tname matches an alias
 *   *non_alias — number of NAME_OP / other nodes that don't match an alias
 *
 * Already-NAMENUM_OP nodes (explicit positional) count toward neither —
 * they're preserved as-is and don't need rewriting.
 */
static void
count_orderby_leaves(QNODE *node, const alias_map_t *map,
                     int *aliases, int *non_alias)
{
    if (!node) return;
    switch (node->op) {
    case LIST_OP:
        count_orderby_leaves(node->left,  map, aliases, non_alias);
        count_orderby_leaves(node->right, map, aliases, non_alias);
        return;
#ifdef TX_USE_ORDERING_SPEC_NODE
    case ORDERING_SPEC_OP:
        count_orderby_leaves(node->left, map, aliases, non_alias);
        return;
#endif
    case NAMENUM_OP:
        return;   /* already positional, neither category */
    case NAME_OP:
        if (node->tname && alias_map_lookup(map, (const char *)node->tname) > 0)
            (*aliases)++;
        else
            (*non_alias)++;
        return;
    default:
        (*non_alias)++;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Apply the rewrite.  Caller guarantees count_orderby_leaves reported
 * non_alias == 0 (i.e. every NAME_OP is a known alias) so this can't
 * leave the tree in a partially-rewritten state.
 *
 * Returns 0 on success, -1 on alloc error.
 */
static int
apply_orderby_rewrite(QNODE *node, const alias_map_t *map)
{
    if (!node) return 0;
    switch (node->op) {
    case LIST_OP:
        if (apply_orderby_rewrite(node->left,  map) < 0) return -1;
        if (apply_orderby_rewrite(node->right, map) < 0) return -1;
        return 0;
#ifdef TX_USE_ORDERING_SPEC_NODE
    case ORDERING_SPEC_OP:
        return apply_orderby_rewrite(node->left, map);
#endif
    case NAMENUM_OP:
        return 0;
    case NAME_OP: {
        int pos;
        char buf[24];
        char *newName;
        pos = alias_map_lookup(map, (const char *)node->tname);
        /* Caller guarantees pos > 0; defensive check anyway. */
        if (pos <= 0) return 0;
        snprintf(buf, sizeof(buf), "%d", pos);
        newName = TXstrdup(NULL, __FUNCTION__, buf);
        if (!newName) return -1;
        node->tname = TXfree(node->tname);
        node->tname = newName;
        node->op = NAMENUM_OP;
        return 0;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Top-level recursive walker. */

/* Try to rewrite aliases at one ORDER_OP under the given PROJECT_OP.
 * The shape must be PROJECT_OP whose left child is ORDER_OP.  Returns
 * 0 on success (rewrite applied or skipped — both fine), -1 on alloc
 * error.  Leaves trees we don't own untouched.
 */
static int
try_rewrite_at(QNODE *project_node)
{
    alias_map_t map;
    QNODE *order_node;
    int aliases = 0, non_alias = 0;

    if (!project_node)                                return 0;
    if (project_node->op != PROJECT_OP)               return 0;
    if (!project_node->left || !project_node->right)  return 0;
    if (project_node->left->op != ORDER_OP)           return 0;
    if (!project_node->left->left)                    return 0;

    order_node = project_node->left;

    alias_map_init(&map);
    collect_aliases(project_node->right, &map);
    if (map.count == 0) return 0;

    count_orderby_leaves(order_node->left, &map, &aliases, &non_alias);
    if (aliases == 0 || non_alias != 0) return 0;

    if (apply_orderby_rewrite(order_node->left, &map) < 0)
        return -1;
    order_node->op = ORDERNUM_OP;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public entry point.  Called from prepare.c BEFORE TXreorgqnode so
 * that the reorg pass sees ORDERNUM_OP (post-rewrite) and runs its
 * full path.  Calling after reorgqnode leaves the tree half-reorged.
 *
 * v1 scope: we only act on the top-level shape PROJECT_OP whose left
 * child is ORDER_OP (the canonical `SELECT ... ORDER BY ...` tree).
 *
 * Forms NOT handled in v1 (current behavior preserved):
 *   - INSERT INTO ... SELECT ... ORDER BY <alias>
 *   - Subqueries with their own ORDER BY <alias>
 *   - UNION ... ORDER BY <alias>
 *   - CREATE TABLE AS SELECT ... ORDER BY <alias>
 *
 * Each of those has a different outer wrapper shape and trying to
 * descend into them risks dereferencing non-QNODE pointers in some
 * ops (TABLE_OP holds a DD, etc.) — handled in a follow-up.
 *
 * Returns 0 on success, -1 on hard error.
 */
int
TXrewriteOrderByAliases(QNODE *root)
{
    if (!root) return 0;

    /* The canonical top-level case: PROJECT_OP wrapping ORDER_OP. */
    if (try_rewrite_at(root) < 0) return -1;

    /* CREATE TABLE ... AS SELECT ... ORDER BY <alias>: the tree has
     * TABLE_AS_OP at the top, with left = PROJECT_OP wrapping the
     * inner SELECT.  TABLE_AS_OP's right is the table-creation info
     * (DD pointer, NOT a QNODE), so we must NOT recurse into right. */
    if (root->op == TABLE_AS_OP && root->left) {
        if (try_rewrite_at(root->left) < 0) return -1;
    }

    return 0;
}
