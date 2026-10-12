Building a Pi News Aggregator
=============================

Preface
-------

In this tutorial, we will use  :ref:`rampart-curl<rampart-curl:The rampart-curl module>`
, the :ref:`rampart-sql <rampart-sql:Preface>`\ , the 
:ref:`rampart-html <rampart-html:The rampart-html module>`\ ,
the :ref:`rampart-robots <rampart-robots:The rampart-robots module>`\ ,
the :ref:`rampart-url <rampart-url:The rampart-url module>` and the
:ref:`rampart-server <rampart-server:The rampart-server HTTP module>`
modules.  With these we will grab the latest articles from three Raspberry
Pi news sites, process them and build or update a SQL database which will
be used for a landing page and a search.

You can download the complete project from our 
`Tutorial Repository <https://github.com/aflin/rampart_tutorials>`_ in the
`pi_news directory <https://github.com/aflin/rampart_tutorials/tree/main/pi_news>`_\ .

License
~~~~~~~

The code related to this tutorial is released under the MIT license.

Concepts
--------

This module will demonstrate:

*  Using :ref:`rampart-curl<rampart-curl:The rampart-curl module>` to grab
   articles over http.
*  Using :ref:`rampart-robots <rampart-robots:The rampart-robots module>` to
   comply with `robots.txt exclusions <https://developers.google.com/search/docs/advanced/robots/intro>`_\ .
*  Using :ref:`rampart-html <rampart-html:The rampart-html module>` to
   find article links, images and the main article content.
*  Using :ref:`rampart-url <rampart-url:The rampart-url module>` to
   normalize links extracted from html pages.
*  Using :ref:`rampart-server <rampart-server:The rampart-server HTTP module>`
   to create and serve the web interface.
*  Using :ref:`rampart-sql <rampart-sql:Preface>` to store the article text and create
   a full text search engine.

In order to complete this tutorial, you should have basic knowledge of
JavaScript, HTML and SQL.

Getting Started
---------------

Web Server Layout
~~~~~~~~~~~~~~~~~

In the Rampart binary distribution is a sample web server tree.  For our
purposes here, we assume you have downloaded and unzipped the Rampart binary
distribution into a directory named ``~/downloads/rampart``. We will use
that for this project.  
The :ref:`The rampart-server HTTP module <rampart-server:The rampart-server HTTP module>`
is configured and loaded from the included ``web_server/web_server_conf.js``
script.  It defines ``web_server/html`` as the default directory for static
html, ``web_server/apps`` as the default directory for scripts.

To get started, copy the ``web_server`` directory to a convenient place for
this project.  Also, for our purposes, we do not need
anything in the ``web_server/apps/test_modules`` or
``web_server/apps/wsapps`` directories, so you can delete the copy of those
files. We will also add an empty file at 
``web_server/wsapps/wschat.js`` and ``web_server/html/websockets_chat/index.html``.

::

    user@localhost:~$ mkdir pi_news
    user@localhost:~$ cd pi_news
    user@localhost:~/pi_news$ touch pi_news_aggregator.js
    user@localhost:~/pi_news$ cp -a ~/downloads/rampart/web_server ./
    user@localhost:~/pi_news$ cd web_server/
    user@localhost:~/pi_news/web_server$ rm -rf apps/test_modules wsapps/* html/index.html
    user@localhost:~/pi_news/web_server$ mkdir apps/pi_news
    user@localhost:~/pi_news/web_server$ touch apps/pi_news/search.js
    user@localhost:~/pi_news/web_server$ find .
    ./wsapps
    ./start_server.sh
    ./stop_server.sh
    ./logs
    ./apps
    ./apps/pi_news
    ./apps/pi_news/search.js
    ./web_server_conf.js
    ./html
    ./html/images
    ./html/images/inigo-not-fount.jpg
    ./html/websockets_chat
    ./html/websockets_chat/index.html
    ./data

The Aggregator
--------------

Let's start with a skeleton script with the needed modules loaded by editing
the ``pi_news_aggregator.js`` file:

.. code-block:: javascript

    #!/usr/local/bin/rampart

    rampart.globalize(rampart.utils);

    var Sql    = require("rampart-sql");
    var curl   = require("rampart-curl");
    var html   = require("rampart-html");
    var robots = require("rampart-robots");
    var urlutil= require("rampart-url");
    var dbdir  = process.scriptPath + "/web_server/data/pi_news"

    var sql = new Sql.init(dbdir, true);

    // The date format we are expecting from http servers
    var dateFmt = "%a, %d %b %Y %H:%M:%S GMT";

    var crawlDelay = 10;  // go slow - delay in seconds between fetches from same site.

    // Columns for our table
    // status, fetch_count, server_date, fetch_date, site, url, img_url, title, text
    var schema = "status smallint, fetch_count smallint, server_date date, fetch_date date, " +
                 "site varchar(8), url varchar(64), img_url varchar(64), title varchar(64), " +
                 "text varchar(1024)";

    // running in two stages - first gets all articles from several pages
    //                       - second gets latest articles from main page
    // First stage is done by running with command line argument '--first-run'
    var firstRun = false;
    if ( process.argv[2] == '--first-run' )
        firstRun=true;

    var sites = {}

    // the names of our sites.
    var sitenames = Object.keys(sites);

    function fetch(url) {
    }

    // process the index page holding links to articles
    function procIndex(site, docBody) {
    }

    // update a row of data in our table
    function update_row(data) {
    }

    // insert a new row into our table
    function insert_row(data) {
    }

    // check for new articles in the main index page (sites[].url)
    function check_new_articles() {
    }

    // on first run, get main index page and a few more index pages
    function first_run() {
    }

    // get relevant text and info from article html
    function procpage(site, dbrow, fetch_res) {
    }

    //fetch, process and update table
    function fetch_article(sitename) {
    }

    // get article pages
    function fetch_all_articles(){
    }

    // get index pages
    if(firstRun) {
        first_run();
    } else {
        check_new_articles();
    }

    // get article pages
    fetch_all_articles();

Examining the External Sites
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In order to parse the html to extract links, images and content, we need to
have a peek at the each site's structure.  This is the portion of the script
which we will need to keep up to date if any of the sites we are scraping
change their format.  But in short, we want to know:

* The URL of the index page with the links to the latest articles.
* The format of the URL for more of these page, for older content.
* How many of these extra pages to fetch on our first run.
* How to locate the links to articles and, if present, images from within
  the index page.
* How to locate the relevant content (and possibly an image link) within the 
  article pages themselves.
* How to remove unwanted content inside our extracted "relevant content".

So, without going into great detail, we will define all of these in our
``sites`` :green:`Object`.  In this case, the relevant HTML tags can be
found using CSS class names:

.. code-block:: javascript

    /* details of each scrape:
        url                - the index page with links to the latest articles
        urlNextFmt         - more index pages with links to articles
        initialPages       - how many pages with links to articles to grab on first run
        entryClass         - the CSS class of the element holding article links, for index pages
        entryImgClass      - the CSS class of the element on the index pages with a related image
        contentClass       - on article page, the CSS of element that holds most relevant text
        contentImgClass    - the CSS class of the element on the content pages with a related image
        contentRemoveClass - the CSS class of elements on the content pages inside contentClass that should be removed
    */

    var sites = {
        "hackaday":
        {
            name: "hackaday",
            url: "https://hackaday.com/category/raspberry-pi-2/",
            urlNextFmt: "https://hackaday.com/category/raspberry-pi-2/page/%d/",
            initialPages: 8,        
            entryClass: "entry-featured-image",
            contentImgClass: "entry-featured-image",
            contentClass: 'post'
        },
        "raspberrypi":
        {
            name: "raspberrypi",
            url: "https://www.raspberrypi.com/news/",
            urlNextFmt: "https://www.raspberrypi.com/news/page/%d/",
            initialPages: 5,
            entryClass: "c-blog-post-card__link",
            entryImgClass: "c-blog-post-card__image",
            contentClass: "c-blog-post-content",
            contentRemoveClass: ["c-blog-post-content__footer"]
        },
        "makeuseof":
        {
            name: "makeuseof",
            url: "https://www.makeuseof.com/search/raspberry%20pi/",
            urlNextFmt: "https://www.makeuseof.com/search/raspberry%%20pi/%d/",
            initialPages: 3,
            entryClass: "bc-title-link",
            entryImgClass: "bc-img",
            contentClass: "article",
            contentRemoveClass: ["sidebar", "next-btn", "sharing", "letter-from"]
        }
    }
 
