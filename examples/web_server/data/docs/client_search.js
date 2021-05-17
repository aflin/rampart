$(document).ready(function(){

    function dosearch(q)
    {
        $("#searchres").remove();
        $("div[itemprop=articleBody]").after('<div id="searchres">');
        $("div[itemprop=articleBody],.rst-footer-buttons").hide();
        var sdiv = $('#searchres');
        sdiv.html('<div style="cursor: pointer; float:right; border: 1px gray solid; border-radius:10px;width: 20px;height: 20px;position: relative;padding-left: 0.17em;" id="sclose">X</div>');
        $.getJSON(
            "/apps/docs/rsearch/results.json",
            {q:q},
            function(res)
            {
                var r = res.results;
                sdiv.append("<h3>Search Results:</h3><p><p>");
                console.log(r.length);
                for (var i=0;i<r.length;i++)
                {
                    sdiv.append('<p><span class="linked"><a class="linked" href="/docs/' +r[i].plink+ '">' + r[i].plink.replace(/\.html#.*/,'') + ' : ' +r[i].full+ '</a></span>');
                    sdiv.append(
                        '<div data-base="'+ r[i].plink.replace(/\#.*/,'')  +'" class="searchsect">'+
                        //'<span class="expand" style="position: absolute;top: 6px;right: 6px;">&nbsp;CLICK TO EXPAND&nbsp;</span>'+
                        r[i].html+
                        '<div class="fader"><div class="cont" style="bottom:3px; left:15px; position: relative;">...</div><div class="expand" style="bottom: 3px; right:15px; position: absolute;">Show More</div></div>');
                }
                if(r.length==0)
                {
                    sdiv.append('<p>No Results for "'+q+'"</p>');
                }
                else
                {
                    var links = sdiv.find("a.linked");
                    var hlinks = sdiv.find("a.headerlink");
                    var i=0;
                    for (;i<links.length;i++) {
                        var hx = hlinks.eq(i).parent();
                        hlinks.eq(i).remove();
                        var txt = hx.text();
                        hx.html('<a class="linked" href="' + links.eq(i).attr("href")+'">' + txt + '</a>');
                    }
                    $(".searchsect").each(function(){
                        var t=$(this);
                        console.log(t.height());
                        if(t.height() < 248)
                        {
                            t.find(".fader").hide();
                        }
                    });
                }

                $('.linked').click(function(e){
                    $('#searchres').remove();
                    $("div[itemprop=articleBody],.rst-footer-buttons").show();
                });

                $('.expand').click(function(e){
                    var t = $(this);
                    var sect = t.closest(".searchsect");
                    
                    if (sect.hasClass('expanded'))
                    {
                        t.html("show more");
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

    $('head').append(`
<style>
.autocomplete-suggestions { border: 1px solid #999; background: #FFF; overflow: auto; width: auto !important; padding-right:5px;}
.autocomplete-suggestion { padding: 2px 5px; white-space: nowrap; overflow: hidden; }
.autocomplete-selected { background: #F0F0F0; }
.autocomplete-suggestions strong { font-weight: normal; color: #3399FF; }
.autocomplete-group { padding: 2px 5px; }
.autocomplete-group strong { display: block; border-bottom: 1px solid #000; }
.searchsect {max-height: 250px; overflow:hidden; padding-left:5px; border: 1px dotted gray; position: relative;}
.expand {cursor:pointer;    background-color: #eee; padding: 3px;border: 1px gray dotted; font-family:monospace}
.linked { font-size: 1.1em; display: inline-block; position: relative; margin-top: 20px;}
.fader {position: absolute;bottom: 0px;height: 1em; left:-5px; right:0px; background-color: #fcfcfc;}
.wy-nav-content {max-width: 1600px;}
</style>`);

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


});