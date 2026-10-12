Find Cities and their Closest Neighbors
=======================================

Preface
-------

In this tutorial, we will use the :ref:`SQL module <sqltoc:The rampart-sql module>`
and the :ref:`Server module <rampart-server:The rampart-server HTTP module>`, along 
with some client side scripting to create a search of city names, which also
lists the nearest cities to the selected city. 

You can download the complete project from our 
`Tutorial Repository <https://github.com/aflin/rampart_tutorials>`_
in the `citysearch directory <https://github.com/aflin/rampart_tutorials/tree/main/citysearch>`_\ .

License
~~~~~~~

The code related to this tutorial is released under the MIT license.
The data used to populate the database is licensed under a 
`Creative Commons License <https://creativecommons.org/licenses/by/4.0/>`_\ .

Concepts
--------

This module will demonstrate:

*  Using the :ref:`SQL module <sqltoc:The rampart-sql module>` to 
   :ref:`import <rampart-sql:importCsvFile()>` csv data.
*  Using the :ref:`SQL module <sqltoc:The rampart-sql module>` to create a Fulltext search for
   the client side autocomplete search.  
*  Using the :ref:`SQL geocode functions <sql-server-funcs:Geographical coordinate functions>` 
   to encode latitude/longitude pairs for each entry in the database, and to
   find the closest cities to any given latitude/longitude pair.

In order to complete this tutorial, you should have basic knowledge of
JavaScript, SQL and client side scripting using JavaScript (a passing
knowledge of jQuery is also helpful).  This tutorial will not provide an
in-depth explanation of how client-side scripting works.

Getting Started
---------------

In order to search by name and by location, we need to create a database
that contains both place names and latitude/longitude information. 
Fortunately, the good folks at `GeoNames.org <http://www.geonames.org/>`_
and `OpenDataSoft <https://public.opendatasoft.com/>`_
have available a CSV formatted file that accomplishes that and which is
licensed under a 
`Creative Commons License <https://creativecommons.org/licenses/by/4.0/>`_\ .

You can navigate to 
`<https://public.opendatasoft.com/explore/dataset/geonames-all-cities-with-a-population-1000/export/>`_
and choose the CSV file.  

Your first task will be to create a directory structure for this project.

Web Server Layout
~~~~~~~~~~~~~~~~~

In the Rampart binary distribution is a sample web server tree.  For our
purposes here, we assume you have downloaded and unzipped the Rampart binary
distribution into a directory named ``~/downloads/rampart``. We will use
that for this project.  
:ref:`The rampart-server HTTP module <rampart-server:The rampart-server HTTP module>`
is configured and loaded from the included ``web_server/web_server_conf.js``
script.  It defines ``web_server/html`` as the default directory for static
html, ``web_server/apps`` as the default directory for scripts and
``web_server/data`` as a standard location for databases.

To get started, copy the ``web_server`` directory to a convenient place for
this project. Also, for our purposes, we do not need
anything in the ``web_server/apps/test_modules`` or
``web_server/apps/wsapps`` directories, so you can delete the copy of those
files. We will also add an empty file for our import script and web interface at 
``web_server/apps/citysearch.js``.

