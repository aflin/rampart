var curl=require("rpcurl");
var Sql=require("rpsql");
rampart.globalize(Sql,["rex","sandr","stringFormat"]);
var fetch=curl.fetch;
rampart.globalize(rampart.utils);

/*
    obj=
    {
            "http...":
            {
                google:
                {
                    title: "title",
                    desc:  "desc...",
                    pos: int
                },
                bing:
                {
                    title: "title",
                    desc:  "desc...",
                    pos: int
                }
                lowest: int:lowestpos
             },
            {...},
            ...
    }

*/

var htmltop="<!DOCTYPE HTML>\n"+
        '<html><head><meta charset="utf-8">\n'+
        "\n"+
        "<style>"+
            "body {font-family: arial,sans-serif;}"+
            "td {position:relative;}"+
            "#showrm {position:relative;}"+
            ".np { cursor:pointer;}"+
            ".itemwrap{ width: calc( 100% - 70px); position: relative;display: inline-block;}"+
            ".imgwrap {width:100%;float: left; display:inline-block;position:relative;padding-top:5px;}"+
            ".tstr {color:#07c;vertical-align:top;padding-top:3px;display:inline-block;position:absolute;right:0px;}"+
            ".abs { margin-right:5px;white-space: normal;}"+
            ".urlsp {color:#006621;max-width:100%;overflow: hidden;text-overflow: ellipsis;white-space:nowrap;display:inline-block;font-size:.90em;}"+
            ".urla {text-decoration: none;font-size:16px;overflow: hidden;text-overflow: ellipsis;white-space:nowrap;display:inline-block; width: 100%; }"+
            ".b { font-size: 18px; margin-left:4px; }"+
            ".onsw { background-color: #d00; color: white; float:right;}"+
            ".onsw-on { background-color:#0c5;} "+
            "#res {font-size:12px;padding:15px 10px 0px 0px;}"+
            "#setbox {position:relative; padding:10px; margin:10px; background-color:#eee; border: 1px dotted gray; top:0px; left:0px;}"+
            "#setbox td {white-space:nowrap;}"+
            ".sall{ cursor: pointer;position: absolute;left: -15px;top: 0px;}"+
            ".ib { display: inline-block; }"+
            ".rm {display:none; top:24px; position:absolute; font-size: 15px;width: 12px;text-align: center;cursor:pointer; font-weight: bold;}"+
            ".res {margin-top: 80px;}"+
            ".resi {min-height:20px;position:relative;clear:both;padding-top: 15px;}"+
            ".hm {display:none;}"+
            ".nw { white-space:nowrap;}"+
            ".ico { width:24px;height:24px;margin-right:5px;display:none;}"+
        "</style>"+
        "<script>\n"+
            "function loadImage(img) {\n"+
                "var t=new Image();\n"+
                "var url=img.getAttribute('data-src');\n"+
                "t.onload=function() {\n"+
                    "img.src=url;\n"+
                    "img.style.display='inline';\n"+
                "}\n"+
                "t.src=url;\n"+
            "}\n"+
            "document.addEventListener('DOMContentLoaded', function(event) {\n"+
                "var icons=document.getElementsByClassName('ico');\n"+
                "for (var i=0; i<icons.length; i++)\n"+
                    "loadImage(icons.item(i));\n"+
            "});\n"+
        "</script>\n"+
        "</head><body>"+
        '<div id="lc" style="background-color: white; position: fixed; left:0px; top:0px; min-height: 300px; overflow-x: hidden; padding-right: 20px; padding-left: 20px; box-sizing: border-box; width: 200px;">'+
        ' <div style="width:180px;height:128px;margin-bottom:15px"><img src="data:image/jpeg;base64,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" style="width:145px;height:108px;padding-left:16px;margin-top:10px">'+
          '【ＲａｍｐａｒｔＤＢ】</div>'+
        '</div>'+
        '<div id="main" style="padding-bottom:30px;background-color: white; position: absolute; left:200px; top:0px; min-height: 300px; overflow-x: hidden; padding-right: 20px; padding-left: 30px; box-sizing: border-box; width: 600px;">'+
          '<form id="mf" action="/search.html">'+
            '<div style="width:100%">'+
              '<span style="white-space:nowrap;display:block;width:500px;height:39px;position:fixed;background-color: white;z-index:10;border-bottom: lightGray 1px solid; padding-top:15px;padding-bottom:15px">'+
                '<table style="background-color: white; width:100%">'+
                  '<tr>'+
                    '<td style="position:relative">'+
                      '<input autocomplete="off" type="text" id="fq" name="q" value="';
