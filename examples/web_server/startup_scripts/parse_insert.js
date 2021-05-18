/* make printf et. al. global */
rampart.globalize(rampart.utils);

/* load the html module */
var html=require("rampart-html");

/* load the sql module */
var Sql = require("rampart-sql");
var sql;

function hparse(file, tofile){
    var res=readFile(file);
    var hres=html.newDocument(res,{"indent":true,wrap:120});
    var body = hres.findTag("body");
    var acscript = body.findAttr("id=rampart-search");

    if(!acscript.length) {
        body.append('<script src="https://cdnjs.cloudflare.com/ajax/libs/jquery.devbridge-autocomplete/1.4.11/jquery.autocomplete.min.js"></script>');
        body.append('<script id="rampart-search" src="_static/client_search.js"></script>');
        printf("saving changes to %s\n",tofile);
        fprintf(tofile, "%s", hres.prettyPrint());
    }

    var h = hres.findTag("html");

    t=hres.findClass("section");
    
    var els=[];

    /* get text from section, excluding subsections */
    function getSectText(el)
    {
        var c=el.children();
        var isSection=c.hasClass("section");
        var isH=c.hasTag(['h1','h2','h3','h4']);
        var ret="";
        var i=0;

        for (i=0; i<c.length; i++)
        {
            if( ! isSection[i] && ! isH[i] ) {
                if(i)
                    ret += "\n" + c.eq(i).toText();
                else
                    ret = c.eq(i).toText();
            }
        }
        return ret;
    }

    /* make temp copy of section and remove subsections */
    function getSectHtml(el)
    {
        body.append("<div id='tempdiv'>");
        var temp = body.findAttr("id=tempdiv");
        temp.append(el);
        el = temp.findClass("section").eq(0);
        el.findClass("section").delete();
        var ret = el.toHtml();
        temp.delete();
        return ret[0];
    }

    for (i=0; i<t.length; i++)
    {
        var e=t.eq(i);

        if     ( e.findTag('h1').length==1 )
            els.push({title: e.findTag('h1').toText()[0], id: e.getAttr("id")[0], level:1, text: getSectText(e), html: getSectHtml(e)});
        else if( e.findTag('h2').length==1 )
            els.push({title: e.findTag('h2').toText()[0], id: e.getAttr("id")[0], level:2, text: getSectText(e), html: getSectHtml(e)});
        else if( e.findTag('h3').length==1 )
            els.push({title: e.findTag('h3').toText()[0], id: e.getAttr("id")[0], level:3, text: getSectText(e), html: getSectHtml(e)});
        else if( e.findTag('h4').length==1 )
            els.push({title: e.findTag('h4').toText()[0], id: e.getAttr("id")[0], level:4, text: getSectText(e), html: getSectHtml(e)});
        
    }

    return els;
}

function copy_files(path, docpath, destpath){
    var i, ret;

    ret = shell(`cp -a ${docpath}/_static ${destpath}/`);

    if(ret.exitStatus)
        throw(ret.stderr);

    ret = shell(`cp -a ${docpath}/_sources ${destpath}/`);
    if(ret.exitStatus)
        throw(ret.stderr);

    rampart.utils.copyFile(
        path + "/data/docs/client_search.js",
        destpath + "/_static/client_search.js",
        true
    );
}

function is_current(docpath, destpath) {
    var files = readdir(docpath + "/").filter(function(dir){ return /\.html/.test(dir); });
    for (var j=0; j<files.length;j++) {
        var ofile = docpath  + "/" + files[j];
        var nfile = destpath + "/" + files[j];
        var ostat = stat(ofile);
        var nstat = stat(nfile);
        if (!nstat || ostat.mtime > nstat.mtime)
            return false;
    }
    return true;
}

function make_database(docpath,destpath) {

    var res = sql.exec("select * from SYSTABLES where NAME = 'sections'");
    if(res.rowCount)
    {
        sql.exec("drop table sections;");
    }

    sql.exec("create table sections (title varchar(16), full varchar(64), plink varchar(32), level int, text varchar(128), html varchar(256) );");

    var files = readdir(docpath + "/").filter(function(dir){ return /\.html/.test(dir); });

    for (var j=0; j<files.length;j++) {
        var file=files[j];
        var els = hparse(docpath + "/"+file, destpath + "/"+file);
        var lastlevel=1;
        var fullpath=[];
        for (i=0; i<els.length; i++)
        {
            var el=els[i];
            var indent=(el.level-1) * 4;
            var title = el.title.replace(/ ¶ /,"").toLowerCase();
            fullpath[el.level-1] = title;
            fullname = fullpath.slice(0,el.level).join(' : ')
            link = file + "#" + title.replace(/[ \.\/]/g,'-').replace(/[\(\)]/g,'');

            //printf("SECTION %s, level:%d, length %d\n", link, el.level, el.text.length);
            //printf("%*P\n%!*P\n", indent, title, indent+2, el.text);

            sql.exec(
                "insert into sections values(?, ?, ?, ?, ?, ?);",
                [title, fullname, link, el.level, el.text, el.html]
            );
        }
    }
}

function make_index() {
    sql.set({
        keepNoise: true
    });
    sql.exec("create index sections_title_x on sections(title);");
    sql.exec("create fulltext index sections_full_text_mmix on sections(full\\text) " +
             "WITH WORDEXPRESSIONS "+
             "('[\\alnum\\x80-\\xFF]{2,99}', '[\\alnum\\x80-\\xFF\\(\\)\\%\\-\\_]{2,99}') "+
             "INDEXMETER 'on'"
    );
}


function init(path) {
    var path, docpath, destpath, dbpath;

    if(module && module.path)
        path=module.path;
    else
        path=process.scriptPath;

    path = path.replace(/\/[^\/]+$/, "");

    dbpath = path + "/data/docs/db";


    sql = new Sql.init(dbpath,true);

    var trypaths = [
        path + "/../../../docs/build/html", //github source
        path + "/../../docs",               // relative to $RAMPART_PATH
        "/usr/local/rampart/docs"           // standard install dir
    ];

    for (var i = 0; i<trypaths.length; i++) {
        var p = trypaths[i];
        if(stat(p))
        {
            docpath=realPath(p);
            break;
        }
    }
    if(!docpath)
        throw("Couldn't find html documents");
    
    destpath = path + "/html/docs";
    
    if(!stat(destpath))
        mkdir(destpath);

    var ret = shell(`chown -R ${webuser} ${destpath}`); 

    if(ret.exitStatus)
        throw(ret.stderr);

    if(is_current(docpath, destpath))
    {
        console.log("docs are up to date");
        return;
    }

    copy_files(path, docpath, destpath);

    make_database(docpath, destpath);
    
    make_index();

    ret = shell(`chown -R ${webuser} ${destpath}`); 

    if(ret.exitStatus)
        throw(ret.stderr);

    ret = shell(`chown -R ${webuser} ${dbpath}`); 

    if(ret.exitStatus)
        throw(ret.stderr);

}

if(module && module.exports)
    module.exports = init;
else
    init();