::

    user@localhost:~$ mkdir citysearch
    user@localhost:~$ cd citysearch
    user@localhost:~/citysearch$ cp -a ~/downloads/rampart/web_server ./
    user@localhost:~/citysearch$ cd web_server/
    user@localhost:~/citysearch/web_server$ rm -rf apps/test_modules wsapps/* html/index.html
    user@localhost:~/citysearch/web_server$ touch ./apps/citysearch.js
    user@localhost:~/citysearch/web_server$ find .
    user@localhost:~/citysearch/web_server$ curl -o data/geonames-all-cities-with-a-population-1000.csv \
       'https://public.opendatasoft.com/explore/dataset/geonames-all-cities-with-a-population-1000/download/?format=csv&timezone=America/Los_Angeles&lang=en&use_labels_for_header=true&csv_separator=%3B'
    ./web_server
    ./web_server/apps
    ./web_server/apps/citysearch.js
    ./web_server/html
    ./web_server/html/images
    ./web_server/html/images/inigo-not-found.jpg
    ./web_server/data
    ./web_server/data/geonames-all-cities-with-a-population-1000.csv
    ./web_server/logs
    ./web_server/start_web_server.sh
    ./web_server/wsapps
    ./web_server/web_server_conf.js

Importing the Data
------------------

Examine your CSV
~~~~~~~~~~~~~~~~

The first step for importing a CSV file is to have a look at the format of the
data in the file.  CSV can vary quite a bit, and to import it, we will want to 
know a few things such as (but not limited to):

*  Does every column contain the same type?
*  Are text columns quoted?
*  Are single quotes present inside unquoted text fields?
*  Are records separated by ``,`` or by something else?
*  Is there a header row as the first line in the file?

Let's examine some rows of ``geonames-all-cities-with-a-population-1000.csv``:

::

    $ head -n 5 geonames-all-cities-with-a-population-1000.csv
    Geoname ID;Name;ASCII Name;Alternate Names;Feature Class;Feature Code;Country Code;Country name EN;Country Code 2;Admin1 Code;Admin2 Code;Admin3 Code;Admin4 Code;Population;Elevation;DIgital Elevation Model;Timezone;Modification date;LABEL EN;Coordinates
    8396129;Sanjiang;Sanjiang;Sanjiang,Sanjiang Jiedao,Sanjiang Qu,san jiang,san jiang jie dao,san jiang qu,三江,三江区,三江街道;P;PPLA3;CN;China;;01;3402;;;0;;14;Asia/Shanghai;2021-09-19;China;31.34813,118.36132
    8405692;Xinmin;Xinmin;Xinmin,Xinmin Zhen,xin min,xin min zhen,新民,新民镇;P;PPLA4;CN;China;;33;8739734;;;28033;;402;Asia/Shanghai;2022-04-12;China;30.39759,107.3895
    8416824;Jindaoxia;Jindaoxia;Jindaoxia,Jindaoxia Zhen,jin dao xia,jin dao xia zhen,金刀峡,金刀峡镇;P;PPLA4;CN;China;;33;8739734;;;13752;;323;Asia/Shanghai;2022-04-01;China;30.00528,106.65187
    8420197;Jianlong;Jianlong;Jianlong,Jianlong Xiang,jian long,jian long xiang,健龙,健龙乡;P;PPLA4;CN;China;;33;8739734;;;18151;;276;Asia/Shanghai;2022-04-01;China;29.3586,106.18522

    $ grep "United States" geonames-all-cities-with-a-population-1000.csv | head -n 5
    5095312;Atlantic Highlands;Atlantic Highlands;Atlantic Highlands,Atlantik Khajlands,Portland Point,Portland Poynt,atlantyk haylndz  nywjrsy,Атлантик Хайландс,Атлантик Хајландс,آتلانتیک هایلندز، نیوجرسی;P;PPL;US;United States;;NJ;025;02110;;4311;11;14;America/New_York;2017-05-23;United States;40.40789,-74.03431
    5095335;Avon-by-the-Sea;Avon-by-the-Sea;Avon,Avon-by-the-Sea,Ehvan-baj-zeh-Si,Key East,aywn-bay-d-sy  nywjrsy,Эван-бай-зэ-Си,ایون-بای-د-سی، نیوجرسی;P;PPL;US;United States;;NJ;025;02440;;1794;4;6;America/New_York;2017-05-23;United States;40.19234,-74.01597
    5095561;Belmar;Belmar;BLM,Behlmar,Belmar,Ocean Beach,blmar  nywjrsy,Белмар,Бэлмар,بلمار، نیوجرسی;P;PPL;US;United States;;NJ;025;04930;;5712;4;6;America/New_York;2017-05-23;United States;40.17845,-74.0218
    5096289;Carlstadt;Carlstadt;Carlstadt,Karlshtad,Karlstadt,Tailor Town,karlastadt  nywjrsy,Карлстадт,Карлштад,کارلاستادت، نیوجرسی;P;PPL;US;United States;;NJ;003;10480;;6279;55;55;America/New_York;2017-05-23;United States;40.84038,-74.0907
    5097627;Elmwood Park;Elmwood Park;Dundee Lake,East Paterson,Ehlmvud Park,Elmvud Park,Elmwood Park,almwwd park  nywjrsy,Елмвуд Парк,Элмвуд Парк,الموود پارک، نیوجرسی;P;PPL;US;United States;;NJ;003;21300;;20279;14;14;America/New_York;2017-05-23;United States;40.90399,-74.11848


Immediately we can see that records are separated by semi-colons (``;``), that the
"Admin1" column has both number and text types and that text fields are not quoted.  
Also the first row appears to be column names, and not data.

Next let's check if there are embedded single quotes in text fields:

::

    $ grep "'" geonames-all-cities-with-a-population-1000.csv | head -n 2
    8992324;Nu’erbage;Nu'erbage;Hetian Shi,Nu'erbage,Nu'erbage Jiedao,Nu’erbage,Nu’erbage Jiedao,he tian shi,nu er ba ge,nu er ba ge jie dao,努尔巴格,努尔巴格街道,和田市;P;PPLA3;CN;China;;13;6532;;;0;;1379;Asia/Urumqi;2021-09-20;China;37.1134,79.91034
    12450872;Yingye'er;Yingye'er;;P;PPLA4;CN;China;;13;6532;;;11485;;1315;Asia/Urumqi;2022-03-31;China;37.37312,79.77745

And indeed there are single quotes in city names.

So to answer our earlier questions:

*  Does every column contain the same type? -- :red:`NO`
*  Are text columns quoted?  -- :red:`NO`
*  Are single quotes present inside unquoted text fields? -- :green:`YES`
*  Are records separated by ``,``?  -- :red:`NO`, :green:`Uses semicolons`
*  Is there a header row as the first line in the file? :green:`YES`

Note that although the file is separated by semi-colons, we will continue to use the term
``CSV``.

Armed with this knowledge, we are ready to create a script that imports our data.

Creating the table
~~~~~~~~~~~~~~~~~~

The data will need to be imported in two stages.  Stage one will be as-is from the
CSV into a temporary table.  Stage two will combine City, Admin1 and Country
names into one field, separate and convert "Latitude,Longitude" to Numbers
and compute a geocode we will use later for a bounded area search.

So let's create a script that will make our table by opening 
``~/citysearch/web_server/apps/citysearch.js`` in your text editor.

To begin, we need to load the SQL module and open a database;

.. code-block:: javascript

   var Sql = require("rampart-sql");

   var sql = new Sql.init("~/citysearch/web_server/data/cities", true);


In the above code, the ``var Sql = require("rampart-sql");`` line
loads the SQL module that is distributed with Rampart as 
``rampart-sql.so``.  The second line, 
``var sql = new Sql.init("~/citysearch/web_server/data/cities", true);`` opens the database.

Note the ``true`` in ``Sql.init()``.  It signifies that if the database
does not exist, create the directory and the metadata files necessary
for a new, blank database.

When creating a new database, be sure that:

*  The directory does not (yet) exist (it will be created).  If 
   it exists and does not contain the metadata files, the opening
   of the database will fail.

*  The parent directory (in this case ``~/citysearch/web_server/data/``) 
   **does** exist, and that you have read/write permissions.

So now that we have the code to open, and optionally create our
database, let's make our table.

.. code-block:: javascript

    function create_tmp_table() {
        sql.exec("create table cities_tmp (" +
                "Geoname_ID              varchar(8), " +
                "Name                    varchar(8), " +
                "ASCII_Name              varchar(8), " +
                "Alternate_Names         varchar(8), " +
                "Feature_Class           varchar(8), " +
                "Feature_Code            varchar(8), " +
                "Country_Code            varchar(8), " +
                "Country_name_EN         varchar(8), " +
                "Country_Code_2          varchar(8), " +
                "Admin1_Code             varchar(8), " +
                "Admin2_Code             varchar(8), " +
                "Admin3_Code             varchar(8), " +
                "Admin4_Code             varchar(8), " +
                "Population              int, "        +
                "Elevation               int, "        +
                "Digital_Elevation_Model int, "        +
                "Timezone                varchar(8), " +
                "Modification_date       varchar(8), " +
                "LABEL_EN                varchar(8), " +
                "Coordinates             varchar(8)"   +
                ");"  ); 
    }

    create_tmp_table();

This should all be self-explanatory.  If not, please brush up on
your `SQL <https://www.w3schools.com/sql/sql_create_table.asp>`_\ .

Note though that ``varchar(x)`` in Texis SQL, the size ``x`` is
merely a suggestion.  If the text put into this field is larger
than what is specified, it will not truncate the text or affect any indexing.

Now we have our table.  Let's get that data in!

Importing the CSV
~~~~~~~~~~~~~~~~~

The Settings
^^^^^^^^^^^^

The SQL module has a convenient function to import CSV data.  It has many options
and often takes careful examination of the data to be imported to get it correct.
Fortunately the large number of options allows us to get well formatted
CSV data imported correctly.

See the section for :ref:`importCsvFile() <rampart-sql:importCsvFile()>` for a full
listing.

Knowing what we discovered when we examined the ``allCountries.txt`` file above, we
can make our ``importCsvFile()`` call look like this:

.. code-block:: javascript

    var csvFile = "../data/geonames-all-cities-with-a-population-1000.csv";

    function import_csv() {
        total = sql.importCsvFile(
            csvFile,  //file to import
            {
                tableName:       'cities_tmp',
                singleQuoteNest: false,
                hasHeaderRow:    true,
                delimiter:       ';',
                normalize:       false,
            }
        );
        printf('\n%d rows in total.\n',total);
    }

So the :green:`Object` of settings passed to ``importCsvFile()`` addresses the answers to all our
questions above (``singleQuoteNest: false`` is because there are single quotes present AND they
are not quoted in double quotes -- one setting for those two questions).

We are ready to import the data.  But before we do, we know this will take a bit of time.
Let's set up a function to monitor the progress so we aren't staring at a blank screen
wondering when the import will be finished.

Monitoring the Import
^^^^^^^^^^^^^^^^^^^^^

There are two major stages at which we can monitor the import.  The first
is while ``importCsvFile()`` is analysing the data, and the second is
as the data is being inserted into the SQL table.

In the first, analysis stage, a monitoring function is passed two parameters:
``monitor_import(count, stage)``.  The analysis takes at least two passes.
If ``normalize``, is set ``true``, two more passes are added for each column.

Note that the first stage is significantly faster than the second.  Therefore if your
dataset is not very large, you might want to skip reporting on the progress
of the first page.

In the second, insert stage, a monitoring function is passed only one parameter:
``monitor_import(count)``.  There is only one pass for the second stage.

Knowing this, we can write a single function to print out our progress, which may be used
at either of or both of the major stages.

.. code-block:: javascript

    // cuz no one likes writing out 'rampart.utils.printf()'
    rampart.globalize(rampart.utils);

    var total=-1; //we won't know the total until we finish the first pass of importCsvFile
    var step = 100; //set in importCsvFile(), only report every 100th row

    /* a single function to monitor the import for both pre-processing (progressFunc)
       and import (callback function supplied to sql.importCsvFile as a parameter)   */
    function monitor_import(count, stg) {
        var stage = "Import";

        if(count==0)
            printf("\n");

        if(stg!==undefined) // progressfunc
            stage=stg;

        if(stg === 0) //differentiate between 0 and undefined
        {
            total=count; //update our total in the first stage.
            printf("Stage: %s, Count: %d       \r", stage, count);
        } else {
            printf("Stage: %s, Count: %d of %d      \r", stage, count, total);
        }
        fflush(stdout);
    }

Transforming the Data
^^^^^^^^^^^^^^^^^^^^^

At this point we can import the data into the temporary table.  Now
it needs to be transformed into its final form and put in the final
table.

So we will use the following to create the table and transform
the data.

.. code-block:: javascript

    function create_final_table() {
        sql.exec("create table cities (" +
                "id                      counter, "    +
                "place                   varchar(8), " +
                "alt_names               varchar(8), " +
                "population              int, "        +
                "latitude                double, "     +
                "longitude           double, "     +
                "geocode             long, "       +
                "timezone                varchar(8), " +
                "country                 varchar(8) "  +
                ");"  ); 
    }

    function makerow(o) {
        var ret={}, tmp;

        ret.place = sprintf('%s, %s %s(%s)', o.Name, o.Admin1_Code, o.Country_name_EN, o.Country_Code);
        ret.altNames = o.Alternate_Names;
        ret.population = o.Population;
        tmp = o.Coordinates.split(',');
        ret.lat = parseFloat(tmp[0]);
        ret.lon = parseFloat(tmp[1]);
        ret.tz = o.Timezone;
        ret.country = o.Country_name_EN;
        return ret;
    }

    function build_final_table() {
        printf("sorting rows\n");
        sql.exec("select * from cities_tmp order by Population DESC",
            function(res,i) {

                if(!i) printf("done\nCreating Final Table\n");

                var vals = makerow(res);
                sql.exec("insert into cities values( " +
                    "counter, ?place, ?altNames, ?population, ?lat, ?lon, latlon2geocode(?lat, ?lon), ?tz, ?country );",
                    vals );
                if (! (i % 100) ) {
                    printf("%d of %d\r", i, total);
                    fflush(stdout);
                }
            },
            {maxRows:-1}
        );
        printf('\n');
    }

    create_final_table();
    build_final_table();