var htmltop2 =                    '" placeholder="Search" style="box-sizing:border-box;min-width:150px;width:100%;height:30px;font:normal 18px arial,sans-serif;padding: 1px 3px;border: 2px solid #ccc;">'+
                      '<input type=image id="search" style="height:22px;position: absolute; right: 0px;margin: 4px;" src="data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+CjxzdmcKICAgeG1sbnM6ZGM9Imh0dHA6Ly9wdXJsLm9yZy9kYy9lbGVtZW50cy8xLjEvIgogICB4bWxuczpjYz0iaHR0cDovL2NyZWF0aXZlY29tbW9ucy5vcmcvbnMjIgogICB4bWxuczpyZGY9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkvMDIvMjItcmRmLXN5bnRheC1ucyMiCiAgIHhtbG5zOnN2Zz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciCiAgIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIKICAgeG1sbnM6eGxpbms9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsiCiAgIHZlcnNpb249IjEuMSIKICAgaWQ9InN2ZzQxNTUiCiAgIHZpZXdCb3g9IjAgMCA4MDAuMDAwMDEgODAwLjAwMDAxIgogICBoZWlnaHQ9IjgwMCIKICAgd2lkdGg9IjgwMCI+CiAgPGRlZnMKICAgICBpZD0iZGVmczQxNTciPgogICAgPGxpbmVhckdyYWRpZW50CiAgICAgICBpZD0ibGluZWFyR3JhZGllbnQ1NTQ4Ij4KICAgICAgPHN0b3AKICAgICAgICAgaWQ9InN0b3A1NTUwIgogICAgICAgICBvZmZzZXQ9IjAiCiAgICAgICAgIHN0eWxlPSJzdG9wLWNvbG9yOiMwMDAwMDA7c3RvcC1vcGFjaXR5OjAuNCIgLz4KICAgICAgPHN0b3AKICAgICAgICAgaWQ9InN0b3A1NTUyIgogICAgICAgICBvZmZzZXQ9IjEiCiAgICAgICAgIHN0eWxlPSJzdG9wLWNvbG9yOiMwMDAwMDA7c3RvcC1vcGFjaXR5OjA7IiAvPgogICAgPC9saW5lYXJHcmFkaWVudD4KICAgIDxsaW5lYXJHcmFkaWVudAogICAgICAgZ3JhZGllbnRVbml0cz0idXNlclNwYWNlT25Vc2UiCiAgICAgICB5Mj0iODQ3Ljg1ODA5IgogICAgICAgeDI9Ii02OC4yMzQ5MzIiCiAgICAgICB5MT0iNDE2LjQyOTU3IgogICAgICAgeDE9IjM0OS42NjQ4NiIKICAgICAgIGlkPSJsaW5lYXJHcmFkaWVudDU1NTQiCiAgICAgICB4bGluazpocmVmPSIjbGluZWFyR3JhZGllbnQ1NTQ4IiAvPgogICAgPG1hc2sKICAgICAgIGlkPSJtYXNrNTU1NiIKICAgICAgIG1hc2tVbml0cz0idXNlclNwYWNlT25Vc2UiPgogICAgICA8Y2lyY2xlCiAgICAgICAgIHI9IjQwMCIKICAgICAgICAgY3k9IjY1Mi4zNjIxOCIKICAgICAgICAgY3g9IjQwMCIKICAgICAgICAgaWQ9ImNpcmNsZTU1NTgiCiAgICAgICAgIHN0eWxlPSJjb2xvcjojMDAwMDAwO2NsaXAtcnVsZTpub256ZXJvO2Rpc3BsYXk6aW5saW5lO292ZXJmbG93OnZpc2libGU7dmlzaWJpbGl0eTp2aXNpYmxlO29wYWNpdHk6MTtpc29sYXRpb246YXV0bzttaXgtYmxlbmQtbW9kZTpub3JtYWw7Y29sb3ItaW50ZXJwb2xhdGlvbjpzUkdCO2NvbG9yLWludGVycG9sYXRpb24tZmlsdGVyczpsaW5lYXJSR0I7c29saWQtY29sb3I6IzAwMDAwMDtzb2xpZC1vcGFjaXR5OjE7ZmlsbDojZmZmZmZmO2ZpbGwtb3BhY2l0eToxO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZTpub25lO3N0cm9rZS13aWR0aDowLjQwMDAwMDAxO3N0cm9rZS1taXRlcmxpbWl0OjQ7c3Ryb2tlLWRhc2hhcnJheTpub25lO3N0cm9rZS1vcGFjaXR5OjE7Y29sb3ItcmVuZGVyaW5nOmF1dG87aW1hZ2UtcmVuZGVyaW5nOmF1dG87c2hhcGUtcmVuZGVyaW5nOmF1dG87dGV4dC1yZW5kZXJpbmc6YXV0bztlbmFibGUtYmFja2dyb3VuZDphY2N1bXVsYXRlIiAvPgogICAgPC9tYXNrPgogICAgPG1hc2sKICAgICAgIGlkPSJtYXNrNTU2MCIKICAgICAgIG1hc2tVbml0cz0idXNlclNwYWNlT25Vc2UiPgogICAgICA8Y2lyY2xlCiAgICAgICAgIHI9IjQwMCIKICAgICAgICAgY3k9IjQwMC4wMDAwMyIKICAgICAgICAgY3g9IjQwMCIKICAgICAgICAgaWQ9ImNpcmNsZTU1NjIiCiAgICAgICAgIHN0eWxlPSJjb2xvcjojMDAwMDAwO2NsaXAtcnVsZTpub256ZXJvO2Rpc3BsYXk6aW5saW5lO292ZXJmbG93OnZpc2libGU7dmlzaWJpbGl0eTp2aXNpYmxlO29wYWNpdHk6MTtpc29sYXRpb246YXV0bzttaXgtYmxlbmQtbW9kZTpub3JtYWw7Y29sb3ItaW50ZXJwb2xhdGlvbjpzUkdCO2NvbG9yLWludGVycG9sYXRpb24tZmlsdGVyczpsaW5lYXJSR0I7c29saWQtY29sb3I6IzAwMDAwMDtzb2xpZC1vcGFjaXR5OjE7ZmlsbDojZmZmZmZmO2ZpbGwtb3BhY2l0eToxO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZTpub25lO3N0cm9rZS13aWR0aDowLjQwMDAwMDAxO3N0cm9rZS1taXRlcmxpbWl0OjQ7c3Ryb2tlLWRhc2hhcnJheTpub25lO3N0cm9rZS1vcGFjaXR5OjE7Y29sb3ItcmVuZGVyaW5nOmF1dG87aW1hZ2UtcmVuZGVyaW5nOmF1dG87c2hhcGUtcmVuZGVyaW5nOmF1dG87dGV4dC1yZW5kZXJpbmc6YXV0bztlbmFibGUtYmFja2dyb3VuZDphY2N1bXVsYXRlIiAvPgogICAgPC9tYXNrPgogIDwvZGVmcz4KICA8bWV0YWRhdGEKICAgICBpZD0ibWV0YWRhdGE0MTYwIj4KICAgIDxyZGY6UkRGPgogICAgICA8Y2M6V29yawogICAgICAgICByZGY6YWJvdXQ9IiI+CiAgICAgICAgPGRjOmZvcm1hdD5pbWFnZS9zdmcreG1sPC9kYzpmb3JtYXQ+CiAgICAgICAgPGRjOnR5cGUKICAgICAgICAgICByZGY6cmVzb3VyY2U9Imh0dHA6Ly9wdXJsLm9yZy9kYy9kY21pdHlwZS9TdGlsbEltYWdlIiAvPgogICAgICAgIDxkYzp0aXRsZT48L2RjOnRpdGxlPgogICAgICA8L2NjOldvcms+CiAgICA8L3JkZjpSREY+CiAgPC9tZXRhZGF0YT4KICA8ZwogICAgIGlkPSJsYXllcjEiCiAgICAgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoMCwtMjUyLjM2MjE2KSI+CiAgICA8Y2lyY2xlCiAgICAgICBzdHlsZT0iY29sb3I6IzAwMDAwMDtjbGlwLXJ1bGU6bm9uemVybztkaXNwbGF5OmlubGluZTtvdmVyZmxvdzp2aXNpYmxlO3Zpc2liaWxpdHk6dmlzaWJsZTtvcGFjaXR5OjE7aXNvbGF0aW9uOmF1dG87bWl4LWJsZW5kLW1vZGU6bm9ybWFsO2NvbG9yLWludGVycG9sYXRpb246c1JHQjtjb2xvci1pbnRlcnBvbGF0aW9uLWZpbHRlcnM6bGluZWFyUkdCO3NvbGlkLWNvbG9yOiMwMDAwMDA7c29saWQtb3BhY2l0eToxO2ZpbGw6IzFjOGFkYjtmaWxsLW9wYWNpdHk6MTtmaWxsLXJ1bGU6bm9uemVybztzdHJva2U6bm9uZTtzdHJva2Utd2lkdGg6MC40MDAwMDAwMTtzdHJva2UtbWl0ZXJsaW1pdDo0O3N0cm9rZS1kYXNoYXJyYXk6bm9uZTtzdHJva2Utb3BhY2l0eToxO2NvbG9yLXJlbmRlcmluZzphdXRvO2ltYWdlLXJlbmRlcmluZzphdXRvO3NoYXBlLXJlbmRlcmluZzphdXRvO3RleHQtcmVuZGVyaW5nOmF1dG87ZW5hYmxlLWJhY2tncm91bmQ6YWNjdW11bGF0ZSIKICAgICAgIGlkPSJwYXRoNDcxMiIKICAgICAgIGN4PSI0MDAiCiAgICAgICBjeT0iNjUyLjM2MjE4IgogICAgICAgcj0iNDAwIiAvPgogICAgPHBhdGgKICAgICAgIG1hc2s9InVybCgjbWFzazU1NjApIgogICAgICAgaWQ9InBhdGg0NzMwIgogICAgICAgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoMCwyNTIuMzYyMTYpIgogICAgICAgZD0ibSAzNDguMzI2MTcsMTkzLjk1MTE3IGMgLTM5LjUwODU4LDAgLTc5LjAxNTY0LDE1LjA3MTUyIC0xMDkuMTYwMTUsNDUuMjE0ODUgTCAzLjg1MzUyMDQsNDc0LjQ3ODUyIEMgMzMuMTA4MDEyLDY0OC4yOTA1OSAxMTIuNjM4MDIsNzg1Ljc1ODkxIDM2My44ODU0OCw3OTkuMDk0MTQgTCA2MDYuMDUwNzgsNTU2LjkyOTY5IDQ3OS4xMjg5MSw0MzAuMDA3ODEgYyAzOC4wNTIwNiwtNjAuOTA0MjcgMjkuMDgyMzMsLTE0MC4wMDQxIC0yMS42NDA2MywtMTkwLjg0MTc5IC0zMC4xNDQ1MSwtMzAuMTQzMzMgLTY5LjY1MzUzLC00NS4yMTQ4NSAtMTA5LjE2MjExLC00NS4yMTQ4NSB6IgogICAgICAgc3R5bGU9ImNvbG9yOiMwMDAwMDA7Y2xpcC1ydWxlOm5vbnplcm87ZGlzcGxheTppbmxpbmU7b3ZlcmZsb3c6dmlzaWJsZTt2aXNpYmlsaXR5OnZpc2libGU7b3BhY2l0eToxO2lzb2xhdGlvbjphdXRvO21peC1ibGVuZC1tb2RlOm5vcm1hbDtjb2xvci1pbnRlcnBvbGF0aW9uOnNSR0I7Y29sb3ItaW50ZXJwb2xhdGlvbi1maWx0ZXJzOmxpbmVhclJHQjtzb2xpZC1jb2xvcjojMDAwMDAwO3NvbGlkLW9wYWNpdHk6MTtmaWxsOnVybCgjbGluZWFyR3JhZGllbnQ1NTU0KTtmaWxsLW9wYWNpdHk6MTtmaWxsLXJ1bGU6bm9uemVybztzdHJva2U6bm9uZTtzdHJva2Utd2lkdGg6MC40MDAwMDAwMTtzdHJva2UtbWl0ZXJsaW1pdDo0O3N0cm9rZS1kYXNoYXJyYXk6bm9uZTtzdHJva2Utb3BhY2l0eToxO2NvbG9yLXJlbmRlcmluZzphdXRvO2ltYWdlLXJlbmRlcmluZzphdXRvO3NoYXBlLXJlbmRlcmluZzphdXRvO3RleHQtcmVuZGVyaW5nOmF1dG87ZW5hYmxlLWJhY2tncm91bmQ6YWNjdW11bGF0ZSIgLz4KICAgIDxwYXRoCiAgICAgICBtYXNrPSJ1cmwoI21hc2s1NTU2KSIKICAgICAgIGlkPSJjaXJjbGU0MTQ5IgogICAgICAgZD0ibSAyMzkuMTY1MDQsNDkxLjUyNzcgYSAxNTQuMzgwOTcsMTU0LjM4MDk3IDAgMCAwIDAsMjE4LjMyNDI4IDE1NC4zODA5NywxNTQuMzgwOTcgMCAwIDAgMTkwLjg0MTAxLDIxLjY0MDY4IEwgNTU2LjkyNzY3LDg1OC40MTI4NyA2MDYuMDUwNCw4MDkuMjkxNDIgNDc5LjEyODc4LDY4Mi4zNjk2MyBBIDE1NC4zODA5NywxNTQuMzgwOTcgMCAwIDAgNDU3LjQ4ODQsNDkxLjUyNzcgYSAxNTQuMzgwOTcsMTU0LjM4MDk3IDAgMCAwIC0yMTguMzIzMzYsMCB6IG0gMzYuMzk0MjcsMzYuMzk0NTggYSAxMDIuOTIwNjUsMTAyLjkyMDY1IDAgMCAxIDE0NS41MzQ2NiwwIDEwMi45MjA2NSwxMDIuOTIwNjUgMCAwIDEgMCwxNDUuNTM1MTEgMTAyLjkyMDY1LDEwMi45MjA2NSAwIDAgMSAtMTQ1LjUzNDY2LDAgMTAyLjkyMDY1LDEwMi45MjA2NSAwIDAgMSAwLC0xNDUuNTM1MTEgeiIKICAgICAgIHN0eWxlPSJjb2xvcjojMDAwMDAwO2NsaXAtcnVsZTpub256ZXJvO2Rpc3BsYXk6aW5saW5lO292ZXJmbG93OnZpc2libGU7dmlzaWJpbGl0eTp2aXNpYmxlO29wYWNpdHk6MTtpc29sYXRpb246YXV0bzttaXgtYmxlbmQtbW9kZTpub3JtYWw7Y29sb3ItaW50ZXJwb2xhdGlvbjpzUkdCO2NvbG9yLWludGVycG9sYXRpb24tZmlsdGVyczpsaW5lYXJSR0I7c29saWQtY29sb3I6IzAwMDAwMDtzb2xpZC1vcGFjaXR5OjE7ZmlsbDojZmZmZmZmO2ZpbGwtb3BhY2l0eToxO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZTpub25lO3N0cm9rZS13aWR0aDowLjQwMDAwMDAxO3N0cm9rZS1taXRlcmxpbWl0OjQ7c3Ryb2tlLWRhc2hhcnJheTpub25lO3N0cm9rZS1vcGFjaXR5OjE7Y29sb3ItcmVuZGVyaW5nOmF1dG87aW1hZ2UtcmVuZGVyaW5nOmF1dG87c2hhcGUtcmVuZGVyaW5nOmF1dG87dGV4dC1yZW5kZXJpbmc6YXV0bztlbmFibGUtYmFja2dyb3VuZDphY2N1bXVsYXRlIiAvPgogIDwvZz4KPC9zdmc+Cg==">'+
                    '</td>'+
                  '</tr>'+
                '</table>'+
              '</span>'+
            '</div> '+
          '</form>'+
          '<div class="res">';


