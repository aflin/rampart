var Sql=require("rampart-sql");

/* As a server module, find the database relative to this script
   (apps/docs/ -> data/docs/db), as build_db() does from the command line.
   process.scriptPath is the directory of the server's config, which need not
   be the directory that holds apps/ and data/.                          */
var db = (module && module.path ? module.path + '/../..' : process.scriptPath) + '/data/docs/db';

var sql;

/* This is a dual purpose script.  
   If run from command line, it will build the database and html pages.
   If run as a module, it will perform the typeahead and search functions.
*/

rampart.globalize(rampart.utils);

/**************** functions for ajax search ********************/

// connect to db as normal if module
if(module && module.exports)
{
    sql=new Sql.init(db);

    sql.set({
        keepNoise: true
    });
}

/* suggestions based on dictionary words from the corpus of all docs */
function corpsug(word)
{
    var res;
    sql.set({"indexaccess":true});//allow the search of metamorph word index like a table
    /* Return array of objects formatted as 
        [
            { 
                value: "matchedword1", 
                data: "search" 
            },
            { 
                value: "matchedword2", 
                data: "search" 
            },
            ...
        ]
    ordered by word count.  data: "search" -- indicate that a search should be performed upon clicking.
    See client javascript below in copy_files().
    */
    res=sql.exec(
        "select Word value, convert('search','varchar') data from sections_full_text_mmix where Word matches ? order by Count DESC",
        [word.toLowerCase()+'%']
    );
    return res.rows;
}

function suggest(req)
{
    var q = req.query.query;
    var cwords;
    if(!q) return {"suggestions":[]};
    var space = q.lastIndexOf(" ");
    if(space == -1)
    {
        // look for suggestions as an exact title match
        var res = sql.exec("select full value, plink data from sections where title matches ? order by length(title)",[q+'%'])
        // if less than 10, add word matches
        if (res.rowCount < 10)
        {
            cwords = corpsug(q);
            for (var i=0;i<cwords.length; i++)
                res.rows.push(cwords[i]);
        }
        return { 
            json: { "suggestions": res.rows}
        }
    }
    else 
    {
        // get suggestions for the partial word after the last space
        var pref = q.substring(0,space);
        var word = q.substring(space+1);
        if(!word.length)
            return {json: { "suggestions":  [q] } };
        cwords = corpsug(word);
        for( var i=0; i<cwords.length; i++)
        {
            var o=cwords[i];
            o.value = pref + " " + o.value;
        }
    }
    return {json: { "suggestions":  cwords} };
}

/* return entire sections' html that match query */
function results(req)
{
    return { json: 
        sql.exec(
            "select full, plink, html from sections where full\\text likep ?q",
            {q:req.query.q}
        )};
}

/**************** functions to build the docs search ***********/


/* load the html module */
var html=require("rampart-html");