The ``create_final_table()`` function is easily understood.  

In the ``makerow()`` function, we take a single row from the temporary table
and create the placename, separate the coordinates and convert them to
numbers and add the other columns we want to keep.

In the ``build_final_table()`` function, we select one row at a time,
transform the row by passing it to ``makerow()`` and insert it into our
final table.  Rows are returned from the ``cities_tmp`` table ordered by
population so that we do not need to sort them in the web application.

In addition, we will calculate the geocode necessary
to do bounded geographical searches and insert it into the ``geocode``
column. 

According to the :ref:`documentation <sql-server-funcs:latlon2geocode, latlon2geocodearea>`:

::

   The latlon2geocode function encodes a given latitude/longitude coordinate
   into one long return value.  This encoded value – a “geocode” value – can
   be indexed and used with a special variant of Texis’ BETWEEN operator for
   bounded-area searches of a geographical region.  

That is exactly what we need to efficiently search for other places close to a given one.

Putting it Together
^^^^^^^^^^^^^^^^^^^

Putting this all together, and using the ``callbackStep`` and ``progressStep``
settings, we end up with this:

.. code-block:: javascript

    function create_tmp_table() {
        sql.exec("create table cities_tmp (" +
                "Geoname_ID              varchar(8), " +
                "Name                    varchar(8), " +
                "ASCII_Name              varchar(8), " +
                "Alternate_Names         varchar(8), " +
                "Feature_Class           varchar(8), " +
                "Feature_Code            varchar(8), " +
                "Country_Code            varchar(8), " +
                "Country_name_EN         varchar(8), " +
                "Country_Code_2          varchar(8), " +
                "Admin1_Code             varchar(8), " +
                "Admin2_Code             varchar(8), " +
                "Admin3_Code             varchar(8), " +
                "Admin4_Code             varchar(8), " +
                "Population              int, "        +
                "Elevation               int, "        +
                "Digital_Elevation_Model int, "        +
                "Timezone                varchar(8), " +
                "Modification_date       varchar(8), " +
                "LABEL_EN                varchar(8), " +
                "Coordinates             varchar(8)"   +
                ");"  ); 
    }


    var total=-1; //we won't know the total until we finish the first pass of importCsvFile
    var step = 100; //set in importCsvFile(), only report every 100th row

    /* a single function to monitor the import for both pre-processing (progressFunc)
       and import (callback function supplied to sql.importCsvFile as a parameter)   */
    function monitor_import(count, stg) {
        var stage = "Import";

        if(count==0)
            printf("\n");

        if(stg!==undefined) // progressfunc
            stage=stg;

        if(stg === 0) //differentiate between 0 and undefined
        {
            total=count; //update our total in the first stage.
            printf("Stage: %s, Count: %d       \r", stage, count);
        } else {
            printf("Stage: %s, Count: %d of %d      \r", stage, count, total);
        }
        fflush(stdout);
    }

    function import_csv() {
        total = sql.importCsvFile(
            csvFile,  //file to import
            {
                tableName:       'cities_tmp',
                singleQuoteNest: false,
                hasHeaderRow:    true,
                delimiter:       ';',
                normalize:       false,
                callbackStep:    step, //callback run every 100th row
                progressStep:    step, //progressfunc run every 100th row for each stage
                progressFunc:    monitor_import //progress function while processing csv 
            },
            monitor_import //callback function upon actual import
        );
        printf('\n%d rows in total.\n',total);
    }

    function create_final_table() {
        sql.exec("create table cities (" +
                "id                      counter, "    +
                "place                   varchar(8), " +
                "alt_names               varchar(8), " +
                "population              int, "        +
                "latitude                double, "     +
                "longitude           double, "     +
                "geocode             long, "       +
                "timezone                varchar(8), " +
                "country                 varchar(8) "  +
                ");"  ); 
    }

    function makerow(o) {
        var ret={}, tmp;

        ret.place = sprintf('%s, %s %s(%s)', o.Name, o.Admin1_Code, o.Country_name_EN, o.Country_Code);
        ret.altNames = o.Alternate_Names;
        ret.population = o.Population;
        tmp = o.Coordinates.split(',');
        ret.lat = parseFloat(tmp[0]);
        ret.lon = parseFloat(tmp[1]);
        ret.tz = o.Timezone;
        ret.country = o.Country_name_EN;
        return ret;
    }

    function build_final_table() {
        printf("sorting rows\n");
        sql.exec("select * from cities_tmp order by Population DESC",
            function(res,i) {

                if(!i) printf("done\nCreating Final Table\n");

                var vals = makerow(res);
                sql.exec("insert into cities values( " +
                    "counter, ?place, ?altNames, ?population, ?lat, ?lon, latlon2geocode(?lat, ?lon), ?tz, ?country );",
                    vals );
                if (! (i % 100) ) {
                    printf("%d of %d\r", i, total);
                    fflush(stdout);
                }
            },
            {maxRows:-1}
        );
        printf('\n');
    }

    create_tmp_table();
    import_csv();
    create_final_table();
    build_final_table();

Building Indexes
~~~~~~~~~~~~~~~~

The geocode index
^^^^^^^^^^^^^^^^^

We need to create an index to speed up the bounded "geocode" search we will
use in our web application. 
And we should do that in our script.  And once again, we are going to wrap
it in a function.

.. code-block:: javascript

    function make_geocode_index() {
        printf("creating index on geocode\n");
        sql.exec("create index cities_geocode_x on cities(geocode) WITH INDEXMETER 'on';");
    }

A couple of things to note:

First -- when this index is made, it will be backed by a
file named ``cities_geocode_x.btr``.  It is so named because it will be
easy to find using ``ls -l`` (it will come right before the table, named
``cities.tbl``, and it lets you know the field indexed (``_geocode``) and
the type of index (``_x`` for plain index, i.e.  - not Fulltext or unique). 
Your methodology for naming indexes is not as important as making sure you
are consistent, can read it and know what the index is for without having to
resort to looking it up.

However if you do not know what an index is for, you can always look it up in the System
Catalog:

::

    $ tsql -d ../data/cities/ "select * from SYSINDEX where NAME='cities_geocode_x'"

        NAME        TBNAME       FNAME       COLLSEQ        TYPE      NON_UNIQUE     FIELDS       PARAMS
    ------------+------------+------------+------------+------------+------------+------------+------------+
    cities_geocode_x cities       cities_geocode_x A            B                      01 geocode      stringcomparemode=unicodemulti,respectcase;

Second --  note the ``WITH INDEXMETER 'on'`` in the SQL statement.  This will tell
Texis to print a pretty meter to let you know the progress of your index creation.

The id index
^^^^^^^^^^^^

Eventually, when we write our web application script, we will want to look up records based on the
``id counter`` field.  So we will make a function to make an index on that as well.  

.. code-block:: javascript

    function make_id_index(){
        printf("creating index on id\n");
        sql.exec("create index cities_id_x on cities(id) WITH INDEXMETER 'on';");
    }


The Fulltext index
^^^^^^^^^^^^^^^^^^

When making a Fulltext index, often it is best to leave most settings as is.  The one exception
is the :ref:`rex <rampart-sql:rex()>` expressions used to define what is a word. 
Often we want to make sure we include utf-8 encoded text.

The default is ``[\uword]{2,99}``: two to ninety-nine Unicode letters,
digits or combining marks — whole utf-8 characters in any script.  That
already handles our names of places from all over the world.  However
some place names, notably in CJK scripts, are a single character, and
the default's two-character minimum would leave them out of the index.

To change the expression used during Fulltext index creation, we can use
:ref:`delExp <sql-set:delExp>` and :ref:`addExp <sql-set:addExp>`.  However
as a shortcut, we can specify the expressions that will be used in the SQL
statement itself by utilizing the ``WITH`` keyword:

``WITH WORDEXPRESSIONS ('[\\uword]{1,99}')``

Now we also have a database that does not contain normal text.  It is worth
thinking about where this might bite us when we perform a search.  Let's
look at the :ref:`list of noise words <sql-set:noiseList>`.  The excluded
``us``, ``to`` and ``in`` look suspect.  They are the same as some of our
country codes.  And ``or`` is an ``Admin1`` abbreviation for Oregon.

Normally when searching normal English text, removing all the noise words
would hurt performance.  However, in this very specific circumstance, noise
words serve no purpose.  Further, they will exclude some codes we want in
our index.  So we will delete the noiseList.

You should always have a very good reason for altering or deleting the
noiseList.  For English text, there is rarely a need to do so and it can
have adverse consequences on performance and quality of results.  However, 
this time we do have a good reason.

Now - what exactly do we want to index?  Naturally we want to be able to
look up a place based upon the ``place`` column.  So we will create one
index on it.  However we also have alternate names, which we will want
to search if there are no matches from ``place``.  Thus we will
create a separate index on ``alt_names`` as well.

So, let's see our SQL statements to create the Fulltext indexes in its own function:

.. code-block:: javascript
    
    function make_text_indexes() {
        printf("creating indexes on place names\n");

        // noiselist as detailed at https://rampart.dev/docs/sql-set.html#noiselist
        // This is not English text and some geographic abbreviations like OR IN DO TO SO and US
        // are also on the noise words list.  Setting to empty will allow such words in the index.
        sql.set({ noiseList:[]});

        // make compact index.  Sorting by population, not by likep rank.  See like3 search below.
        sql.exec("create fulltext index cities_place_ftx on cities(place)"+
            " WITH WORDEXPRESSIONS ('[\\uword]{1,99}') INDEXMETER 'on' WORDPOSITIONS 'off';");

        sql.exec("create fulltext index cities_altNames_ftx on cities(alt_names)"+
            " WITH WORDEXPRESSIONS ('[\\uword]{1,99}') INDEXMETER 'on' WORDPOSITIONS 'off';");
    }

Note that we also have ``WORDPOSITIONS 'off'``.  This omits the position of
the indexed words and creates a smaller index.  Normally we would want this
information in order to rank by relevance, taking into account word
proximity, order and placement in the document.  However in our web
application we will be ordering by population, so this information is not
needed.

See :ref:`the documentation <rampart-sql:Fulltext Indexes>` for more information.

The Final Import Script
~~~~~~~~~~~~~~~~~~~~~~~

We put it all together, wrap it in a function, and it looks something like this:

.. code-block:: javascript

    // cuz no one likes writing out 'rampart.utils.printf()'
    rampart.globalize(rampart.utils);
    
    var Sql = require("rampart-sql");

    var sql = new Sql.init("~/citysearch/web_server/data/cities", true);

    var csvFile = "../data/geonames-all-cities-with-a-population-1000.csv";

    function import_data(){
        function create_tmp_table() {
            sql.exec("create table cities_tmp (" +
                    "Geoname_ID              varchar(8), " +
                    "Name                    varchar(8), " +
                    "ASCII_Name              varchar(8), " +
                    "Alternate_Names         varchar(8), " +
                    "Feature_Class           varchar(8), " +
                    "Feature_Code            varchar(8), " +
                    "Country_Code            varchar(8), " +
                    "Country_name_EN         varchar(8), " +
                    "Country_Code_2          varchar(8), " +
                    "Admin1_Code             varchar(8), " +
                    "Admin2_Code             varchar(8), " +
                    "Admin3_Code             varchar(8), " +
                    "Admin4_Code             varchar(8), " +
                    "Population              int, "        +
                    "Elevation               int, "        +
                    "Digital_Elevation_Model int, "        +
                    "Timezone                varchar(8), " +
                    "Modification_date       varchar(8), " +
                    "LABEL_EN                varchar(8), " +
                    "Coordinates             varchar(8)"   +
                    ");"  ); 
        }


        var total=-1; //we won't know the total until we finish the first pass of importCsvFile
        var step = 100; //set in importCsvFile(), only report every 100th row

        /* a single function to monitor the import for both pre-processing (progressFunc)
           and import (callback function supplied to sql.importCsvFile as a parameter)   */
        function monitor_import(count, stg) {
            var stage = "Import";

            if(count==0)
                printf("\n");

            if(stg!==undefined) // progressfunc
                stage=stg;

            if(stg === 0) //differentiate between 0 and undefined
            {
                total=count; //update our total in the first stage.
                printf("Stage: %s, Count: %d       \r", stage, count);
            } else {
                printf("Stage: %s, Count: %d of %d      \r", stage, count, total);
            }
            fflush(stdout);
        }

        function import_csv() {
            total = sql.importCsvFile(
                csvFile,  //file to import
                {
                    tableName:       'cities_tmp',
                    singleQuoteNest: false,
                    hasHeaderRow:    true,
                    delimiter:       ';',
                    normalize:       false,
                    callbackStep:    step, //callback run every 100th row
                    progressStep:    step, //progressfunc run every 100th row for each stage
                    progressFunc:    monitor_import //progress function while processing csv 
                },
                monitor_import //callback function upon actual import
            );
            printf('\n%d rows in total.\n',total);
        }

        function create_final_table() {
            sql.exec("create table cities (" +
                    "id                      counter, "    +
                    "place                   varchar(8), " +
                    "alt_names               varchar(8), " +
                    "population              int, "        +
                    "latitude                double, "     +
                    "longitude           double, "     +
                    "geocode             long, "       +
                    "timezone                varchar(8), " +
                    "country                 varchar(8) "  +
                    ");"  ); 
        }

        function makerow(o) {
            var ret={}, tmp;

            ret.place = sprintf('%s, %s %s(%s)', o.Name, o.Admin1_Code, o.Country_name_EN, o.Country_Code);
            ret.altNames = o.Alternate_Names;
            ret.population = o.Population;
            tmp = o.Coordinates.split(',');
            ret.lat = parseFloat(tmp[0]);
            ret.lon = parseFloat(tmp[1]);
            ret.tz = o.Timezone;
            ret.country = o.Country_name_EN;
            return ret;
        }

        function build_final_table() {
            printf("sorting rows\n");
            sql.exec("select * from cities_tmp order by Population DESC",
                function(res,i) {

                    if(!i) printf("done\nCreating Final Table\n");

                    var vals = makerow(res);
                    sql.exec("insert into cities values( " +
                        "counter, ?place, ?altNames, ?population, ?lat, ?lon, latlon2geocode(?lat, ?lon), ?tz, ?country );",
                        vals );
                    if (! (i % 100) ) {
                        printf("%d of %d\r", i, total);
                        fflush(stdout);
                    }
                },
                {maxRows:-1}
            );
            printf('\n');
        }

        function make_geocode_index() {
            printf("creating index on geocode\n");
            sql.exec("create index cities_geocode_x on cities(geocode) WITH INDEXMETER 'on';");
        }

        function make_id_index(){
            printf("creating index on id\n");
            sql.exec("create index cities_id_x on cities(id) WITH INDEXMETER 'on';");
        }

        function make_text_indexes() {
            printf("creating indexes on place names\n");

            // noiselist as detailed at https://rampart.dev/docs/sql-set.html#noiselist
            // This is not English text and some geographic abbreviations like OR IN DO TO SO and US
            // are also on the noise words list.  Setting to empty will allow such words in the index.
            sql.set({ noiseList:[]});

            // make compact index.  Sorting by population, not by likep rank.  See like3 search below.
            sql.exec("create fulltext index cities_place_ftx on cities(place)"+
                " WITH WORDEXPRESSIONS ('[\\uword]{1,99}') INDEXMETER 'on' WORDPOSITIONS 'off';");

            sql.exec("create fulltext index cities_altNames_ftx on cities(alt_names)"+
                " WITH WORDEXPRESSIONS ('[\\uword]{1,99}') INDEXMETER 'on' WORDPOSITIONS 'off';");
        }

        function drop_tmp_table() {
            sql.exec("drop table cities_tmp");
        }

        create_tmp_table();
        import_csv();
        create_final_table();
        build_final_table();
        make_geocode_index();
        make_id_index();
        make_text_indexes();
        drop_tmp_table();

    }

    import_data();


Web Server Script
-----------------

Web Server Script Mapping
~~~~~~~~~~~~~~~~~~~~~~~~~

We will start by adding to the ``apps/citysearch.js`` file in order to map 
functions to urls for use with :ref:`Server module <rampart-server:The rampart-server HTTP module>`.  
So we add the following stub script to what we have above to get started:

.. code-block:: javascript

    ...

    // var to hold the client-side javascript
    var client_script;

    // var to hold a format string containing the top of the page
    // html and a place for the query string.
    var pageTopFmt;

    // the bottom of our html page
    var pageBottom;

    // function stubs
    function htmlpage(req) {
    }

    function autocomp(req) {
    }


    // module and module.exports are set when called from the webserver
    if(module && module.exports) {
        // url to function mapping
        module.exports= {
            "/":               htmlpage,  //http://localhost:8088/apps/citysearch/
            "/index.html":     htmlpage,  //http://localhost:8088/apps/citysearch/index.html
            "/autocomp.json":  autocomp,  //http://localhost:8088/apps/citysearch/autocomp.json
        }
    } else {
        // called from the command line.  Build the database.
        import_data();
    }


Notice that ``module.exports`` is set to an :green:`Object`.  This allows a
single module script to serve pages at multiple URLs.  Here ``/`` and
``/index.html`` will be used to return html and ``/autocomp.json``
will be used for AJAX requests and will return JSON.

Delivering Static Content
~~~~~~~~~~~~~~~~~~~~~~~~~

Looking at the stub script above, there are two distinct times that the code
therein will be run: 1) once upon module load and 2) upon each request from
a client as mapped in the ``module.exports`` :green:`Object`.  For security
purposes, it is important to keep this in mind as you write scripts. 
Variables set in the module outside of the ``htmlpage()``, and
``autocomp()`` functions are long lived and span multiple requests.