function add_res(obj,url,eng,title,desc,pos)
{
    if(undefined == obj[url])
        obj[url]={lowestpos:999999999};

    obj[url][eng]={
        title:title,
        desc:desc,
        pos:pos,
    }
    if(pos < obj[url].lowestpos)
        obj[url].lowestpos=pos;
}


function comboformat(obj,q)
{
    var keys=Object.keys(obj);
    var arr=[];
    var ret="";
    var qq="@0 "+q;
    for (var i=0;i<keys.length;i++)
    {
        var key=keys[i];
        var url=obj[key];
        var newent ={
            url:keys[i],
            title:"",
            desc:"",
            postxt:""
        };
        var keys2=Object.keys(url);
        for (var j=0;j<keys2.length;j++)
        {
            var key2=keys2[j];
            engent=url[key2];
            if(key2=='lowestpos'){
                newent.pos=engent;
                continue;
            }
            if(newent.title.length<engent.title.length)
                newent.title=engent.title;
            
            if(newent.desc.length<engent.desc.length)
                newent.desc=engent.desc;

            newent.postxt += key2 + " (" + (1+engent.pos) + ") ";
        }
        arr.push(newent);
    }
    arr.sort(function(a,b){return a.pos - b.pos});
//console.log(JSON.stringify(arr,null,4));
    for (i=0;i<arr.length;i++)
    {
        var r=arr[i];
        var ico=rex(">>=http=s?://=[^/]+/=",r.url)
        if(ico && ico.length)
            ico='<img class="ico" data-src="' + ico[0] + 'favicon.ico">';
        else
            ico="";
        ret+=Sql.stringFormat('<div class="resi" style="padding-top: 15px;">'+
                    '<span class="imgwrap">'+
                    '<span class="itemwrap">'+
                        '<span class="abs nw">'+
                          '%s<a class="urla tar" target="_blank" href="%s">%s</a>'+
                        '</span>'+
                        '<span class="abs urlsp snip">%s</span><br>'+
                        '<span class="abs urlsp snip">%s</span><br>'+
                        '<span class="abs snip">%mbs</span>'+
                  '</span></span></div>'
            ,ico,r.url,r.title,r.url,r.postxt,qq,r.desc); 
//        ret=sprintf('%s<div class="res"><a href="%!U">%s</a><br><span class="desc">%s</span></div>\n',ret,r.url,r.title,r.desc);        
    }
    return ret;
}