// break up a page into <section>s
function parse_html_file(file, tofile){
    var res=readFile(file);
    var hres=html.newDocument(res,{"indent":true,wrap:120});
    var body = hres.findTag("body");
    var head = hres.findTag("head");
    /* check if we already altered this file */
    var acscript = body.findAttr("id=rampart-search");

    /* remove source link */
    hres.findClass('wy-breadcrumbs-aside').delete();

    if(!acscript.length) {
        /* some alterations: jquery-autocomplete and the client-side search functions */
        body.append('<script src="https://cdnjs.cloudflare.com/ajax/libs/jquery.devbridge-autocomplete/1.4.11/jquery.autocomplete.min.js"></script>');
        body.append('<script id="rampart-search" src="_static/client_search.js"></script>');
        head.append(`
            <script>(function(){var c=document.cookie.match(/(?:^|; )theme=([^;]*)/);if(c&&c[1]==='dark')document.documentElement.setAttribute('data-theme','dark');})()</script>
            <link rel="preconnect" href="https://fonts.gstatic.com">
            <link href="https://fonts.googleapis.com/css2?family=Varela+Round&family=Fira+Code:wght@400;600&display=swap" rel="stylesheet">
            <style>
            /* ---- Light theme (default) to match rampart.dev site ---- */
            :root {
              --rp-dark: #e2e8f0;
              --rp-darker: #f8fafc;
              --rp-accent: #16a34a;
              --rp-accent-dim: #15803d;
              --rp-slate: #e2e8f0;
              --rp-slate-light: #cbd5e1;
              --rp-text: #1e293b;
              --rp-text-dim: #475569;
              --rp-card-heading: #0f172a;
              --rp-shadow: rgba(0,0,0,0.08);
            }

            [data-theme="dark"] {
              --rp-dark: #0f172a;
              --rp-darker: #020617;
              --rp-accent: #22c55e;
              --rp-accent-dim: #16a34a;
              --rp-slate: #1e293b;
              --rp-slate-light: #334155;
              --rp-text: #e2e8f0;
              --rp-text-dim: #94a3b8;
              --rp-card-heading: #fff;
              --rp-shadow: rgba(0,0,0,0.3);
            }

            body, .wy-body-for-nav { background: var(--rp-darker) !important; color: var(--rp-text); }
            body, h1, h2, h3, h4, h5, h6 { font-family: "Varela Round", sans-serif; color: var(--rp-text); }
            h1 { color: var(--rp-card-heading); }
            dl:not(.docutils) dt { font-family: "Varela Round", sans-serif !important; font-size: 90% !important; background: var(--rp-slate) !important; color: var(--rp-text) !important; border-top: 3px solid var(--rp-accent) !important; }
            p, li, dd, td, th, .rst-content { color: var(--rp-text-dim); }

            /* Sidebar */
            .wy-nav-side { background: var(--rp-dark) !important; }
            .wy-side-nav-search { background: var(--rp-dark) !important; border-bottom: 1px solid var(--rp-slate-light); }
            .wy-side-nav-search input[type=text] { background: var(--rp-slate) !important; border: 1px solid var(--rp-slate-light) !important; color: var(--rp-text) !important; border-radius: 6px; }
            .wy-menu-vertical a { color: var(--rp-text-dim) !important; }
            .wy-menu-vertical a:hover { background: var(--rp-slate) !important; color: var(--rp-card-heading) !important; }
            .wy-menu-vertical li.current > a { background: var(--rp-slate) !important; color: var(--rp-card-heading) !important; border: none !important; }
            .wy-menu-vertical li.current { background: var(--rp-slate) !important; }
            .wy-menu-vertical li.toctree-l2.current > a, .wy-menu-vertical li.toctree-l3.current > a { background: var(--rp-slate-light) !important; }
            .wy-menu-vertical li.toctree-l2.current li.toctree-l3 > a { background: var(--rp-dark) !important; }
            .wy-menu-vertical p.caption { color: var(--rp-accent) !important; }

            /* Mobile nav */
            /* Mobile nav */
            .wy-nav-top { background: var(--rp-dark) !important; border-bottom: 1px solid var(--rp-slate-light); }

            /* Overlay sidebar instead of push */
            @media screen and (max-width: 768px) {
              .wy-nav-side.shift { z-index: 9999; position: fixed; box-shadow: 4px 0 24px rgba(0,0,0,0.5); }
              .wy-nav-content-wrap.shift { position: static !important; left: auto !important; min-width: auto !important; }
            }
            #rp-close-sidebar { display: none; background: none; border: none; cursor: pointer; color: var(--rp-card-heading) !important; font-size: 30px; padding: 0.095em 0.46em; line-height: 50px; float: left; }
            #rp-close-sidebar:hover { color: var(--rp-card-heading) !important; }
            .wy-nav-side.shift #rp-close-sidebar { display: block; }

            /* Main content area */
            .wy-nav-content-wrap { background: var(--rp-darker) !important; }
            .wy-nav-content { max-width: 1600px; background: var(--rp-darker) !important; }

            /* Links */
            a { color: var(--rp-accent) !important; }
            a:hover { color: #15803d !important; }
            [data-theme="dark"] a:hover { color: #4ade80 !important; }
            a.headerlink { color: var(--rp-slate-light) !important; }
            a.headerlink:hover { color: var(--rp-accent) !important; }

            /* Breadcrumbs */
            .wy-breadcrumbs li a { color: var(--rp-accent) !important; }
            .wy-breadcrumbs li { color: var(--rp-text-dim); }
            .wy-breadcrumbs { border-bottom: 1px solid var(--rp-slate-light); }

            /* Code blocks */
            .highlight { background: var(--rp-dark) !important; border: 1px solid var(--rp-slate-light); border-radius: 6px; }
            .highlight pre { color: var(--rp-text); }
            code, .rst-content tt, .rst-content code { background: var(--rp-slate) !important; color: var(--rp-text) !important; border: 1px solid var(--rp-slate-light) !important; }
            .rst-content pre.literal-block, .rst-content div[class^="highlight"] pre { background: var(--rp-dark) !important; color: var(--rp-text); }

            /* Tables */
            .wy-table thead, .rst-content table.docutils thead { background: var(--rp-slate) !important; }
            .wy-table thead th, .rst-content table.docutils thead th { color: var(--rp-card-heading); border-bottom: 2px solid var(--rp-accent) !important; }
            .wy-table td, .rst-content table.docutils td, .wy-table th, .rst-content table.docutils th { border: 1px solid var(--rp-slate-light) !important; background: transparent !important; }
            .wy-table-responsive table tr:nth-child(2n-1) td { background: var(--rp-slate) !important; }

            /* Admonitions / notes / warnings */
            .rst-content .note, .rst-content .seealso { background: var(--rp-slate) !important; }
            .rst-content .note .admonition-title, .rst-content .seealso .admonition-title { background: var(--rp-slate-light) !important; color: var(--rp-card-heading); }
            .rst-content .warning { background: rgba(234,97,83,0.1) !important; }
            .rst-content .warning .admonition-title { background: #E74C3C !important; }
            .rst-content .hint, .rst-content .tip, .rst-content .important { background: rgba(34,197,94,0.1) !important; }
            .rst-content .hint .admonition-title, .rst-content .tip .admonition-title, .rst-content .important .admonition-title { background: var(--rp-accent-dim) !important; }
            .rst-content .attention, .rst-content .caution { background: rgba(230,126,34,0.1) !important; }
            .rst-content .attention .admonition-title, .rst-content .caution .admonition-title { background: #E67E22 !important; }
            .rst-content .admonition-todo { background: var(--rp-slate) !important; }
            .rst-content .admonition-todo .admonition-title { background: var(--rp-slate-light) !important; }

            /* Footer / versions */
            footer { background: var(--rp-dark) !important; border-top: 1px solid var(--rp-slate-light); }
            footer p { color: var(--rp-text-dim); }
            footer hr { margin: 5px; }
            .rst-footer-buttons a { color: var(--rp-card-heading) !important; background: var(--rp-slate) !important;  margin: 5px 5px 0 5px;}
            .rst-footer-buttons a:hover { background: var(--rp-accent) !important; }
            .rst-versions { background: var(--rp-dark) !important; color: var(--rp-text-dim); }

            /* Scrollbar */
            .wy-side-scroll::-webkit-scrollbar { width: 6px; }
            .wy-side-scroll::-webkit-scrollbar-track { background: var(--rp-dark); }
            .wy-side-scroll::-webkit-scrollbar-thumb { background: var(--rp-slate-light); border-radius: 3px; }

            /* HR */
            hr { border-color: var(--rp-slate-light); }

            /* Theme toggle */
            #rp-theme-toggle { background: none; border: 1px solid var(--rp-slate-light); border-radius: 6px; padding: 4px 8px; cursor: pointer; color: var(--rp-text-dim); line-height: 1; display: inline-flex; align-items: center; margin-left: 8px; vertical-align: middle; }
            #rp-theme-toggle:hover { border-color: var(--rp-accent); color: var(--rp-accent); }
            #rp-theme-toggle svg { width: 16px; height: 16px; }
            #rp-theme-toggle .icon-moon { display: none; }
            [data-theme="dark"] #rp-theme-toggle .icon-moon { display: inline; }
            [data-theme="dark"] #rp-theme-toggle .icon-sun { display: none; }

            /* Autocomplete */
            .autocomplete-suggestions { border: 1px solid var(--rp-slate-light); background: var(--rp-slate); overflow: auto; width: auto !important; padding-right:5px; border-radius: 6px; box-shadow: 0 8px 24px var(--rp-shadow); }
            .autocomplete-suggestion { padding: 6px 10px; white-space: nowrap; overflow: hidden; color: var(--rp-text-dim); }
            .autocomplete-selected { background: var(--rp-slate-light); color: var(--rp-card-heading); }
            .autocomplete-suggestions strong { font-weight: normal; color: var(--rp-accent); }
            .autocomplete-group { padding: 2px 5px; }
            .autocomplete-group strong { display: block; border-bottom: 1px solid var(--rp-slate-light); }

            /* Search results */
            .searchsect { max-height: 250px; overflow:hidden; padding-left:5px; padding-bottom: 15px; position: relative; }
            .linked { font-size: 1.1em; display: inline-block; position: relative; margin-top: 20px; }
            .slink { font-size: 1rem; margin-top:0px; margin-bottom:5px; }
            .fader { position: absolute; bottom: 0px; height: 2em; left:-5px; right:0px; background: linear-gradient(transparent, var(--rp-darker)); }
            .expand { cursor:pointer; background: var(--rp-slate); color: var(--rp-accent); padding: 3px 8px; font-family: 'Fira Code', monospace; font-size: 0.8rem; border-radius: 4px; position: absolute; right: 15px; }
            .expand:hover { background: var(--rp-slate-light); }
            .expandBottom { bottom: 18px }
            .expandTop { top: 3px }
            .expanded .fader { bottom: -15px }
            .expanded .expandBottom { bottom: 38px; }

            /* Misc */
            .wy-side-nav-search > a.icon { color: var(--rp-card-heading) !important; display: inline-block !important; margin-bottom: 0 !important; vertical-align: middle; }
            .wy-side-nav-search div[role="search"] { margin-top: 10px; }
            .wy-side-nav-search > #rp-theme-toggle { vertical-align: middle; }
            .rst-content .section ul li { color: var(--rp-text-dim); }
            </style>`
        );
        /* Insert theme toggle right after the "Rampart" link in sidebar, on same line */
        body.findClass('icon-home').after(`<button type="button" id="rp-theme-toggle" title="Toggle light/dark theme"><svg class="icon-moon" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/></svg><svg class="icon-sun" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/></svg></button>`);
        body.findClass('icon-home').attr('href','/');
        /* Add close button at top of sidebar for mobile overlay */
        body.findClass('wy-side-scroll').prepend(`<button type="button" id="rp-close-sidebar"><span class="fa fa-bars">\u00a0</span></button>`);
        body.findClass('wy-breadcrumbs').prepend('<li><a href="/">Home</a> » ');

        /* FAQ accordion: collapse answers, show only questions */
        if(file.indexOf('faq.html') !== -1) {
            var faqsections = hres.findTag("section").filterAttr("id");
            if(!faqsections.length)
                faqsections = hres.findClass("section");
            for(var fi = 0; fi < faqsections.length; fi++) {
                var fel = faqsections.eq(fi);
                var fh3 = fel.findTag("h3");
                /* skip sections that also contain an h2 (category headings
                   that nest a single question inside) */
                if(fh3.length !== 1 || fel.findTag("h2").length) continue;
                fel.addClass("faq-item");
                fh3.addClass("faq-question");
            }
            head.append(`
<style>
.faq-question { cursor:pointer; position:relative; padding-right:30px !important; user-select:none; -webkit-user-select:none; transition:color 0.2s; font-size:1rem !important; font-weight:600; }
.faq-question:hover { color:var(--rp-accent) !important; }
.faq-item { margin-bottom:0.3em !important; padding-bottom:0 !important; }
.faq-question::after { content:'\\25B8'; position:absolute; right:0; top:50%; transform:translateY(-50%); font-size:1.2em; color:#808080; transition:transform 0.2s; }
.faq-item.open .faq-question::after { content:'\\25BE'; }
.faq-answer { display:none; padding-top:0.5em; }
.faq-item.open .faq-answer { display:block; }
.faq-controls { margin-bottom:1em; font-size:0.85rem; }
.faq-controls a { cursor:pointer; margin-right:1em; }
</style>`);
            body.append(`
<script>
(function(){
    var items=document.querySelectorAll('.faq-item');
    for(var i=0;i<items.length;i++){
        var item=items[i],h3=item.querySelector('.faq-question');
        if(!h3)continue;
        var answer=document.createElement('div');
        answer.className='faq-answer';
        var next=h3.nextSibling;
        while(next){var move=next;next=next.nextSibling;answer.appendChild(move);}
        item.appendChild(answer);
        h3.addEventListener('click',function(e){
            if(e.target.classList.contains('headerlink'))return;
            this.parentElement.classList.toggle('open');
        });
    }
    var h1=document.querySelector('h1');
    if(h1){
        var c=document.createElement('div');c.className='faq-controls';
        c.innerHTML='<a id="faq-expand-all">Expand all</a><a id="faq-collapse-all">Collapse all</a>';
        h1.parentElement.insertBefore(c,h1.nextSibling);
        document.getElementById('faq-expand-all').addEventListener('click',function(){
            var it=document.querySelectorAll('.faq-item');for(var j=0;j<it.length;j++)it[j].classList.add('open');
        });
        document.getElementById('faq-collapse-all').addEventListener('click',function(){
            var it=document.querySelectorAll('.faq-item');for(var j=0;j<it.length;j++)it[j].classList.remove('open');
        });
    }
    function openFromHash(){
        var all=document.querySelectorAll('.faq-item');
        for(var j=0;j<all.length;j++) all[j].classList.remove('open');
        if(window.location.hash){
            var t=document.querySelector(window.location.hash);
            if(t){var it=t.closest('.faq-item');if(it)it.classList.add('open');}
        }
    }
    openFromHash();
    window.addEventListener('hashchange', openFromHash);
})();
</script>`);
        }

        printf("saving changes to %s\n",tofile);
        fprintf(tofile, "%s", hres.prettyPrint());
    }

    var sections=hres.findClass("section");
    var altsection=0;
    /* other versions of this theme replace '<div class="section"' with '<section id=' */
    if(!sections.length) {
        sections=hres.findTag("section").filterAttr('id');
        altsection=1;
    }   
    var els=[];

    function getSect(level, el)
    {
        var hlev = `h${level}`;
        //add a temp div so we can manipulate the contents
        body.append("<div id='tempdiv'>");
        var temp = body.findAttr("id=tempdiv");
        //copy our current section to temp div
        temp.append(el);

        if(altsection){
            //get the copied section
            var elcpy = temp.findTag("section").eq(0);
            //remove subsections in the copy
            elcpy.findTag("section").delete();
        } else {
            //get the copied section
            var elcpy = temp.findClass("section").eq(0);
            //remove subsections in the copy
            elcpy.findClass("section").delete();
        }
        //remove the temp div
        temp.delete();
        return {
            title: el.findTag(hlev).toText()[0],
            id:    el.getAttr("id")[0],
            level: level,
            text: elcpy.toText()[0],
            html: elcpy.toHtml()[0]
        }
    }

    for (i=0; i<sections.length; i++)
    {
        var el=sections.eq(i);

        /* h5/h6 sections used to match no branch and were dropped
           silently, keeping their text out of the search index. */
        for (var lev=1; lev<=6; lev++)
        {
            if( el.findTag('h'+lev).length==1 )
            {
                els.push(getSect(lev,el));
                break;
            }
        }
    }

    return els;
}