That being said, we can set a few variables in the script to deliver
the static content.  They only need to be loaded once as they contains the
HTML and client side JavaScript that will be delivered to every client, and
they do not change.

.. code-block:: javascript

    // the autocomplete plugin from  https://github.com/devbridge/jQuery-Autocomplete
    // jquery and plugin included from cloudflare in <script src="xyz"> tags below in pageTopFmt.
    var client_script = `
    $(document).ready(function(){
        $('#cstextbox').autocomplete(
            {
                serviceUrl: '/apps/citysearch/autocomp.json',
                minChars: 2,
                autoSelectFirst: true,
                showNoSuggestionNotice: true,
                triggerSelectOnValidInput: false,
                onSelect: function(sel) { window.location.assign("./?id="+sel.id); }
            }
        );

        $('#cstextbox').on('keypress', function(e){
            var key = e.charCode || e.keyCode || 0;
            if (key == 13) {       // on <return> don't submit form
                e.preventDefault();
                return false;
            }
        });
    });
    `;

    // pageTopFmt is defined once upon script load here rather than upon each request in 
    // htmlpage() below. format code %w removes leading white space.
    var pageTopFmt=sprintf('%w',`<!DOCTYPE HTML>
    <html>
        <head><meta charset="utf-8">
        <script src="https://cdnjs.cloudflare.com/ajax/libs/jquery/3.5.1/jquery.min.js"></script>
        <script src="https://cdnjs.cloudflare.com/ajax/libs/jquery.devbridge-autocomplete/1.4.11/jquery.autocomplete.min.js"></script>
        <style>
            body,h1,h2,h3,h4,h5,h6 {font-family: "Varela Round", Sans-Serif;}
            .autocomplete-suggestions {border: 1px solid #999; background: #FFF; overflow: auto; width: auto !important; padding-right:5px;}
            .autocomplete-suggestion { padding: 2px 5px; white-space: nowrap; overflow: hidden; }
            .autocomplete-suggestions strong {font-weight: normal; color: #3399FF; }
            .autocomplete-group strong { display: block; border-bottom: 1px solid #000; }
            .autocomplete-selected { background: #F0F0F0; }
            .autocomplete-group { padding: 2px 5px; }
            #main {background-color: white;margin: auto;min-height: 300px;width: 600px;}
            #idiv { width:500px;height:39px;border-bottom: lightGray 1px solid;padding:15px 0px 15px 0px;}
            #cstextbox {min-width:150px;width:100%%;height:30px;font:normal 18px arial,sans-serif;padding: 1px 3px;border: 2px solid #ccc;box-sizing: bord
        </style>
        <title>City Search Tutorial</title>
        </head>
        <body>
        <div id="main">
          <form id="mf">
              <div id="idiv">
                  <input type="text" id="cstextbox" name="q" value="%s" placeholder="Search for a city">
              </div>
          </form>
          <div id="res">`);

    var pageBottom = sprintf(`</div></body><script>
    %w
    </script></html>`, client_script);
    

The variable ``pageTopFmt`` is set to a basic web page with a form and text box for
searching.  The format code ``"%s'`` in the input text box will be filled
with the current query.  Note because this is a format string, all other
literal percent signs (``%``) must be escaped as ``%%``.

In ``pageBottom, we will include our client-side JavaScript from the
variable ``client_script``.

Note the use of the format code ``%w``.  This removes leading white space
so that our source can be indented, but we don't send unneeded white space
to the client.

We will use the text input in the form to search for cities and load the
nearest cities, displaying the results in the ``<div id="res"></div>`` div
at the bottom of the page.

Next we need to fill in our actual functions that deliver the HTML as well
as JSON via AJAX for a type-ahead search.

The Nearest City Results
~~~~~~~~~~~~~~~~~~~~~~~~

The main page is delivered via normal HTTP request and returns results
formatted as HTML.  It will display the nearest 30 cities to the one set in
the query string parameter ``id``.

To do so, we need to construct an appropriate SQL statement that
looks up our city by ``id``.

.. code-block:: sql

    SELECT place, latitude, longitude
    FROM cities WHERE 
    id=?;

After retrieving the latitude and longitude of the current city,
we will need another SQL statement to find the closest cities to
it.  Here, ``?lat``/``?lon`` correspond to the latitude and longitude
retrieved from the above SQL statement, while ``latitude``/``longitude``
correspond to the latitude and longitude in the currently selected
row.

.. code-block:: sql

    SELECT
    place, id, latitude, longitude, population,
    DISTLATLON(?lat, ?lon, latitude, longitude) dist,
    AZIMUTH2COMPASS( AZIMUTHLATLON(?lat, ?lon, latitude, longitude), 3 ) heading
    FROM cities WHERE 
    geocode BETWEEN (SELECT LATLON2GEOCODEAREA(?lat, ?lon, 1.0))
    ORDER BY 6 ASC;

Taking it line by line:

``SELECT`` - We are looking up and returning rows in the table.

``place`` - Our preformatted place name.

``id, latitude, longitude, population`` - Other columns we need.

``DISTLATLON(?lat, ?lon, latitude, longitude) dist`` - The distance from our
current city to the one in this row.

``AZIMUTH2COMPASS( AZIMUTHLATLON(?lat, ?lon, latitude, longitude), 3 ) heading``
-- The heading (direction) from our current city to the one in this row.

``FROM cities WHERE`` - The name of the table, and ``WHERE`` for the
search on the next line.

``geocode BETWEEN (SELECT LATLON2GEOCODEAREA(?lat, ?lon, 1.0))`` - select
only rows within a certain dist from ``lat``/``lon`` (one degree).

``ORDER BY 6 ASC`` -- order by the 6th selected field in the SQL statement. In
this case that is ``dist``.

With that, we can write the function to find the closest cities to
``?id=<cityid>`` and format the results in HTML.

.. code-block:: javascript

    var useKilometers = true;

    var distconv = 1;
    var distvar = "miles";

    if(useKilometers) {
        distconv=1.60934;
        distvar = "kilometers";
    }

    function htmlpage(req) {
        var id = req.params.id, lat, lon;

        // check if we already have a place id.
        if(id){
            id_res= sql.one("SELECT place, latitude, longitude " + 
                "FROM cities WHERE id=?;",
                [req.params.id]
            );
            // yes, then set lat,lon vars
            if(id_res) {
                lon=id_res.longitude;
                lat=id_res.latitude;
            }
        } else {
            // no, just print the blank search form
            req.printf(pageTopFmt,'');  // add top of page to return buffer without a query.
            return({html:pageBottom});  // add bottom of page, return with 'content-type:text/html'
        } 

        // what to do if the query_string id is not found in the db
        if(!lon || !lat) {
            req.printf(pageTopFmt,'');
            req.printf('No entry for id "%s".', id);
            return({html:pageBottom});
        }

        /* here we select rows based on their distance from the place specified by 'id',
           calculate the distance and direction between id and the selected city,
           then sort by the distance from 'id' (field 6 in our sql statement) */
        res = sql.exec(`SELECT
            place, id, latitude, longitude, population,
            DISTLATLON(?lat, ?lon, latitude, longitude) dist,
            AZIMUTH2COMPASS( AZIMUTHLATLON(?lat, ?lon, latitude, longitude), 3 ) heading
            FROM cities WHERE geocode BETWEEN (SELECT LATLON2GEOCODEAREA(?lat, ?lon, 1.0))
            ORDER BY 6 ASC;`,
            {lat:lat, lon:lon},
            {maxRows: 31}, // first row is same city
            function(res, i) { // foreach city retrieved:
                if(!i) {
                    // this is our 'id' city, as it is closest to itself.
                    req.printf(pageTopFmt,res.place);
                    req.printf('<h3 style="margin-bottom:0px">%s</h3><ul style="margin-top:0px">',res.place);
                } else {
                    // all other nearby cities we will print the direction and distance:
                    req.printf('<a href="?id=%s">%s</a><br><ul>' +
                        '<li>Direction:  %.2f %s to the %s</li>',
                        res.id, res.place, res.dist * distconv, distvar, res.heading);
                }
                // some useful information to go along with the city name
                req.printf("<li>Population: %s</li>" +
                    '<li>Location: <a target="_blank" href="https://maps.google.com/maps?z=11&q=%U&ll=%f,%f">' +
                    'google maps (%.4f,%.4f)</a></li></ul>',
                    Sql.stringFormat('%ki', res.population), res.place, res.latitude, res.longitude , res.latitude, res.longitude);

                if(!i) req.put('<hr><h3>Closest Cities:</h3>');
            }
        );
        return {html:pageBottom}; //pageBottom is added to same buffer as is used with req.printf()
    }

