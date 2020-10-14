var Sql=require("rampart-sql");
var sql=new Sql.init("./wikidb",true); /* true means make db if it doesn't exist */

/*
  This statement creates the full text index on the Doc field.
 
  WITH WORDEXPRESSIONS:
  see: https://docs.thunderstone.com/site/texisman/index_options.html
  and  https://docs.thunderstone.com/site/texisman/creating_a_metamorph_index.html
  see also "addexp", which is the same as "WITH WORDEXPRESSIONS" 
    but in a separate statement: https://docs.thunderstone.com/site/texisman/indexing_properties.html
  
  the regular expressions used to define a word are not perlRE.  It is thunderstone's own rex:
  https://docs.thunderstone.com/site/texisman/rex_expression_syntax.html
  
  "metamorph inverted index" can also be replaced with "FULLTEXT"
  see: https://docs.thunderstone.com/site/vortexman/create_index_with_options.html
  
  INDEXMETER prints the progress of the index creation.
  
*/

sql.exec(
  "create metamorph inverted index wikitext_Doc_mmix on wikitext(Doc) "+
  "WITH WORDEXPRESSIONS "+
  "('[\\alnum\\x80-\\xFF]{2,99}', '[\\alnum\\$\\%\\@\\-\\_\\+]{2,99}') "+
  "INDEXMETER 'on'");