/* copy source/text files and static files and create client_search.js */

function copy_files(path, docpath, destpath){
    var i, ret;

    ret = shell(`cp -a ${docpath}/_static ${destpath}/`);

    if(ret.exitStatus)
        throw(ret.stderr);

    ret = shell(`cp -a ${docpath}/_sources ${destpath}/`);
    if(ret.exitStatus)
        throw(ret.stderr);

    fprintf(destpath + "/_static/client_search.js", '%s',
`$(document).ready(function(){
    var query = document.location.search;
    var params = new URLSearchParams(query);
    var q = params.get("q");

    if(q) {
        window.history.pushState({}, "", window.location.href.replace(/\\?.*/,''));
        $('#rtd-search-form').find('input[type=text]').val(q);
        dosearch(q);
    }

    function dosearch(q)
    {
        $("#searchres").remove();
        $("div[itemprop=articleBody]").after('<div id="searchres">');
        $("div[itemprop=articleBody],.rst-footer-buttons").hide();
        var sdiv = $('#searchres');
        sdiv.html('<div style="cursor: pointer; float:right; border: 1px solid #334155; border-radius:4px; position: relative; padding: 2px 8px; color:#22c55e; font-size:0.9rem;" id="sclose">Close Search Results</div>');
        $.getJSON(
            "/apps/docs/rsearch/results.json",
            {q:q},
            function(res)
            {
                var r = res.rows;
                sdiv.append("<h3>Search Results:</h3><p><p>");
                for (var i=0;i<r.length;i++)
                {
                    sdiv.append(
                        '<div data-base="'+ r[i].plink.replace(/#.*/,'')  +'" class="searchsect">'+
                          r[i].html+
                          '<div class="expand expandTop">show less</div>' +
                          '<div class="fader">' +
                            '<div class="cont" style="bottom:3px; left:15px; position: relative;">...</div>' +
                            '<hr style="margin: 2px 0; border-color: #334155;">'+
                            '<div class="expand expandBottom">show more</div>' +
                          '</div>' +
                         '</div>');
                }
                $('.expandTop').hide();
                if(r.length==0)
                {
                    sdiv.append('<p>No Results for "'+q+'"</p>');
                }
                else
                {
                    var hlinks = sdiv.find("a.headerlink");
                    var i=0;
                    for (;i<hlinks.length;i++) {
                        var hx = hlinks.eq(i).parent();
                        hlinks.eq(i).remove();
                        var txt = hx.text();
                        hx.empty().append('<span>');
                        var hspan = hx.find('span');
                        hspan.unwrap();
                        hspan.html('<a class="linked mlink" href="/docs/' + r[i].plink +'">' + txt + '</a>'+
                                    '<br><span><a class="linked slink" href="/docs/' +
                                 r[i].plink+ '">' + r[i].plink.replace(/.html#.*/,'') + ' : ' +
                                 r[i].full+ '</a>');

                    }
                    $(".searchsect").each(function(){
                        var t=$(this);
                        if(t.height() < 233)
                        {
                            t.find(".fader").replaceWith('<hr style="margin: 2px 0; border-color: #334155;">');
                        }
                    });
                }

                $('.linked').click(function(e){
                    $('#searchres').remove();
                    $("div[itemprop=articleBody],.rst-footer-buttons").show();
                });

                $('.expand').click(function(e){
                    var sect = $(this).closest(".searchsect");
                    t = sect.find('.expand');

                    if (sect.hasClass('expanded'))
                    {
                        t.html("show more");
                        sect.find('.expandTop').hide();
                        sect.css('max-height','250px');
                        var o = sect.offset();
                        if($(window).scrollTop() > o.top-40)
                            $(window).scrollTop(o.top-40);
                        sect.find('.cont').show();
                    }
                    else
                    {
                        t.text("show less");
                        sect.css("max-height","initial");
                        sect.find('.expandTop').show();
                        sect.find('.cont').hide();
                    }
                    sect.toggleClass('expanded');
                });
            }
        );

        $('#sclose').click(function(e) {
            sdiv.remove();
            $("div[itemprop=articleBody],.rst-footer-buttons").show();
        });
    }

    $('#rtd-search-form').submit(function(e){
        e.preventDefault();
        var q = $(this).find('input[name=q]').val();
        dosearch(q);
        return false;
    });

    $('#rtd-search-form input[name=q]').autocomplete(
        {
            serviceUrl: '/apps/docs/rsearch/suggest.json',
            onSelect: function(sel)
            {
                if (sel.data == "search")
                {
                    dosearch(sel.value);
                }
                else if(sel.data)
                {
                    $('#searchres').remove();
                    $("div[itemprop=articleBody],.rst-footer-buttons").show();
                    window.location.href = "/docs/" + sel.data;
                }
            }
        }
    );

    // Theme toggle
    function toggleTheme() {
        var isDark = document.documentElement.getAttribute('data-theme') === 'dark';
        if (isDark) {
            document.documentElement.removeAttribute('data-theme');
            document.cookie = 'theme=light; path=/; max-age=31536000; SameSite=Lax';
        } else {
            document.documentElement.setAttribute('data-theme', 'dark');
            document.cookie = 'theme=dark; path=/; max-age=31536000; SameSite=Lax';
        }
    }
    $('#rp-theme-toggle').on('click', toggleTheme);

    // Close sidebar overlay
    $('#rp-close-sidebar').on('click', function() {
        $('[data-toggle="wy-nav-shift"]').removeClass('shift');
        $('[data-toggle="rst-versions"]').removeClass('shift');
    });

});
`);
}