Also added is the variable ``useKilometers``, which will
flag the conversion of miles to kilometers.


The Auto-Complete Results
~~~~~~~~~~~~~~~~~~~~~~~~~

The ajax portion of the script allows for suggestions/type-ahead
to be displayed in the HTML text box, making it easier to find
a city name.  This is done over an AJAX request, returning JSON to the
``jQuery-Autocomplete`` script included in the HTML from CloudFlare.

First step will be to construct our SQL statement and query in order
to get a list of cities that are closest to a city or lat/lon requested.

When we made our Fulltext indexes on our table, we used the field ``place``
for the primary search and ``alt_names`` for a follow-up search, if
necessary.

Our first search uses the following SQL

.. code-block:: sql

    SELECT place value, id, latitude, longitude, population 
    FROM cities WHERE
    place LIKE3 ? ; 

Taking it line by line:

``SELECT`` - We are looking up and returning rows in the table.

``place`` - Our preformatted place name.

``id, latitude, longitude, population`` - Other columns we need.

``FROM cities WHERE`` - The name of the table, and ``WHERE`` for the
search on the next line.

``place``
- Specifying the field upon which the Fulltext index is build.

``LIKE3`` - This signifies a Fulltext search where word positions are
**not** significant.  Usually ``LIKEP`` will be your most used type of
``like`` search for normal Fulltext queries.  However, here we made an index
without word positions and are pre-sort by population.  Thus ``LIKE3`` is
appropriate.  See :ref:`The documentation <rampart-sql:Fulltext Indexes>`
for more information.

The ``?`` corresponds to a variable we will give ``sql.exec()``, as explained below.

The second SQL statement will be similar except it searches against the ``alt_names`` 
column should no match be found in the first query.

Finally the results are formatted as required by ``jQuery-Autocomplete``
and returned to the client as JSON.

We will also format the incoming query to allow for a partial string match. 
Using the example of ``be`` as a search, our script will add a ``*`` to it
and pass it to ``sql.exec()``.
 
Texis' Fulltext search will first search its index for all words beginning
with ``be`` and add them to the query.  Since this could potentially be
thousands of words added to our query, we need to make a few adjustments to
the default limits.

.. code-block:: javascript

    sql.set({
        'qMaxWords'    : 5000,  // allow query and sets to be larger than normal for '*' wildcard searches
        'qMaxSetWords' : 5000   // see https://rampart.dev/docs/sql-set.html#qmaxsetwords 
                                // and https://rampart.dev/docs/sql-set.html#qmaxwords .
    });

 
We also want to continue to ignore noise words.

.. code-block:: javascript

    sql.set({
        noiseList      : [],    // allow search for 'the', 'us', 'or', etc.
        'qMaxWords'    : 5000,  // allow query and sets to be larger than normal for '*' wildcard searches
        'qMaxSetWords' : 5000   // see https://rampart.dev/docs/sql-set.html#qmaxsetwords 
                                // and https://rampart.dev/docs/sql-set.html#qmaxwords .
    });


Our completed AJAX function now looks like this:

.. code-block:: javascript

    // For autocomp. This needs to be set only once
    sql.set({
        noiseList      : [],    // allow search for 'the', 'us', 'or', etc.
        'qMaxWords'    : 5000,  // allow query and sets to be larger than normal for '*' wildcard searches
        'qMaxSetWords' : 5000   // see https://rampart.dev/docs/sql-set.html#qmaxsetwords 
                                // and https://rampart.dev/docs/sql-set.html#qmaxwords .
    });

    /* autocomp() results must be formatted as such:
    {
        "suggestions": [
            {"value":"Vaulion, Canton de Vaud, CH","id":"6233eaf65bd","latitude":46.6848,"longitude":6.3832, ...},
            {"value":"Vallorbe, Canton de Vaud, CH","id":"6233eaf65c6","latitude":46.7078,"longitude":6.3714, ...},
            ...
        ]
    }
    */

    function autocomp(req) {
        var res;
        var q = req.query.query;

        // remove any spaces at the beginning of q
        q = q.replace(/^\s+/, '');

        // if query is only one char, return an empty set
        //   (even though client-side autocomplete is set to 2 char min)
        if(q.length<2)
            return {json: { "suggestions": []}}

        // we will need at least two chars in our last word since it will get a '*' wildcard added to it
        q = q.replace(/ \S$/, ' ');

        // if last character is not a space, add wildcard
        if(q.charAt(q.length-1) != ' ')
            q += '*';

        // perform a like3 (no rank) pre-sorted by pop text search, and return a list of best matching locations
        res = sql.exec("SELECT place value, id, latitude, longitude, population FROM cities WHERE "+
                        "place LIKE3 ? ;", [q] );

        //if no results, try again using alt_names
        if(res.rowCount == 0) {
            res = sql.exec("SELECT place value, alt_names,id, latitude, longitude, population FROM cities WHERE " +
                            "alt_names LIKE3 ? ;", [q] );
            // add alt name to "value" for type ahead display
            for (var i=0; i<res.rows.length;i++) {
                var row = res.rows[i];
                var ql = req.query.query.toLowerCase();
                var anames = row.alt_names.split(',');
                for (var j=0; j<anames.length;j++) {
                    var aname = anames[j].toLowerCase();
                    if(aname.indexOf(ql) > -1) {
                        row.value += ' (aka: ' +  aname + ')';
                        break;
                    }
                }
            }
        }
        return {json: { "suggestions": res.rows}};
    }

