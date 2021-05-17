var Sql=require("rampart-sql");

var db=process.scriptPath + '/data/docs/db';

var sql=new Sql.init(db);

rampart.globalize(rampart.utils);

sql.set({
    "likepleadbias":1000,
    keepNoise: true
});

function corpsug(word)
{
    var res;
    sql.set({"indexaccess":true});
    res=sql.exec(
        "select Word value, convert('search','varchar') data from sections_full_text_mmix where Word matches ? order by Count DESC",
        [word+'%']
    );
    return res.results;
}

function suggest(req)
{
    var q = req.query.query;
    if(!q) return {"suggestions":[]};
    var space = q.lastIndexOf(" ");
    if(space == -1)
    {
        var res = sql.exec("select full value, plink data from sections where title matches ? order by length(title)",[q+'%'])
        if (res.rowCount < 10)
        {
            var cwords = corpsug(q);
            for (var i=0;i<cwords.length; i++)
                res.results.push(cwords[i]);
        }
        return { 
            json: { "suggestions": res.results}
        }
    }
    else 
    {
        var pref = q.substring(0,space);
        var word = q.substring(space+1);
        if(!word.length)
            return {json: { "suggestions":  [q] } };
        var cwords = corpsug(word);
        for( var i=0; i<cwords.length; i++)
        {
            var o=cwords[i];
            o.value = pref + " " + o.value;
        }
    }
    return {json: { "suggestions":  cwords} };
}

function results(req)
{
    return { json: 
        sql.exec(
            "select full, plink, html from sections where full\\text likep ?q",
            {q:req.query.q}
        )};
}


module.exports = {
    "suggest.json": suggest,
    "results.json": results
}