function gparse(txt,obj)
{
    var rres=rex('<a href\\="/url\\?q\\==!<a href\\="/url\\?q\\=*>></div></div></div></div></div></div></div></div>=',txt,{submatches:true});

    var j=0;
    for (var i=0;i<rres.length;i++) {
        var subs=rres[i].submatches;

        var url=rex(">>=[^&]+",subs[1])[0];
        url=sprintf("%!U", url); //un-urlencode the url
        if (!url || !url.length || !/http/.test(url) )
            continue;

        var title=rex('>><h3=[^>]*>=!</h3*',subs[1]);
        if(!title.length) 
            continue;
        title=sandr('>><=[^>]+>=',' ',title[0]);

        var desc=rex('!</h3>*>>=',subs[1]);
        if(!desc.length)
            continue;
        desc=sandr('[^>]+>>›[^<]+','',desc[0]);
        desc=sandr('>><=[^>]+>=',' ',desc);

        add_res(obj,url,"google",title,desc,j++);
    }    
}

function bparse(txt,obj)
{
    var rres=rex('>><li =[^>]+>=!</li>*',txt,{submatches:true});
    var j=0;
    for (var i=0;i<rres.length;i++) {
        var subs=rres[i].submatches;

        var url=rex('>><a href\\="\\P=[^"]+',subs[3]);
        if (!url || !url.length || !/http/.test(url) ) {
        //    console.log("NO URL",JSON.stringify(subs,null,4));
            continue;
        }
        url=url[0];

        var title=rex('>><a href\\==[^>]+>\\P=[^<]+',subs[3]);
        if(!title || !title.length) {
            //console.log("NO TITLE",JSON.stringify(subs,null,4));
            continue;
        }
        title=title[0];

        var desc=rex('>><p=[^>]*>\\P=!</p>+',subs[3]);
        if(!desc || !desc.length) {
            //console.log("NO DESC",JSON.stringify(subs,null,4));
            continue;
        }
        desc=sandr('>><=[^>]+>=',' ',desc[0]);

        add_res(obj,url,"bing",title,desc,j++);
    }    

}