The Complete Server-Side Script
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    ...

    var useKilometers = true;

    var distconv = 1;
    var distvar = "miles";

    if(useKilometers) {
        distconv=1.60934;
        distvar = "kilometers";
    }

    function htmlpage(req) {
        var id = req.params.id, lat, lon;

        // check if we already have a place id.
        if(id){
            id_res= sql.one("SELECT place, latitude, longitude " + 
                "FROM cities WHERE id=?;",
                [req.params.id]
            );
            // yes, then set lat,lon vars
            if(id_res) {
                lon=id_res.longitude;
                lat=id_res.latitude;
            }
        } else {
            // no, just print the blank search form
            req.printf(pageTopFmt,'');  // add top of page to return buffer without a query.
            return({html:pageBottom});  // add bottom of page, return with 'content-type:text/html'
        } 

        // what to do if the query_string id is not found in the db
        if(!lon || !lat) {
            req.printf(pageTopFmt,'');
            req.printf('No entry for id "%s".', id);
            return({html:pageBottom});
        }

        /* here we select rows based on their distance from the place specified by 'id',
           calculate the distance and direction between id and the selected city,
           then sort by the distance from 'id' (field 6 in our sql statement) */
        res = sql.exec(`SELECT
            place, id, latitude, longitude, population,
            DISTLATLON(?lat, ?lon, latitude, longitude) dist,
            AZIMUTH2COMPASS( AZIMUTHLATLON(?lat, ?lon, latitude, longitude), 3 ) heading
            FROM cities WHERE geocode BETWEEN (SELECT LATLON2GEOCODEAREA(?lat, ?lon, 1.0))
            ORDER BY 6 ASC;`,
            {lat:lat, lon:lon},
            {maxRows: 31}, // first row is same city
            function(res, i) { // foreach city retrieved:
                if(!i) {
                    // this is our 'id' city, as it is closest to itself.
                    req.printf(pageTopFmt,res.place);
                    req.printf('<h3 style="margin-bottom:0px">%s</h3><ul style="margin-top:0px">',res.place);
                } else {
                    // all other nearby cities we will print the direction and distance:
                    req.printf('<a href="?id=%s">%s</a><br><ul>' +
                        '<li>Direction:  %.2f %s to the %s</li>',
                        res.id, res.place, res.dist * distconv, distvar, res.heading);
                }
                // some useful information to go along with the city name
                req.printf("<li>Population: %s</li>" +
                    '<li>Location: <a target="_blank" href="https://maps.google.com/maps?z=11&q=%U&ll=%f,%f">' +
                    'google maps (%.4f,%.4f)</a></li></ul>',
                    Sql.stringFormat('%ki', res.population), res.place, res.latitude, res.longitude , res.latitude, res.longitude);

                if(!i) req.put('<hr><h3>Closest Cities:</h3>');
            }
        );
        return {html:pageBottom}; //pageBottom is added to same buffer as is used with req.printf()
    }

    // For autocomp. This needs to be set only once
    sql.set({
        noiseList      : [],    // allow search for 'the', 'us', 'or', etc.
        'qMaxWords'    : 5000,  // allow query and sets to be larger than normal for '*' wildcard searches
        'qMaxSetWords' : 5000   // see https://rampart.dev/docs/sql-set.html#qmaxsetwords 
                                // and https://rampart.dev/docs/sql-set.html#qmaxwords .
    });

    /* autocomp() results must be formatted as such:
    {
        "suggestions": [
            {"value":"Vaulion, Canton de Vaud, CH","id":"6233eaf65bd","latitude":46.6848,"longitude":6.3832, ...},
            {"value":"Vallorbe, Canton de Vaud, CH","id":"6233eaf65c6","latitude":46.7078,"longitude":6.3714, ...},
            ...
        ]
    }
    */

    function autocomp(req) {
        var res;
        var q = req.query.query;

        // remove any spaces at the beginning of q
        q = q.replace(/^\s+/, '');

        // if query is only one char, return an empty set
        //   (even though client-side autocomplete is set to 2 char min)
        if(q.length<2)
            return {json: { "suggestions": []}}

        // we will need at least two chars in our last word since it will get a '*' wildcard added to it
        q = q.replace(/ \S$/, ' ');

        // if last character is not a space, add wildcard
        if(q.charAt(q.length-1) != ' ')
            q += '*';

        // perform a like3 (no rank) pre-sorted by pop text search, and return a list of best matching locations
        res = sql.exec("SELECT place value, id, latitude, longitude, population FROM cities WHERE "+
                        "place LIKE3 ? ;", [q] );

        //if no results, try again using alt_names
        if(res.rowCount == 0) {
            res = sql.exec("SELECT place value, alt_names,id, latitude, longitude, population FROM cities WHERE " +
                            "alt_names LIKE3 ? ;", [q] );
            // add alt name to "value" for type ahead display
            for (var i=0; i<res.rows.length;i++) {
                var row = res.rows[i];
                var ql = req.query.query.toLowerCase();
                var anames = row.alt_names.split(',');
                for (var j=0; j<anames.length;j++) {
                    var aname = anames[j].toLowerCase();
                    if(aname.indexOf(ql) > -1) {
                        row.value += ' (aka: ' +  aname + ')';
                        break;
                    }
                }
            }
        }
        return {json: { "suggestions": res.rows}};
    }

The Complete Script
-------------------

We now have all we need to perform the autocomplete search and nearest
city search.  This is the final script which, as layed out may be accessed
at ``http://localhost:8088/apps/citysearch/``.  It will also build the
database when run from the command line as ``rampart citysearch.js``.