Now that we have the basic information for each site, we need to set up a
polite way of fetching content, respecting robots.txt.  We will make a
``fetch()`` function that automatically does this for us.  Since all HTTP
status codes are positive and greater than 100, (i.e.  ``200``), we will use
codes less than 100 for errors related to the fetch, and later store the
code in our table.

In this function, we will load the robots.txt file for each site only once, and
then use it with each new url from that site to check if crawling is allowed.

.. code-block:: javascript

    // status < 0 -- try or retry
    // status > 0 -- don't retry
    // status = 0 -- first try
    // status > 0 and < 100 - custom codes
    var statTexts = {
    "1": "Disallowed by robots.txt",
    "2": "Cannot parse Url",
    "-1": "Error retrieving robots.txt"
    }

    var robotstxt = {}; //load these once per run
    var userAgent = "rampart_tutorial";  //our identification

    // fetch with robots.txt check
    function fetch(url) {
        var comp = urlutil.components(url);
        var origin, res;
        if(!comp)
            return {status: 2, statusText: statTexts[2]};

        origin=comp.origin;

        if(!robotstxt[origin]) {
            var rurl = origin + '/robots.txt'
            printf("fetching %s\r", rurl);
            fflush(stdout);

            // body is a buffer.  robots.isAllowed also takes a buffer, so we can dispense with text.
            res = curl.fetch(rurl, {"user-agent": userAgent, returnText: false, location:true});
            printf("%d    - %s\n", res.status, rurl);
            if(res.status==200) {
                robotstxt[origin]=res.body;
            } else if (res.status > 399) {
                robotstxt[origin]=-1;
            } else {
                // there are other possibilities not covered here
                return {status: -1, statusText: statTexts[-1]};
            }
        }

        // if there is a robots.txt, and this url is disallowed, return status=1
        if(robotstxt[origin]!=-1 && !robots.isAllowed(userAgent, robotstxt[origin], url) ) {
            return {status: 1, statusText: statTexts[1]};
        }

        printf("fetching %s\r", url);
        fflush(stdout);
        res = curl.fetch (url, {"user-agent": userAgent, returnText: false, location:true});
        printf("%d    - %s\n", res.status, url);
        if(res.status > 499) res.status *= -1;  //return negative on serve error, so we will retry

        return res;
    }

Defining the Table
~~~~~~~~~~~~~~~~~~

Defining a schema for the table usually requires a bit of foresight to know
exactly the data you need saved, and how it will be accessed.  If we wanted
to be very flexible, we could include a JSON field in our table, but for the
purposes of this tutorial, that won't be necessary.

We know we will want a way to uniquely identify each entry.  Since each
entry corresponds to a specific URL, we will have that covered just by
including the URL of the article (which we would do in any case).  We also
want to save the site name and the status code returned from our ``fetch()``
function.  In addition, we will be extracting the article's title, a related
image URL and, of course, the article text.