function dparse(txt,obj)
{
    var rres=rex('>>web-result=[ ">]+!</h2>+!result__snippet*!</a>*',txt,{submatches:true});
//console.log(JSON.stringify(rres,null,4));
    var j=0;
    for (var i=0;i<rres.length;i++) {
        var subs=rres[i].submatches;

        var url=rex('>><a =!href*href\\="\\P=[^"]+',subs[2]);
        if (!url || !url.length) {
            //console.log("NO URL",JSON.stringify(subs,null,4));
            continue;
        }
        url=url[0];

        var title=rex('>><a =!href*href\\==[^>]+>\\P=!</a>*',subs[2]);
        if(!title || !title.length) {
            //console.log("NO TITLE",JSON.stringify(subs,null,4));
            continue;
        }
        title=sandr('>><=[^>]+>=',' ', title[0]);

        var desc=rex('>>=[^>]+>\\P=!</a>*',subs[4]);
        if(!desc || !desc.length) {
            //console.log("NO DESC",JSON.stringify(subs,null,4));
            continue;
        }
        desc=sandr('>><=[^>]+>=',' ',desc[0]);

        add_res(obj,url,"duckduckgo",title,desc,j++);
    }    

}

function aparse(txt,obj,j)
{
    var rres=rex('>>PartialSearchResults-item-title=!</p>*!</div>*',txt);
    for (var i=0;i<rres.length;i++) {
        var match=rres[i];
        var url=rex('>><a =!href*href\\=[\'"]\\P=[^"\']+',match);
        if (!url || !url.length) {
            //console.log("NO URL",match);
            continue;
        }
        url=url[0];

        var title=rex('>><a =!href*href\\==[^>]+>\\P=!</a>*',match);
        if(!title || !title.length) {
            //console.log("NO TITLE",match);
            continue;
        }
        title=sandr('>><=[^>]+>=',' ', title[0]);

        var desc=rex('>>PartialSearchResults-item-abstract">\\P=!</p>*',match);
        if(!desc || !desc.length) {
            //console.log("NO DESC",match);
            continue;
        }
        desc=sandr('>><=[^>]+>=',' ',desc[0]);

        add_res(obj,url,"ask",title,desc,j++);
    }    

}