.. code-block:: javascript

    // cuz no one likes writing out 'rampart.utils.printf()'
    rampart.globalize(rampart.utils);
    
    var Sql = require("rampart-sql");

    var sql = new Sql.init("~/citysearch/web_server/data/cities", true);

    var csvFile = "../data/geonames-all-cities-with-a-population-1000.csv";

    function import_data(){
        function create_tmp_table() {
            sql.exec("create table cities_tmp (" +
                    "Geoname_ID              varchar(8), " +
                    "Name                    varchar(8), " +
                    "ASCII_Name              varchar(8), " +
                    "Alternate_Names         varchar(8), " +
                    "Feature_Class           varchar(8), " +
                    "Feature_Code            varchar(8), " +
                    "Country_Code            varchar(8), " +
                    "Country_name_EN         varchar(8), " +
                    "Country_Code_2          varchar(8), " +
                    "Admin1_Code             varchar(8), " +
                    "Admin2_Code             varchar(8), " +
                    "Admin3_Code             varchar(8), " +
                    "Admin4_Code             varchar(8), " +
                    "Population              int, "        +
                    "Elevation               int, "        +
                    "Digital_Elevation_Model int, "        +
                    "Timezone                varchar(8), " +
                    "Modification_date       varchar(8), " +
                    "LABEL_EN                varchar(8), " +
                    "Coordinates             varchar(8)"   +
                    ");"  ); 
        }


        var total=-1; //we won't know the total until we finish the first pass of importCsvFile
        var step = 100; //set in importCsvFile(), only report every 100th row

        /* a single function to monitor the import for both pre-processing (progressFunc)
           and import (callback function supplied to sql.importCsvFile as a parameter)   */
        function monitor_import(count, stg) {
            var stage = "Import";

            if(count==0)
                printf("\n");

            if(stg!==undefined) // progressfunc
                stage=stg;

            if(stg === 0) //differentiate between 0 and undefined
            {
                total=count; //update our total in the first stage.
                printf("Stage: %s, Count: %d       \r", stage, count);
            } else {
                printf("Stage: %s, Count: %d of %d      \r", stage, count, total);
            }
            fflush(stdout);
        }

        function import_csv() {
            total = sql.importCsvFile(
                csvFile,  //file to import
                {
                    tableName:       'cities_tmp',
                    singleQuoteNest: false,
                    hasHeaderRow:    true,
                    delimiter:       ';',
                    normalize:       false,
                    callbackStep:    step, //callback run every 100th row
                    progressStep:    step, //progressfunc run every 100th row for each stage
                    progressFunc:    monitor_import //progress function while processing csv 
                },
                monitor_import //callback function upon actual import
            );
            printf('\n%d rows in total.\n',total);
        }

        function create_final_table() {
            sql.exec("create table cities (" +
                    "id                      counter, "    +
                    "place                   varchar(8), " +
                    "alt_names               varchar(8), " +
                    "population              int, "        +
                    "latitude                double, "     +
                    "longitude           double, "     +
                    "geocode             long, "       +
                    "timezone                varchar(8), " +
                    "country                 varchar(8) "  +
                    ");"  ); 
        }

        function makerow(o) {
            var ret={}, tmp;

            ret.place = sprintf('%s, %s %s(%s)', o.Name, o.Admin1_Code, o.Country_name_EN, o.Country_Code);
            ret.altNames = o.Alternate_Names;
            ret.population = o.Population;
            tmp = o.Coordinates.split(',');
            ret.lat = parseFloat(tmp[0]);
            ret.lon = parseFloat(tmp[1]);
            ret.tz = o.Timezone;
            ret.country = o.Country_name_EN;
            return ret;
        }

        function build_final_table() {
            printf("sorting rows\n");
            sql.exec("select * from cities_tmp order by Population DESC",
                function(res,i) {

                    if(!i) printf("done\nCreating Final Table\n");

                    var vals = makerow(res);
                    sql.exec("insert into cities values( " +
                        "counter, ?place, ?altNames, ?population, ?lat, ?lon, latlon2geocode(?lat, ?lon), ?tz, ?country );",
                        vals );
                    if (! (i % 100) ) {
                        printf("%d of %d\r", i, total);
                        fflush(stdout);
                    }
                },
                {maxRows:-1}
            );
            printf('\n');
        }

        function make_geocode_index() {
            printf("creating index on geocode\n");
            sql.exec("create index cities_geocode_x on cities(geocode) WITH INDEXMETER 'on';");
        }

        function make_id_index(){
            printf("creating index on id\n");
            sql.exec("create index cities_id_x on cities(id) WITH INDEXMETER 'on';");
        }

        function make_text_indexes() {
            printf("creating indexes on place names\n");

            // noiselist as detailed at https://rampart.dev/docs/sql-set.html#noiselist
            // This is not English text and some geographic abbreviations like OR IN DO TO SO and US
            // are also on the noise words list.  Setting to empty will allow such words in the index.
            sql.set({ noiseList:[]});

            // make compact index.  Sorting by population, not by likep rank.  See like3 search below.
            sql.exec("create fulltext index cities_place_ftx on cities(place)"+
                " WITH WORDEXPRESSIONS ('[\\uword]{1,99}') INDEXMETER 'on' WORDPOSITIONS 'off';");

            sql.exec("create fulltext index cities_altNames_ftx on cities(alt_names)"+
                " WITH WORDEXPRESSIONS ('[\\uword]{1,99}') INDEXMETER 'on' WORDPOSITIONS 'off';");
        }

        function drop_tmp_table() {
            sql.exec("drop table cities_tmp");
        }

        create_tmp_table();
        import_csv();
        create_final_table();
        build_final_table();
        make_geocode_index();
        make_id_index();
        make_text_indexes();
        drop_tmp_table();

    }

    var useKilometers = true;

    var distconv = 1;
    var distvar = "miles";

    if(useKilometers) {
        distconv=1.60934;
        distvar = "kilometers";
    }

    // the autocomplete plugin from  https://github.com/devbridge/jQuery-Autocomplete
    // jquery and plugin included from cloudflare in <script src="xyz"> tags below in pageTopFmt.
    var client_script = `
    $(document).ready(function(){
        $('#cstextbox').autocomplete(
            {
                serviceUrl: '/apps/citysearch/autocomp.json',
                minChars: 2,
                autoSelectFirst: true,
                showNoSuggestionNotice: true,
                triggerSelectOnValidInput: false,
                onSelect: function(sel) { window.location.assign("./?id="+sel.id); }
            }
        );

        $('#cstextbox').on('keypress', function(e){
            var key = e.charCode || e.keyCode || 0;
            if (key == 13) {       // on <return> don't submit form
                e.preventDefault();
                return false;
            }
        });
    });
    `;

    // pageTopFmt is defined once upon script load here rather than upon each request in 
    // htmlpage() below. format code %w removes leading white space.
    var pageTopFmt=sprintf('%w',`<!DOCTYPE HTML>
    <html>
        <head><meta charset="utf-8">
        <script src="https://cdnjs.cloudflare.com/ajax/libs/jquery/3.5.1/jquery.min.js"></script>
        <script src="https://cdnjs.cloudflare.com/ajax/libs/jquery.devbridge-autocomplete/1.4.11/jquery.autocomplete.min.js"></script>
        <style>
            body,h1,h2,h3,h4,h5,h6 {font-family: "Varela Round", Sans-Serif;}
            .autocomplete-suggestions {border: 1px solid #999; background: #FFF; overflow: auto; width: auto !important; padding-right:5px;}
            .autocomplete-suggestion { padding: 2px 5px; white-space: nowrap; overflow: hidden; }
            .autocomplete-suggestions strong {font-weight: normal; color: #3399FF; }
            .autocomplete-group strong { display: block; border-bottom: 1px solid #000; }
            .autocomplete-selected { background: #F0F0F0; }
            .autocomplete-group { padding: 2px 5px; }
            #main {background-color: white;margin: auto;min-height: 300px;width: 600px;}
            #idiv { width:500px;height:39px;border-bottom: lightGray 1px solid;padding:15px 0px 15px 0px;}
            #cstextbox {min-width:150px;width:100%%;height:30px;font:normal 18px arial,sans-serif;padding: 1px 3px;border: 2px solid #ccc;box-sizing: bord
        </style>
        <title>City Search Tutorial</title>
        </head>
        <body>
        <div id="main">
          <form id="mf">
              <div id="idiv">
                  <input type="text" id="cstextbox" name="q" value="%s" placeholder="Search for a city">
              </div>
          </form>
          <div id="res">`);

    var pageBottom = sprintf(`</div></body><script>
    %w
    </script></html>`, client_script);
    function htmlpage(req) {
        var id = req.params.id, lat, lon;

        // check if we already have a place id.
        if(id){
            id_res= sql.one("SELECT place, latitude, longitude " + 
                "FROM cities WHERE id=?;",
                [req.params.id]
            );
            // yes, then set lat,lon vars
            if(id_res) {
                lon=id_res.longitude;
                lat=id_res.latitude;
            }
        } else {
            // no, just print the blank search form
            req.printf(pageTopFmt,'');  // add top of page to return buffer without a query.
            return({html:pageBottom});  // add bottom of page, return with 'content-type:text/html'
        } 

        // what to do if the query_string id is not found in the db
        if(!lon || !lat) {
            req.printf(pageTopFmt,'');
            req.printf('No entry for id "%s".', id);
            return({html:pageBottom});
        }

        /* here we select rows based on their distance from the place specified by 'id',
           calculate the distance and direction between id and the selected city,
           then sort by the distance from 'id' (field 6 in our sql statement) */
        res = sql.exec(`SELECT
            place, id, latitude, longitude, population,
            DISTLATLON(?lat, ?lon, latitude, longitude) dist,
            AZIMUTH2COMPASS( AZIMUTHLATLON(?lat, ?lon, latitude, longitude), 3 ) heading
            FROM cities WHERE geocode BETWEEN (SELECT LATLON2GEOCODEAREA(?lat, ?lon, 1.0))
            ORDER BY 6 ASC;`,
            {lat:lat, lon:lon},
            {maxRows: 31}, // first row is same city
            function(res, i) { // foreach city retrieved:
                if(!i) {
                    // this is our 'id' city, as it is closest to itself.
                    req.printf(pageTopFmt,res.place);
                    req.printf('<h3 style="margin-bottom:0px">%s</h3><ul style="margin-top:0px">',res.place);
                } else {
                    // all other nearby cities we will print the direction and distance:
                    req.printf('<a href="?id=%s">%s</a><br><ul>' +
                        '<li>Direction:  %.2f %s to the %s</li>',
                        res.id, res.place, res.dist * distconv, distvar, res.heading);
                }
                // some useful information to go along with the city name
                req.printf("<li>Population: %s</li>" +
                    '<li>Location: <a target="_blank" href="https://maps.google.com/maps?z=11&q=%U&ll=%f,%f">' +
                    'google maps (%.4f,%.4f)</a></li></ul>',
                    Sql.stringFormat('%ki', res.population), res.place, res.latitude, res.longitude , res.latitude, res.longitude);

                if(!i) req.put('<hr><h3>Closest Cities:</h3>');
            }
        );
        return {html:pageBottom}; //pageBottom is added to same buffer as is used with req.printf()
    }

    // For autocomp. This needs to be set only once
    sql.set({
        noiseList      : [],    // allow search for 'the', 'us', 'or', etc.
        'qMaxWords'    : 5000,  // allow query and sets to be larger than normal for '*' wildcard searches
        'qMaxSetWords' : 5000   // see https://rampart.dev/docs/sql-set.html#qmaxsetwords 
                                // and https://rampart.dev/docs/sql-set.html#qmaxwords .
    });

    /* autocomp() results must be formatted as such:
    {
        "suggestions": [
            {"value":"Vaulion, Canton de Vaud, CH","id":"6233eaf65bd","latitude":46.6848,"longitude":6.3832, ...},
            {"value":"Vallorbe, Canton de Vaud, CH","id":"6233eaf65c6","latitude":46.7078,"longitude":6.3714, ...},
            ...
        ]
    }
    */

    function autocomp(req) {
        var res;
        var q = req.query.query;

        // remove any spaces at the beginning of q
        q = q.replace(/^\s+/, '');

        // if query is only one char, return an empty set
        //   (even though client-side autocomplete is set to 2 char min)
        if(q.length<2)
            return {json: { "suggestions": []}}

        // we will need at least two chars in our last word since it will get a '*' wildcard added to it
        q = q.replace(/ \S$/, ' ');

        // if last character is not a space, add wildcard
        if(q.charAt(q.length-1) != ' ')
            q += '*';

        // perform a like3 (no rank) pre-sorted by pop text search, and return a list of best matching locations
        res = sql.exec("SELECT place value, id, latitude, longitude, population FROM cities WHERE "+
                        "place LIKE3 ? ;", [q] );

        //if no results, try again using alt_names
        if(res.rowCount == 0) {
            res = sql.exec("SELECT place value, alt_names,id, latitude, longitude, population FROM cities WHERE " +
                            "alt_names LIKE3 ? ;", [q] );
            // add alt name to "value" for type ahead display
            for (var i=0; i<res.rows.length;i++) {
                var row = res.rows[i];
                var ql = req.query.query.toLowerCase();
                var anames = row.alt_names.split(',');
                for (var j=0; j<anames.length;j++) {
                    var aname = anames[j].toLowerCase();
                    if(aname.indexOf(ql) > -1) {
                        row.value += ' (aka: ' +  aname + ')';
                        break;
                    }
                }
            }
        }
        return {json: { "suggestions": res.rows}};
    }

    // module and module.exports are set when called from the webserver
    if(module && module.exports) {
        // url to function mapping
        module.exports= {
            "/":               htmlpage,  //http://localhost:8088/apps/citysearch/
            "/index.html":     htmlpage,  //http://localhost:8088/apps/citysearch/index.html
            "/autocomp.json":  autocomp,  //http://localhost:8088/apps/citysearch/autocomp.json
        }
    } else {
        // called from the command line.  Build the database.
        import_data();
    }

Enjoy.