/* return true if all files in webserver destpath are newer than source files */
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

/* parse source files with some rewrites, get doc text to be searched and create search db */
function make_database(docpath,destpath) {

    /* drop existing table, if exists */
    var res = sql.exec("select * from SYSTABLES where NAME = 'sections'");
    if(res.rowCount)
    {
        sql.exec("drop table sections;");
    }

    /* create table */
    sql.exec("create table sections (title varchar(16), full varchar(64), plink varchar(32), level int, text varchar(128), html varchar(256) );");

    // an array of the list of *.html files in docpath
    var files = readdir(docpath + "/").filter(function(dir){ return /\.html/.test(dir); });

    for (var j=0; j<files.length;j++) {
        var file=files[j];
        var els = parse_html_file(docpath + "/"+file, destpath + "/"+file);
        var fullpath=[];
        for (i=0; i<els.length; i++)
        {
            var el=els[i];
            //var indent=(el.level-1) * 4; //for debugging code below

            /* create title links for sections, e.g. 
               "rampart-server : the rampart-server http module : loading and using the module : configuring and starting the server : start()" */
            var title = el.title.replace(/ ¶ /,"").toLowerCase();//remove pesky ¶ symbols in title tags

            fullpath[el.level-1] = title;
            fullname = fullpath.slice(0,el.level).join(' : ')

            /* use the id sphinx emitted.  Deriving it from the title left
               ':', '[]', '$' and others in the fragment, so the link missed. */
            link = file + "#" + (el.id ? el.id :
                   title.replace(/[^a-z0-9]+/g,'-').replace(/^[-0-9]+|-+$/g,''));

            // Some debugging code:
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
             "('[\\ualnum]+', '[\\ualnum_,$]+', '[\\ualnum_]+>>()=', '[\\ualnum_]+(=>>[\\ualnum_\\,\\. ]{1,25})=')" +
             "INDEXMETER 'on'"
    );
}

var webuser;

function build_db() {
    var path, docpath, destpath, dbpath;

    path=process.scriptPath;

    // remove "/apps/docs"
    path = path.replace(/\/[^\/]+\/[^\/]+$/, "");

    dbpath = path + "/data/docs/db";

    sql = new Sql.init(dbpath,true);

    var trypaths = [
        process.scriptPath + "/../../docs/html", //web_server/ide/docs/html in the rampart source tree
        "../../../build/html", //relative to github dirs
        "/usr/local/src/rampart_docs/build/html" //absolute, if script moved
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

    if(webuser) {
        var ret = shell(`chown -R ${webuser} ${destpath}`); 

        if(ret.exitStatus)
            throw(ret.stderr);
    }

    if(process.argv[2] != '-f' && is_current(docpath, destpath))
    {
        console.log("docs are up to date");
        return;
    }

    copy_files(path, docpath, destpath);

    make_database(docpath, destpath);
    
    make_index();

    if(webuser)
    {
        ret = shell(`chown -R ${webuser} ${destpath}`); 

        if(ret.exitStatus)
            throw(ret.stderr);

        ret = shell(`chown -R ${webuser} ${dbpath}`); 

        if(ret.exitStatus)
            throw(ret.stderr);
    }
}

// Command line, or module?
if(module && module.exports) {
    module.exports = {
        "suggest.json": suggest,
        "results.json": results
    }
} else {
    // Set "webuser" to chown the database to another user if running as root.
    // This is also taken care of in start_docs_web_server.sh
    //webuser='nobody';

    build_db();
}

