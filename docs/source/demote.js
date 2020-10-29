var Sql=require("rampart-sql");

rampart.globalize(Sql);
rampart.globalize(rampart.utils);
var file = process.argv[2];

if(!stat(file,true))
{
    printf("can't find %s\n",file);
    process.exit(1);
}


var text=readFile(file);

var lines=rex(/[^\n]*\n=/, text);
var lastline="";

for (var i=0;i<lines.length;i++)
{
    var line=lines[i];
    var addnl=0;
    if( rex(/>>=\=+\n=/,line).length )
    {
        line=sandr(/\==/,'-',line);
        addnl=1;
    } 
    else if ( rex(/>>=\-+\n=/,line).length ) 
    {
        line=sandr(/\-=/,'~',line);
        addnl=1;
    }
    else if ( rex(/>>=\~+\n=/,line).length ) 
    {
        line=sandr(/\~=/,'"',line);
        addnl=1;
    }
    else if ( rex(/>>="+\n=/,line).length ) 
    {
        line=sandr(/"=/,"'",line);
        addnl=1;
    }
    // these are misplaced
    else if ( rex(/>>=\^+\n=/,line).length )
    {
        line=sandr(/\^=/,'"',line);
        addnl=1;
    }
    if(addnl) printf("\n");
    printf('%s',lastline );
    lastline=line
}

printf('%s',lastline );