Other items we may want in the future include the date we fetched the
article, the date of the article as returned by the server (we'll do both)
and the actual HTML of the article (which we will not do in this example). 
Saving the HTML in case we make a mistake or the format changes in the
future is a wise strategy for anything beyond a demo as it allows you to
correct a potentially bad situation without having to re-fetch many pages.

So we will make a schema, plus a couple of functions to create the table
and the indicies on them.

.. code-block:: javascript

    // Columns for our table
    // status, fetch_count, server_date, fetch_date, site, url, img_url, title, text
    var schema = "status smallint, fetch_count smallint, server_date date, fetch_date date, " +
                 "site varchar(8), url varchar(64), img_url varchar(64), title varchar(64), " +
                 "text varchar(1024)";

    function create_table() {
        // create using schema
        sql.exec(`create table pipages (${schema});`);
        // unique index on url, so we don't have duplicates
        sql.exec('create unique index pipages_url_ux on pipages(url);');
    }

    /*
        Regular indexes are updated on each insert/update.
        Text indexes, however, need to be manually updated.
          -  When a new row is inserted, it still is available for search,
             but that search is a linear scan of the document, so it is slower.
          -  A text index can be updated with either:
              1) sql.exec("alter index pipages_text_ftx OPTIMIZE");
                  or
              2) issuing the same command that created the index as in make_index() below.
             Here we will issue the same command when creating and updating for simplicity.
    */

    function make_index(){
        // we want to match "Check out the new pi 4." if we search for "pi 4"
        // so we add an expression for [\s\,]\d+[\s,\.] (in PERLRE), but only matching the number(s)
        sql.exec(
            "create fulltext index pipages_text_ftx on pipages(text) " +
            "WITH WORDEXPRESSIONS "+
            "('[\\uword]{2,99}', '[\\space\\,]\\P=\\digit+\\F[\\space,\\.]=') "+
            "INDEXMETER 'on'");

    }

We separate the creation of the ``fulltext`` index into its own function since it will be
run each time we update the content.

Parsing The Index Page HTML
~~~~~~~~~~~~~~~~~~~~~~~~~~~

We now need to fetch the use the data stored in ``sites`` to find links to
the articles and images in the index page of each site.  To that end, we
will use the following function to find the class of the element (or one of
its child elements) that contains the ``href`` and/or ``src`` of the
article URL or image URL we need.

Note that the extraction of the image URLs is perhaps not as tidy as we
might like.  MakeUseOf stores it in ``<source>`` tags, and for HackADay, it
is easier to get the image URL from the article itself.  So we will have to
make accommidations in our function.

.. code-block:: javascript

    // process the index page holding links to articles
    function procIndex(site, docBody) {
        var doc = html.newDocument(docBody);

        var entries = doc.findClass(site.entryClass);

        var images;
        if(site.entryImgClass) {
            images = doc.findClass(site.entryImgClass);
        }

        var insertRows = [];

        for (var i=0; i<entries.length; i++) {
            var row={site: site.name};
            var entry = entries.eq(i);
            var atag;
            if(entry.hasTag("a")[0])
                atag=entry;
            else
                atag = entry.findTag("a");

            row.url = atag.getAttr("href");

            if(row.url.length)
                row.url = urlutil.absUrl(site.url, row.url[0]);
            if(images && images.length) {
                var image = images.eq(i);
                if (image.length) {
                    var imgtag = image.findTag("source"); //makeuseof
                    if(imgtag.length) {
                        imgtag = imgtag.eq(0);
                        var srcset = imgtag.getAttr("data-srcset");
                        if(srcset[0]=='') {
                            srcset = imgtag.getAttr("srcset");
                        }
                        if(srcset.length)
                            row.img_url = srcset;
                    } else { //raspberrypi
                        if(image.hasTag("img")[0])
                            imgtag = image;
                        else
                            imgtag = image.findTag("img");

                        row.img_url = imgtag.getAttr("src");

                    }
                }
                if(row.img_url && row.img_url.length)
                    row.img_url = row.img_url[0];
            }
            
            insertRows.push(row);
        }
        return insertRows;
    }

What we get in return is a :green:`Array` of :green:`Objects`, each object
with properties that match the column names of our table.  Naming the
properties of the :green:`Objects` the same as the column names will make
for easy inserts and updates of records.

Inserting New URLs into the Table
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We have some of the columns for each row in the return value of
``procIndex()`` but there are several missing ones.  We will need to fill
those in with blanks that will be updated when we actually get the article. 
To do that, we will start with an empty :green:`Object`, add parameters and
blank values from ``empty_params``, then overwrite the empty values with the
parameters we in have ``data``, all using ``Object.assign()`` function.

.. code-block:: javascript

    //properties we need in the substitution parameters object when doing an insert
    var empty_params = {
        status:0,
        fetch_count:0,
        server_date:0,
        fetch_date:0,
        img_url:"",
        title:"",
        text:""
    }

    // insert a new row into our table
    function insert_row(data) {
        var dfilled = {};                     // start with empty object
        Object.assign(dfilled, empty_params); // add default empty params 
        Object.assign(dfilled, data);         // overwrite params with what we have

        var res = sql.exec("insert into pipages values " +
            "(?status, ?fetch_count, ?server_date, ?fetch_date, ?site, ?url, ?img_url, ?title, ?text);",
            dfilled
        );    

        // if duplicate, sql.errMsg will be set with "duplicate value" message and res.rowCount == 0
        // Checking res.rowCount first is likely faster
        if (!res.rowCount && sql.errMsg.includes("Trying to insert duplicate value") ) {
            res.isDup=true;
            // remove sql.errMsg
            sql.errMsg = '';
        }
        return res;
    }

The First Run - Grabbing the Index Pages
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The first time we run the script, we will need to create our database
and the unique index on ``url``, and then grab the index pages and
subsequent pages to populate our database with a fair number of
recent articles (50-60 each).

.. code-block:: javascript

    // get single key response without \n
    function getresp(def, len) {
        var l = (len)? len: 1;
        var ret = stdin.getchar(l);
        if(ret == '\n')
            return def;
        printf("\n");
        return ret.toLowerCase();
    }

    // drop table if exists, but ask first
    function clean_database() {
        if( sql.one("select * from SYSTABLES where NAME = 'pipages';") ) {
            printf('Table "pipages" exists. Drop it and delete all saved pages?\n [y/N]: ');
            fflush(stdout); //flush text after \n
            var resp = getresp('n'); //default no
            if(resp == 'n') {
                process.exit(0);
            }
            sql.exec("drop table pipages;");
            if(sql.errMsg != '') {
                printf("Error dropping table:\n%s",sql.errMsg);
                process.exit(0);
            }
        }
    }

    // on first run, get main index page and a few more index pages
    function first_run() {
        var i,j,k;

        clean_database();
        create_table();

        for (i=0; i< sitenames.length; i++) {
            var site = sites[sitenames[i]];
            var res;

            printf("Getting new article urls for %s\n", site.url);

            res = fetch(site.url);
            if(res.status != 200) {
                printf("error getting '%s':\n    %s\n", site.url, res.statusText);
                process.exit(1);
            }

            // extract urls of articles
            var urllist = procIndex(site, res.body);
            // insert urls into table
            for (j=0; j<urllist.length; j++) {
                insert_row(urllist[j]);
            }

            // go slow
            sleep(crawlDelay);

            // get second and subsequent pages up to site.initialPages
            for (k=2; k<=site.initialPages;k++) {
                var purl = sprintf(site.urlNextFmt, k);

                res = fetch(purl);
                if(res.status != 200) {
                    printf("error getting '%s':\n    %s\n", site.url, res.statusText);
                    process.exit(1);
                }

                // extract urls of articles
                var urllist = procIndex(site, res.body);
                // insert urls into table
                for (j=0; j<urllist.length; j++) {
                    insert_row(urllist[j]);
                }

                //sleep unless at end
                if( k < site.initialPages)
                    sleep(crawlDelay);
            }
        }
    }

Notice the ``crawlDelay`` variable.  The sites have allowed us to index their
content, however they may change their mind if they see in their logs that you
are grabbing multiple pages at once.  So it pays to be conservative.

Update Runs - Grabbing New Article URLs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here we will check, perhaps after a day or two, if there are any new
articles.  Very similar to above, we will fetch ``sites[i].url``
and extract URLs.  We'll also print out some extra information
about the links we get.

Also remember we created a unique index on ``url``, so we do not have to
worry about inserting the same url more than once.

.. code-block:: javascript

    // check for new articles in the main index page (sites[].url)
    function check_new_articles() {
        // check that our table exists
        if( ! sql.one("select * from SYSTABLES where NAME = 'pipages';") ) {
            console.log(
                'Error: table "pipages" does not exist.\n' +
                'If this is the first time running this script, run it with\n' +
                `${process.argv[1]} --first-run`
           );
            process.exit(1);
        }

        for (var i=0; i< sitenames.length; i++) {
            var site = sites[sitenames[i]];
            var res;

            printf("Getting new article urls for %s\n", site.url);

            res = fetch(site.url);

            var urllist = procIndex(site, res.body);
            for (var j=0; j<urllist.length; j++) {
                printf("checking     %s - \r", urllist[j].url)
                fflush(stdout);
                var sqlres = insert_row(urllist[j]);
                if(sqlres.isDup)
                    printf("exists:      %s\n", urllist[j].url);
                else
                    printf("NEW ARTICLE: %s\n", urllist[j].url);
            }
        }
    }

Updating Content
~~~~~~~~~~~~~~~~

Once we actually fetch and process articles, we will need to insert the
updated content into the table.  Since the results of each fetch will give
us a varying list of properties in ``data``, we will make a function that will create an
appropriate SQL statement for the update.

.. code-block:: javascript

    // update a row of data in our table
    function update_row(data) {
        var keys = Object.keys(data);

        // build our "set" section of sql statement
        // based on what we have in *data*
        // should look like "text=?text, title=?title, ..."
        var sqlstr="update pipages set ";
        var j=0;
        for (var i = 0; i<keys.length; i++) {
            var key = keys[i];
            if(key == 'url')
                continue;
            if(j)
                sqlstr += ", "+key + "=?" + key;
            else
                sqlstr += key + "=?" + key;
            j++;
        }

        // if we only have data.url, there's nothing to update
        if(!j)
            return;

        printf("updating %s\n",data.url);

        var res = sql.exec(sqlstr + " where url=?url;", data);

        if(sql.errMsg) {
            printf("sql update error, cannot continue:\n%s",sql.errMsg);
            process.exit(1);
        }
        return res;
    }


Processing the Articles
~~~~~~~~~~~~~~~~~~~~~~~

Once our Table has some URLs in it, we can go about the business of fetching
and processing them.  To fetch, we will continue to use our robots.txt
friendly ``fetch()`` function from within a new function called
``fetch_article()``.  That function will select articles to be fetched and
updated based on their ``status``.  

To process the HTML and extract the text, we will create a new function
called ``procpage()`` that will use the rampart-html module to extract
the title and text.

We will also put it all together with a
``setInterval()`` in order to respect ``crawlDelay`` per each site in a
function called ``fetch_all_articles()``.  That function will select
articles to be updated based on the ``status`` field.

.. code-block:: javascript

    // get relevant text and info from article html
    function procpage(site, dbrow, fetch_res) {
        var row = {
            url:dbrow.url, 
            fetch_date:'now', 
            fetch_count: dbrow.fetch_count +1
        };
        var image, imgtag;

        // get server date
        if(typeof fetch_res.headers.Date == 'string')
            row.server_date = scanDate(fetch_res.headers.Date, dateFmt);
        else if(typeof fetch_res.headers.date == 'string')
            row.server_date = scanDate(fetch_res.headers.date, dateFmt);
        else
            row.server_date = 'now';

        var doc = html.newDocument(fetch_res.body);
        // the content is located in an element with CSS class *site.contentClass*
        var content = doc.findClass(site.contentClass);

        // remove from content items we don't want
        if(site.contentRemoveClass) {
            for (var i=0;i<site.contentRemoveClass.length;i++) {
                content.findClass( site.contentRemoveClass[i] ).delete();
            }
        }

        // makeuseof has an easier to grab image in the article
        if(site.contentImgClass) {
            image=doc.findClass( site.contentImgClass );
            if(image.hasTag("img")[0])
                imgtag = image;
            else
                imgtag = image.findTag("img");

            if(imgtag.length)
                row.img_url = imgtag.getAttr("src")[0];
        }

        // extract the text from the content html
        row.text = content.toText({concatenate:true, titleText: true});

        // find the <title> tag text
        var title = doc.findTag('title');
        if(title.length)
            row.title = title.eq(0).toText()[0];

        return row;
    }

    // interval ID needed to cancel setInteral when all pages have been fetched;
    var iId = {};

    // status, fetch_count, server_date, fetch_date, site, url, img_url, text

    //fetch, process and update table
    function fetch_article(sitename) {
        var row;
        var res = sql.one("select * from pipages where site=? and status<1 and fetch_count<10 order by status DESC",
                    [sitename] );
        if(!res) {
            printf("%s is up to date\n", sitename);
            // no more pages to fetch, so cancel interval
            clearInterval(iId[sitename]);
            delete iId[sitename]; // get rid of entry so we know we are done with it
            // final action before script exits:
            if(Object.keys(iId).length == 0) {
                printf("updating fulltext index\n");
                make_index();
            }
            return;
        }

        var site = sites[res.site];

        // fetch the page
        var cres = fetch(res.url);
        if(cres.status != 200 ) {
            // failed
            update_row({
                url:res.url,
                status: cres.status,
                fetch_count: res.fetch_count + 1
            });            
        } else {
            // success
            row=procpage(site, res, cres);
            row.status=200;
            update_row(row);
        }
    }

    // get article pages asynchronously using setInterval
    function fetch_all_articles(){
        for (var i = 0; i < sitenames.length; i++) {
            var site = sites[sitenames[i]];

            var res = sql.one("select * from pipages where site=? and status<1 and fetch_count<10 order by status DESC",
                        [sitenames[i]] );

            if (res) { // only if we have some sites, otherwise we'd waste crawlDelay seconds just to exit

                // this complicated mess is to make sure our *sitenames[i].name*
                // is scoped to the setInterval callback and stays the same 
                // even as the *site* variable changes in subsequent *for* loops.

                // if you are unfamiliar with this technique and immediately invoked functions - see:
                // https://www.google.com/search?q=iife+settimeout

                // scope sitename in an IIFE
                (function(sn) {
                    iId[sn] = setInterval(
                        function() { fetch_article(sn);}, 
                        crawlDelay * 1000
                    );
                })(site.name);

                /* or use IIFE to return a function with sitename scoped. Result is the same.
                iId[site.name] = setInterval(
                    (function(sn) { 
                        return function() {
                            fetch_article(sn);
                        }
                    })(site.name),
                    crawlDelay * 1000
                );
                */
            }
        }
    }


The Final Aggregator Script
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Putting everything above together, we end up with this script:

.. code-block:: javascript

    #!/usr/local/bin/rampart

    rampart.globalize(rampart.utils);

    var Sql    = require("rampart-sql");
    var curl   = require("rampart-curl");
    var html   = require("rampart-html");
    var robots = require("rampart-robots");
    var urlutil= require("rampart-url");
    var dbdir  = process.scriptPath + "/web_server/data/pi_news"

    var sql = new Sql.init(dbdir, true);

    // The date format we are expecting from http servers
    var dateFmt = "%a, %d %b %Y %H:%M:%S GMT";

    var crawlDelay = 10;  // go slow - delay in seconds between fetches from same site.

    // Columns for our table
    // status, fetch_count, server_date, fetch_date, site, url, img_url, title, text
    var schema = "status smallint, fetch_count smallint, server_date date, fetch_date date, " +
                 "site varchar(8), url varchar(64), img_url varchar(64), title varchar(64), " +
                 "text varchar(1024)";

    // running in two stages - first gets all articles from several index pages
    //                       - second gets latest articles from main index page
    // First stage is done by running with command line argument '--first-run'
    var firstRun = false;
    if ( process.argv[2] == '--first-run' )
        firstRun=true;

    /* details of each scrape:
        url                - the index page with links to the latest articles
        urlNextFmt         - more index pages with links to articles
        initialPages       - how many pages with links to articles to grab on first run
        entryClass         - the CSS class of the element holding article links, for index pages
        entryImgClass      - the CSS class of the element on the index pages with a related image
        contentClass       - on article page, the CSS of element that holds most relevant text
        contentImgClass    - the CSS class of the element on the content pages with a related image
        contentRemoveClass - the CSS class of elements on the content pages inside contentClass that should be removed
    */

    var sites = {
        "hackaday":
        {
            name: "hackaday",
            url: "https://hackaday.com/category/raspberry-pi-2/",
            urlNextFmt: "https://hackaday.com/category/raspberry-pi-2/page/%d/",
            initialPages: 8,        
            entryClass: "entry-featured-image",
            contentImgClass: "entry-featured-image",
            contentClass: 'post'
        },
        "raspberrypi":
        {
            name: "raspberrypi",
            url: "https://www.raspberrypi.com/news/",
            urlNextFmt: "https://www.raspberrypi.com/news/page/%d/",
            initialPages: 5,
            entryClass: "c-blog-post-card__link",
            entryImgClass: "c-blog-post-card__image",
            contentClass: "c-blog-post-content",
            contentRemoveClass: ["c-blog-post-content__footer"]
        },
        "makeuseof":
        {
            name: "makeuseof",
            url: "https://www.makeuseof.com/search/raspberry%20pi/",
            urlNextFmt: "https://www.makeuseof.com/search/raspberry%%20pi/%d/",
            initialPages: 3,
            entryClass: "bc-title-link",
            entryImgClass: "bc-img",
            contentClass: "article",
            contentRemoveClass: ["sidebar", "next-btn", "sharing", "letter-from"]
        }
    }

    // the names of our sites.
    var sitenames = Object.keys(sites);

    // status < 0 -- try or retry
    // status > 0 -- don't retry
    // status = 0 -- first try
    // status > 0 and < 100 - custom codes
    var statTexts = {
    "1": "Disallowed by robots.txt",
    "2": "Cannot parse Url",
    "-1": "Error retrieving robots.txt"
    }

    var robotstxt = {}; //load these once per run
    var userAgent = "rampart_tutorial";  //our identification

    // fetch with robots.txt check
    function fetch(url) {
        var comp = urlutil.components(url);
        var origin, res;
        if(!comp)
            return {status: 2, statusText: statTexts[2]};

        origin=comp.origin;

        if(!robotstxt[origin]) {
            var rurl = origin + '/robots.txt'
            printf("fetching %s\r", rurl);
            fflush(stdout);

            // body is a buffer.  robots.isAllowed also takes a buffer, so we can dispense with text.
            res = curl.fetch(rurl, {"user-agent": userAgent, returnText: false, location:true});
            printf("%d    - %s\n", res.status, rurl);
            if(res.status==200) {
                robotstxt[origin]=res.body;
            } else if (res.status > 399) {
                robotstxt[origin]=-1;
            } else {
                // there are other possibilities not covered here
                return {status: -1, statusText: statTexts[-1]};
            }
        }

        // if there is a robots.txt, and this url is disallowed, return status=1
        if(robotstxt[origin]!=-1 && !robots.isAllowed(userAgent, robotstxt[origin], url) ) {
            return {status: 1, statusText: statTexts[1]};
        }

        printf("fetching %s\r", url);
        fflush(stdout);
        res = curl.fetch (url, {"user-agent": userAgent, returnText: false, location:true});
        printf("%d    - %s\n", res.status, url);
        if(res.status > 499) res.status *= -1;  //return negative on serve error, so we will retry

        return res;
    }

    // process the index page holding links to articles
    function procIndex(site, docBody) {
        var doc = html.newDocument(docBody);

        var entries = doc.findClass(site.entryClass);

        var images;
        if(site.entryImgClass) {
            images = doc.findClass(site.entryImgClass);
        }

        var insertRows = [];

        for (var i=0; i<entries.length; i++) {
            var row={site: site.name};
            var entry = entries.eq(i);
            var atag;
            if(entry.hasTag("a")[0])
                atag=entry;
            else
                atag = entry.findTag("a");

            row.url = atag.getAttr("href");

            if(row.url.length)
                row.url = urlutil.absUrl(site.url, row.url[0]);
            if(images && images.length) {
                var image = images.eq(i);
                if (image.length) {
                    var imgtag = image.findTag("source"); //makeuseof
                    if(imgtag.length) {
                        imgtag = imgtag.eq(0);
                        var srcset = imgtag.getAttr("data-srcset");
                        if(srcset[0]=='') {
                            srcset = imgtag.getAttr("srcset");
                        }
                        if(srcset.length)
                            row.img_url = srcset;
                    } else { //raspberrypi
                        if(image.hasTag("img")[0])
                            imgtag = image;
                        else
                            imgtag = image.findTag("img");

                        row.img_url = imgtag.getAttr("src");

                    }
                }
                if(row.img_url && row.img_url.length)
                    row.img_url = row.img_url[0];
            }
            
            insertRows.push(row);
        }
        return insertRows;
    }

    // get single key response without \n
    function getresp(def, len) {
        var l = (len)? len: 1;
        var ret = stdin.getchar(l);
        if(ret == '\n')
            return def;
        printf("\n");
        return ret.toLowerCase();
    }

    // drop table if exists, but ask first
    function clean_database() {
        if( sql.one("select * from SYSTABLES where NAME = 'pipages';") ) {
            printf('Table "pipages" exists. Drop it and delete all saved pages?\n [y/N]: ');
            fflush(stdout); //flush text after \n
            var resp = getresp('n'); //default no
            if(resp == 'n') {
                process.exit(0);
            }
            sql.exec("drop table pipages;");
            if(sql.errMsg != '') {
                printf("Error dropping table:\n%s",sql.errMsg);
                process.exit(0);
            }
        }
    }

    function create_table() {
        // create using schema
        sql.exec(`create table pipages (${schema});`);
        // unique index on url, so we don't have duplicates
        sql.exec('create unique index pipages_url_ux on pipages(url);');
    }

    // update a row of data in our table
    function update_row(data) {
        var keys = Object.keys(data);

        // build our "set" section of sql statement
        // based on what we have in *data*
        // should look like "text=?text, title=?title, ..."
        var sqlstr="update pipages set ";
        var j=0;
        for (var i = 0; i<keys.length; i++) {
            var key = keys[i];
            if(key == 'url')
                continue;
            if(j)
                sqlstr += ", "+key + "=?" + key;
            else
                sqlstr += key + "=?" + key;
            j++;
        }

        // if we only have data.url, there's nothing to update
        if(!j)
            return;

        printf("updating %s\n",data.url);

        var res = sql.exec(sqlstr + " where url=?url;", data);

        if(sql.errMsg) {
            printf("sql update error, cannot continue:\n%s",sql.errMsg);
            process.exit(1);
        }
        return res;
    }

    //properties we need in the substitution parameters object when doing an insert
    var empty_params = {
        status:0,
        fetch_count:0,
        server_date:0,
        fetch_date:0,
        img_url:"",
        title:"",
        text:""
    }

    // insert a new row into our table
    function insert_row(data) {
        var dfilled = {};                     // start with empty object
        Object.assign(dfilled, empty_params); // add default empty params 
        Object.assign(dfilled, data);         // overwrite params with what we have

        var res = sql.exec("insert into pipages values " +
            "(?status, ?fetch_count, ?server_date, ?fetch_date, ?site, ?url, ?img_url, ?title, ?text);",
            dfilled
        );    

        // if duplicate, sql.errMsg will be set with "duplicate value" message and res.rowCount == 0
        // Checking res.rowCount first is likely faster
        if (!res.rowCount && sql.errMsg.includes("Trying to insert duplicate value") ) {
            res.isDup=true;
            // remove sql.errMsg
            sql.errMsg = '';
        }
        return res;
    }

    // check for new articles in the main index page (sites[].url)
    function check_new_articles() {
        // check that our table exists
        if( ! sql.one("select * from SYSTABLES where NAME = 'pipages';") ) {
            console.log(
                'Error: table "pipages" does not exist.\n' +
                'If this is the first time running this script, run it with\n' +
                `${process.argv[1]} --first-run`
           );
            process.exit(1);
        }

        for (var i=0; i< sitenames.length; i++) {
            var site = sites[sitenames[i]];
            var res;

            printf("Getting new article urls for %s\n", site.url);

            res = fetch(site.url);

            var urllist = procIndex(site, res.body);
            for (var j=0; j<urllist.length; j++) {
                printf("checking     %s - \r", urllist[j].url)
                fflush(stdout);
                var sqlres = insert_row(urllist[j]);
                if(sqlres.isDup)
                    printf("exists:      %s\n", urllist[j].url);
                else
                    printf("NEW ARTICLE: %s\n", urllist[j].url);
            }
        }
    }

    // on first run, get main index page and a few more index pages
    function first_run() {
        var i,j,k;

        clean_database();
        create_table();

        for (i=0; i< sitenames.length; i++) {
            var site = sites[sitenames[i]];
            var res;

            printf("Getting new article urls for %s\n", site.url);

            res = fetch(site.url);
            if(res.status != 200) {
                printf("error getting '%s':\n    %s\n", site.url, res.statusText);
                process.exit(1);
            }

            // extract urls of articles
            var urllist = procIndex(site, res.body);
            // insert urls into table
            for (j=0; j<urllist.length; j++) {
                insert_row(urllist[j]);
            }

            // go slow
            sleep(crawlDelay);

            // get second and subsequent pages up to site.initialPages
            for (k=2; k<=site.initialPages;k++) {
                var purl = sprintf(site.urlNextFmt, k);

                res = fetch(purl);
                if(res.status != 200) {
                    printf("error getting '%s':\n    %s\n", site.url, res.statusText);
                    process.exit(1);
                }

                // extract urls of articles
                var urllist = procIndex(site, res.body);
                // insert urls into table
                for (j=0; j<urllist.length; j++) {
                    insert_row(urllist[j]);
                }

                //sleep unless at end
                if( k < site.initialPages)
                    sleep(crawlDelay);
            }
        }
    }

    // get relevant text and info from article html
    function procpage(site, dbrow, fetch_res) {
        var row = {
            url:dbrow.url, 
            fetch_date:'now', 
            fetch_count: dbrow.fetch_count +1
        };
        var image, imgtag;

        // get server date
        if(typeof fetch_res.headers.Date == 'string')
            row.server_date = scanDate(fetch_res.headers.Date, dateFmt);
        else if(typeof fetch_res.headers.date == 'string')
            row.server_date = scanDate(fetch_res.headers.date, dateFmt);
        else
            row.server_date = 'now';

        var doc = html.newDocument(fetch_res.body);
        // the content is located in an element with CSS class *site.contentClass*
        var content = doc.findClass(site.contentClass);

        // remove from content items we don't want
        if(site.contentRemoveClass) {
            for (var i=0;i<site.contentRemoveClass.length;i++) {
                content.findClass( site.contentRemoveClass[i] ).delete();
            }
        }

        // makeuseof has an easier to grab image in the article
        if(site.contentImgClass) {
            image=doc.findClass( site.contentImgClass );
            if(image.hasTag("img")[0])
                imgtag = image;
            else
                imgtag = image.findTag("img");

            if(imgtag.length)
                row.img_url = imgtag.getAttr("src")[0];
        }

        // extract the text from the content html
        row.text = content.toText({concatenate:true, titleText: true});

        // find the <title> tag text
        var title = doc.findTag('title');
        if(title.length)
            row.title = title.eq(0).toText()[0];

        return row;
    }

    /*
        Regular indexes are updated on each insert/update.
        Text indexes, however, need to be manually updated.
          -  When a new row is inserted, it still is available for search,
             but that search is a linear scan of the document, so it is slower.
          -  A text index can be updated with either:
              1) sql.exec("alter index pipages_text_ftx OPTIMIZE");
                  or
              2) issuing the same command that created the index as in make_index() below.
             Here we will issue the same command when creating and updating for simplicity.
    */

    function make_index(){
        // we want to match "Check out the new pi 4." if we search for "pi 4"
        // so we add an expression for [\s\,]\d+[\s,\.] (in PERLRE), but only matching the number(s)
        sql.exec(
            "create fulltext index pipages_text_ftx on pipages(text) " +
            "WITH WORDEXPRESSIONS "+
            "('[\\uword]{2,99}', '[\\space\\,]\\P=\\digit+\\F[\\space,\\.]=') "+
            "INDEXMETER 'on'");

    }

    // interval ID needed to cancel setInteral when all pages have been fetched;
    var iId = {};

    // status, fetch_count, server_date, fetch_date, site, url, img_url, text

    //fetch, process and update table
    function fetch_article(sitename) {
        var row;
        var res = sql.one("select * from pipages where site=? and status<1 and fetch_count<10 order by status DESC",
                    [sitename] );
        if(!res) {
            printf("%s is up to date\n", sitename);
            // no more pages to fetch, so cancel interval
            clearInterval(iId[sitename]);
            delete iId[sitename]; // get rid of entry so we know we are done with it
            // final action before script exits:
            if(Object.keys(iId).length == 0) {
                printf("updating fulltext index\n");
                make_index();
            }
            return;
        }

        var site = sites[res.site];

        // fetch the page
        var cres = fetch(res.url);
        if(cres.status != 200 ) {
            // failed
            update_row({
                url:res.url,
                status: cres.status,
                fetch_count: res.fetch_count + 1
            });            
        } else {
            // success
            row=procpage(site, res, cres);
            row.status=200;
            update_row(row);
        }
    }

    // get article pages asynchronously using setInterval
    function fetch_all_articles(){
        for (var i = 0; i < sitenames.length; i++) {
            var site = sites[sitenames[i]];

            var res = sql.one("select * from pipages where site=? and status<1 and fetch_count<10 order by status DESC",
                        [sitenames[i]] );

            if (res) { // only if we have some sites, otherwise we'd waste crawlDelay seconds just to exit

                // this complicated mess is to make sure our *sitenames[i].name*
                // is scoped to the setInterval callback and stays the same 
                // even as the *site* variable changes in subsequent *for* loops.

                // if you are unfamiliar with this technique and immediately invoked functions - see:
                // https://www.google.com/search?q=iife+settimeout

                // scope sitename in an IIFE
                (function(sn) {
                    iId[sn] = setInterval(
                        function() { fetch_article(sn);}, 
                        crawlDelay * 1000
                    );
                })(site.name);

                /* or use IIFE to return a function with sitename scoped. Result is the same.
                iId[site.name] = setInterval(
                    (function(sn) { 
                        return function() {
                            fetch_article(sn);
                        }
                    })(site.name),
                    crawlDelay * 1000
                );
                */
            }
        }

    }

    // get index pages
    if(firstRun) {
        first_run();
    } else {
        check_new_articles();
    }

    // get article pages
    fetch_all_articles();

Client-Side Script
------------------

The client-side script is fairly easy and self explanatory compared to the
aggregator script.  Here we edit the ``search.js`` file and add some
styling using Bootstrap:

.. code-block:: javascript

    /* The PI NEWS Demo Search Module

       Everything outside the 'search()' function below is run once (per thread) when
       the module is loaded.  The 'module.exports=search` is the exported function
       which is run once per client request.

       Note that this means that variables set outside the 'search()' function are 
       long lived and span multiple requests by potentially multiple clients.  As
       such, care should be taken that these variables do not include any information
       which should not be shared between clients.
    */

    // Load the sql module.  This only needs to be done once
    var Sql=require("rampart-sql");

    // process.scriptPath is the path of the web_server_conf.js, not
    // the path of this module script. For the path of this module, 
    // use 'module.path'.
    var db=process.scriptPath + '/data/pi_news';

    // Open the db.
    var sql=new Sql.init(db);

    // make printf = rampart.utils.printf
    // See: https://rampart.dev/docs/rampart-main.html#rampart-globalize
    rampart.globalize(rampart.utils);

    /* 
    Example of some settings to modify search weights which can be used to use
    to tune the search results.  These values are just examples and are not
    tuned for this search.
    See: https://rampart.dev/docs/sql-set.html#rank-knobs
    and  https://rampart.dev/docs/sql-set.html#other-ranking-properties
        sql.set({
            "likepallmatch":false,
            "likepleadbias":250,
            "likepproximity":750,
            "likeprows":2000,
        });

    NOTE ALSO:  
         Here the 'sql' variable is set when the module is loaded.  Any changes
         made in 'search()' below using sql.set() will be carried forward for
         the next client (per server thread).  If you have settings (as set in
         'sql.set()') that change per request or per client, it is highly
         advisable that you call 'sql.reset()' at the beginning of the exported
         'search()' function below followed by a `sql.set()` call to make the
         behavioral changes desired.
         
         If the sql.set() call is intended for every client and every search,
         setting it here is not problematic.
    */

    // we want "pi 4" query to match "Check out the new pi 4."
    // so we set qminwordlen to 1, the shortest word in a query (normally 2).
    // We also set wordexpressions to:
    //      ('[\\uword]{2,99}', '[\\space\\,]\\P=\\digit+\\F[\\space,\\.]=')
    // in the scraping script so that single digits will be indexed.
    // In another context, this might be counter productive as it polutes the index, but
    // here we have a small table and a greater need to match single digits. 
    sql.set({ "qminwordlen":1 });

    /* 
     the top section of html only needs to be set once as it remains the same
     regardless of the request.  Here it is set when the module is first loaded. 
     This is a printf style format string so that the query text box may be
     filled if, e.g., ?q=pi+4 is set.

     The sprintf(%w', format_string): 
        This removes leading white space so it can be pretty 
        here in the source but compact when sent. 
        See https://rampart.dev/docs/rampart-utils.html#printf
    */
    var htmltop_format=sprintf('%w',
    `<!DOCTYPE HTML>
        <html>
        <head>
          <meta charset="utf-8">
          <meta name="viewport" content="width=device-width, initial-scale=1">
          <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-1BmE4kWBq78iYhFldvKuhfTAU6auU8tT94WrHftjDbrCEXSU1oBoqyl2QvZ6jIW3" crossorigin="anonymous">
          <style>
            body {font-family: arial,sans-serif;}
            .urlsp {color:#006621;max-width:100%%;overflow: hidden;text-overflow: ellipsis;white-space:nowrap;display:inline-block;font-size:.90em;}
            .urla {text-decoration: none;font-size:16px;overflow: hidden;text-overflow: ellipsis;white-space:nowrap;display:inline-block; width: 100%%; }
            .res {margin-top: 80px !important;}
            .img-cover{object-fit: cover; aspect-ratio:5/3}
          </style>
        </head>
        <body>
        <nav style="z-index:1" class="navbar position-fixed top-0 w-100 navbar-expand-lg navbar-light bg-light">
          <div class="container-fluid">
            <a class="navbar-brand m-auto p-2" href="#">
              【Ｒａｍｐａｒｔ : Ｐｉ Ｎｅｗｓ】
            </a>
            <form style="width:100%%" id="mf" action="/apps/pi_news/search.html">
              <div class="input-group mb-2">
                <input autocomplete="off" type="text" id="fq" name="q" value="%H" placeholder="Search" class="form-control">
                <button class="btn btn-outline-secondary" type="submit">Search</button>
              </div>
            </form>
          </div>
        </nav>
    `
    );



    function search(req) {
        var q=req.query.q ? req.query.q: "";

        // req.query.skip in, e.g. "/apps/pi_news/search.html?q=pi&skip=10" is text.
        // Make it a JavaScript number.
        var skip=parseInt( req.query.skip );

        var icount=0;  //estimated total number of results, set below
        var endhtml;   // closing tags, set below
        var nres=12;   // number of results per page

        // add the htmltop text to the server's output buffer.
        // See: https://rampart.dev/docs/rampart-server.html#req-printf
        // it includes escaped '%%' values and the 'value="%H"' format code for the query
        req.printf(htmltop_format, q);

        if (!skip)skip=0;

        var sqlStatement;
        // if there is a query, search for it and format the results.
        // if not, just send the latest articles.

        if(req.query.q) {
            /* The SQL statement:
           
               %mbH in stringformat() means highlight with bold and html escape. 
               See: https://rampart.dev/docs/rampart-sql.html#metamorph-hit-mark-up
                    https://rampart.dev/docs/sql-server-funcs.html#stringformat

               abstract(text[, maxsize[, style[, query]]]) will create an abstract:  
                  - text is the table field from which to create an abstract.
                  - 0 (or <0) means use the default maximum size of 230 characters.
                  - 'querymultiple' is a style which will break up the abstract into multiple sections if necessary
                  - '?' is replaced with the JavaScript variable 'q'
            */
            sqlStatement = "select url, img_url, title, stringformat('%mbH',?query,abstract(text, 0,'querymultiple',?query)) Ab from pipages where text likep ?query";
        } else {
            /* if no query, get latest articles */
            sqlStatement = "select url, img_url, title, abstract(text) Ab from pipages order by server_date DESC";
        }

        // by default, only the first 100 rows are returned for any likep search.
        // if we are skipping past that, we need to raise the likeprows setting.
        if(skip + nres > 100 )
            sql.set({likeprows:skip + nres});
        else
            sql.set({likeprows:100}); //reset to default in case previously set
        // sql.exec(statement, params, settings, callback);
        sql.exec(
            sqlStatement,
            // the parameters for each '?query' in the above statement
            {query: q},

            // options
            {maxRows:nres,skipRows:skip,includeCounts:true},

            // callback is executed once per retrieved row.
            function(res,i,cols,info) {
                /* res = {url: www, img_url:xxx, title:"yyy", Ab:"zzz"}
                 * i = current row, beginning at skip, ending at or before skip + nres
                 * cols = ["url", "img_url", "title", "Ab"] - the columns returned from the SQL statement
                 * includeCounts sets info to an object detailing the number of possible matches to a "likep" query. */

                // before the first row
                if(i==skip) {
                    icount=parseInt(info.indexCount);
                    req.printf('<div class="res m-3">');

                    if(req.query.q)
                        req.printf('<div class="m-5 mb-0">Results %d-%d of about %d</div>',
                            skip+1,(skip+nres>icount)?icount:skip+nres,icount
                        );
                    else
                        req.printf('<div class="m-5 mb-0">Latest Articles</div>');

                    req.printf('<div class="row row-cols-md-3 row-cols-sm-2 row-cols-1 g-4 m-3 mt-0">');
                }

                req.printf('<div class="col"><div class="card">'+
                               '<a target="_blank" href="%s">'+
                                 '<img class="card-img-top img-cover" src = "%s">' +
                               '</a>' +
                               '<div class="card-body">'+
                                 '<a class="urla tar" target="_blank" href="%s">%s</a>'+
                                  '<span class="urlsp">%s</span>'+
                                  '<p class="card-text">%s</p>'+
                               '</div>'+
                            '</div></div>',
                res.url, res.img_url, res.url, res.title, res.url, res.Ab); 
            }
        );

        // check if there are more rows.  If so, print a 'next' link.
        if (icount > nres+skip) {
            skip+=nres
            // %U is for url encoding.  See https://rampart.dev/docs/rampart-utils.html#printf
            req.printf('</div><div class="m-3 mt-0">' +
                          '<a class="m-3" href="/apps/pi_news/search.html?q=%U&skip=%d">Next %d</a>' +
                       '<div class="m-3"> </div></div>',
                req.query.q,skip, (nres > icount - skip ? icount - skip: nres)
            );
        }
        endhtml='</div></div></body></html>';


        // send the closing html and set the  mime-type to text/html
        // This is appended to everything already sent using req.printf()
        return({html:endhtml});

        // alternatively, it might be sent like this:
    //    req.put(endhtml);
    //    return({html:null}); //null means just set the mime-type, but don't append

    }

    //export the main search function
    module.exports=search;

    // -fin-



Improvements
------------

We purposely kept these examples very simple in order to clearly demonstrate
the concepts we covered, so there is room for improvement:

*  Save the HTML of each article in our table.
*  Include a more robust treatment of HTTP status codes.  
*  Error check the results of the parsed properties before insert or update.
*  Optimize the update of the fulltext index with a conditional, e.g.,
   ``ALTER INDEX pipages_text_ftx OPTIMIZE HAVING COUNT(NewRows) > 30;`` so
   that there is a balance between the time it takes to optimize the index
   and number of rows that will be linearly search.  This would only be an
   issue after the database grows much larger than anticipated in this
   tutorial.
*  Create a better strategy should the fetch of ``/robots.txt`` not return ``200``. 
*  Examine ``sql.errMsg`` after each call to ``sql.exec()``.
*  Have a different format for the latest articles and the search results (so that search
   results look more like the return from a search).

Enjoy!
