typedef struct tagUSAGE
{
	int	num;
	char	*usage;
} USAGE;

static USAGE use[] = {
{0, "ALTER TABLE table-name\n\tADD column-name data-type"},
{1, "ALTER TABLE table-name\n\tMODIFY old-column-name new-data-type"},
{2, "ALTER TABLE table-name\n\t{ADD column-name data-type}\n\t{DELETE column-name}\n\t{RENAME old-column-name new-column-name}\n\t{MODIFY old-column-name new-data-type}"},
{3, "CREATE [UNIQUE | METAMORPH | INVERTED] INDEX index-name\n\tON table-name\n\t(column-name [DESC])"
#ifdef TX_INDEX_OPTIONS
"\n\t[WITH option-name [value] [option-name [value] ...]"
#endif /* TX_INDEX_OPTIONS */
},
{4, "CREATE [FAST] TABLE table-name\n\t(column-name data-type [NOT NULL] \n\t[, column-name data-type [NOT-NULL] ]...)"},
{5, "CREATE VIEW view-name\n\t[(column-name [, column-name]...)]\n\tAS SELECT-statment"},
{6, "DELETE FROM table-name\n\t[WHERE search-condition]"},
{7, "DROP INDEX index-name"},
{8, "DROP TABLE table-name"
#ifdef TX_SQL_IF_EXISTS
" [IF EXISTS]"
#endif /* TX_SQL_IF_EXISTS */
},
{9, "DROP VIEW view-name"},
{10, "GRANT {ALL} {privilege [,privilege]...}\n\tON {table-name} {view-name}\n\tTO {PUBLIC} {userid}\n\t[WITH GRANT OPTION]"},
{11, "INSERT INTO table-name [(column-name [,column-name]...)]\n\tVALUES(value, [,value]...)"},
{12, "INSERT INTO table-name [(column-name [,column-name]...)]\n\tSELECT-statement"},
{13, "REVOKE {ALL} {privilege [,privilege]...}\n\tON {table-name} {view-name}\n\tTO {PUBLIC} {userid}"},
{14, "SELECT [DISTINCT] {expression [,expression]...} {*}\n\tFROM table-name [alias] [,table-name [alias]]...\n\t[WHERE search-condition]\n\t[GROUP BY column-name [,column-name]...\n\t\t[HAVING search-condition]]\n\t[ORDER BY column-name [DESC] [,column-name [DESC]]...]"},
{15, "UPDATE table-name\n\tSET column-name = expression\n\t\t[,column-name = expression]...\n\t[WHERE search-condition]"},
{16, "CREATE TRIGGER trigger-name\n\tBEFORE | AFTER | INSTEAD OF\n\tINSERT | UPDATE | DELETE\n\tON table-name\n\t[ORDER order-value]\n\t[REFERENCING old-or-new-values]\n\tFOR EACH {ROW | STATEMENT}\n\tsql-statment | SHELL 'command'"},
{17, "SET property = value\n\n\tKnown properties:\n\taddexp\t\tAdd index expressions.\n\tdelexp\t\tDelete index expression\n\tlstexp\t\tList index expressions\n\tindirectcompat\tIndirect display\n\ttablespace\tWhere to put tables\n\tindexspace\tWhere to put indices\n\n\tminwordlen\n\tprefixproc\n\tsuffixproc\n\trebuild\n\tuseequiv"},
{18, "ALTER INDEX {index-name | ALL} [ON table-name]\n\tREBUILD\n\t| OPTIMIZE"},
};