module.exports=function(req) {
        
    var q=req.params.q;
    var resobj={};
    if(q)
    {
        var gu=sprintf("https://www.google.com/search?q=%U&hl=en&gbv=1&ie=UTF-8&num=30",q);
        var bu=sprintf("https://www.bing.com/search?q=%U&count=30",q);
        var du=sprintf("https://html.duckduckgo.com/html/?q=%U",q);
        var au=sprintf("https://www.ask.com/web?q=%U",q);
        var au2=sprintf("https://www.ask.com/web?q=%U&page=2",q);
        var res;
        curl.fetch([au,au2,du,bu,gu],
        {
            headers: [
                'User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/78.0.3904.70 Safari/537.36 Rampart-Metasearch(demo; not for production use)',
            ]
        },
        function(res){
            if(/google/.test(res.url))
                gparse(res.text,resobj);        
            else if (/bing/.test(res.url))
                bparse(res.text,resobj);
            else if (/duck/.test(res.url))
                dparse(res.text,resobj);
            else if (/page/.test(res.url))
                aparse(res.text,resobj,10);
            else if (/ask/.test(res.url))
                aparse(res.text,resobj,0);
            
        });

        res=comboformat(resobj,q);
        return({html:htmltop+q+htmltop2+res+'</body></html>'});
    }        
    return({status:302,headers:{location:'/'}});
}

