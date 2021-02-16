$(document).ready(function(){

    function dosearch(q)
    {
        $("#searchres").remove();
        $("div[itemprop=articleBody]").after('<div id="searchres">');
        $("div[itemprop=articleBody]").hide();
        var sdiv = $('#searchres');
        sdiv.html('<div style="cursor: pointer; float:right; border: 1px gray solid; border-radius:10px;width: 20px;height: 20px;position: relative;padding-left: 0.17em;" id="sclose">X</div>');
        $.getJSON(
            "/rampart/rsearch/results.json",
            {q:q},
            function(res)
            {
                var r = res.results;
                sdiv.append("<p><p>");
                for (var i=0;i<r.length;i++)
                {
                    sdiv.append('<p><span class="linked"><a href="/rampart/' +r[i].plink+ '">' + r[i].plink.replace(/\.html#.*/,'') + ' : ' +r[i].full+ '</a></span>');
                    //sdiv.append('<div class="expand">'+r[i].html+'<div class="texpand" style="position:absolute; bottom:0px; right:0px">↧</div></div>');
                    sdiv.append('<div data-base="'+ r[i].plink.replace(/\#.*/,'')  +'" class="expand">'+r[i].html+'<div class="fader"></div></div>');
                }
                $('.expand').click(function(e){
                    var t = $(this);
                    if(e.originalEvent.target.className=="std std-ref" || e.originalEvent.target.className=='reference internal')
                    {
                        var alink = $(e.originalEvent.target).closest('a');
                        var href = alink.attr("href");
                        sdiv.remove();
                        $("div[itemprop=articleBody]").show();
                        if(href.charAt(0) == '#')
                        {
                            var base = t.attr('data-base');
                            href = '/rampart/' + base + href;
                            //console.log("doing href="+href);
                        }
                        console.log(href);
                        window.location.href=href;
                        return;
                    }
                    if (t.hasClass('expanded'))
                    {
                        t.css('max-height','250px');
                        var o = t.offset();
                        if($(window).scrollTop() > o.top-40)
                            $(window).scrollTop(o.top-40);
                        t.find('.fader').show();
                    }
                    else
                    {
                        t.css("max-height","initial");
                        t.find('.fader').hide();
                    }
                    t.toggleClass('expanded');
                });
            }
        );

        $('#sclose').click(function(e) {
            sdiv.remove();
            $("div[itemprop=articleBody]").show();
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
.expand {max-height: 250px; overflow:hidden; border: 1px dotted gray; position: relative;}
.linked { font-size: 1.3em; display: inline-block; position: relative; margin-top: 20px;}
.fader {width: 100%;position: absolute;bottom: 0px;height: 50%;background-image: linear-gradient(to bottom, rgba(255,255,255,0), white);}
</style>`);

    $('#rtd-search-form input[name=q]').autocomplete(
        {
            serviceUrl: '/rampart/rsearch/suggest.json',
            onSelect: function(sel)
            {
                if (sel.data == "search")
                {
                    dosearch(sel.value);
                }
                else if(sel.data)
                {
                    window.location.href = "/rampart/" + sel.data;
                }
            }
        }
    );


});