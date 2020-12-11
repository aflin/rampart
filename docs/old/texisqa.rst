Introduction
============

This document answers the most frequently asked questions about Texis.
It is intended for use by salespersons, support personnel and
consultants.

Why Is This Technology Important?
=================================

Why is Texis important?
~~~~~~~~~~~~~~~~~~~~~~~

Customers are demanding fully integrated systems that manage information
- regardless of data source. Data could be conventional structured data,
text, graphics, images, video or sound.

They want the ability to store, search, retrieve and view these data
objects together in a single, integrated application and deploy that
application over a variety of network topologies.

What does a Text Retrieval RDBMS provide?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An effective system provides the ability to store, manage and
subsequently retrieve textual information. The text can consist of
complete documents, or be abstracts/bibliographies, email, notes,
keywords, names, titles etc. Documents can of course be multi-media made
up of text, graphics, images, drawings, etc.

A key requirement is that text can be placed into the system in an
unstructured (free) format, i.e. the format of books, memos, etc.

Another key requirement is that the user must be able to search and
retrieve information based upon the content of documents (words),
together with (and not solely on) conventional structured fields, e.g.
document number date, author name.

Why is Thunderstone Corporation interested in an RDBMS?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The marketplace for internetworked information management/retrieval
systems is rapidly expanding. Texis represents a crucial part of the
information requirement by symmetrically merging relational data and
full text retrieval.

No other product can make this claim, and no integration of other
products can easily replicate Texis functionality.

What is Texis’ approach?
~~~~~~~~~~~~~~~~~~~~~~~~

Texis’ approach is to provide text retrieval capability, within the
framework of a database management system.

By providing this integrated approach two key benefits are obtained by
users:

(i) Required applications, containing both documents and structured
data, can be generated effectively.

(ii) All applications can be created, maintained and run in a common
software environment. This means that return on investment in software,
staff training and support can be maximized.

What is the opportunity for Thunderstone?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By taking this unique approach we realize the following benefits:

(i) It will expand the number of applications that customers can use
Thunderstone products for. This will generate significant product
revenue for Texis, as well as leverage additional revenue from
incremental product sales, consultancy and support fees.

(ii) The technology will be used in other Thunderstone products, e.g.
CASE, Personnel, 4GL Tools, and Office Automation, adding unique
facilities to these important product areas.

(iii) We will provide a unique product to Thunderstone’s Original
Equipment Manufacturers (OEM’s) and VAR’s - making us more attractive to
them.

(iv) Thunderstone’s consultancy operation can include a True database in
all the applications we specify design and build for customers.

(v) It confirms Thunderstone as the company with the best product
portfolio for serving the ever growing Internet application market.

(vi) It differentiates us greatly from our competitors.

Product Features
================

General
-------

What is Texis?
~~~~~~~~~~~~~~

Texis is a highly portable information retrieval and management product
that fully integrates documents into strategic business applications,
and deploys these applications over a variety of Intra/Inter-net
topologies.

What does it do?
~~~~~~~~~~~~~~~~

It enables both structured data and unstructured data to be stored and
retrieved efficiently, so that ALL relevant information can be retrieved
when required.

The product provides you with the ability to:

(i) Load documents or text fields into a TEXIS database, in several
different ways from several sources, e.g. HTML, WP systems, OCR.,
existing files, archive systems. Or, alternatively leave your text
within the operating system files in its native format.

(ii) Index the text automatically, via several options, which enables
the text to be searched and retrieved efficiently and in a totally
flexible manner.

(iii) Generate queries that will retrieve the relevant textual and
structured information.

(iv) Allow the retrieved information to be displayed in a chosen
sequence onto a web browser, selected output device, or launch an
application e.g. image viewer.

(v) Maintain the information, automatically updating the text if
desired.

(vi) Provides usage audit and access control mechanisms.

What are the components of Texis?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis consists of:

-  a powerful scripting language for creating Intra/Inter-net
   applications.

-  utilities for maintenance of the text data dictionary

-  utilities for text loading/indexing

-  retrieval language (SQL with the addition of a sophisticated ``LIKE``
   clauses)

-  various client interfaces for generation of user applications via
   third party 5GL tools.

-  an ad hoc command language

-  set of demonstration samples

What are the software dependencies?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Unix, Windows NT/2000 for a server, and any web browser for a client.
Client APIs are supported on the same platforms as the server.

TCP/IP if operating in a client server environment.

Are there any hardware dependencies?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No.

Texis is portable to all hardware environments that have the required
software ported (see item above).

What additional functionalility does Texis provide over Metamorph and 3DB?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis provides a completely structured environment for management,
control and dissemination of data around the core functionality of
Metamorph.

Texis provides the ability to create any application the user desires
instead of accepting a single retrieval methodology.

What major performance improvements does Texis provide over Metamorph and 3DB?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis contains many technology advances that provide improved
performance:

-  Variable key size Btree’s for word index storage which greatly reduce
   disk activity during searching and indexing.

-  Improved index compression techniques which result in faster
   retrieval and lower overhead.

-  The zero-latency index queue which dramatically reduces the time
   required to keep text indexes current.

How have these terrific performance improvements been achieved?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis = 17 Years of experience + 7 Years development time + A ton of
customer feedback.

With all this new technology and functionality in how compatible is it with Metamorph and 3DB?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis is 100 percent compatible with the existing Metamorph Query
Language, API calls, and network interface.

Texis represents the next generation of the 3DB product, and is not
binary compatible with 3DB’s databases. However, we provide an an API
layer over TEXIS which will fully emulate 3DB’s functionality.

Text Storage
------------

How Is text stored in an Texis database?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Documents can be stored in a single field per record, or split into
several fields within a Texis table structure. These fields might be for
example, author, title, abstract and the body of the text. Also
additional fields can be designed containing traditional structured
data, for example numeric or date information.

What size of text database can Texis handle?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There is no limit to the size of a database (see also question 2.7).

What size of individual document can Texis handle?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis can handle documents of up to 2 Gigabytes each on all platforms,
and on operating systems that support it, it can handle 64-bit files.

What is the index overhead
~~~~~~~~~~~~~~~~~~~~~~~~~~

There are two different indexing schemes available, one has roughly
10-15% overhead, and the other has roughly 40%.

How fast can it create an index?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis can index text data at around 2 Gigabytes / Hour on a 200MHz
Pentium Pro. Actual index times will vary with the number of documents
and their average size.

Can there be multiple text columns per text table?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Unlimited columns may be present in any table, each one defined to be of
the available data types.

Can the text be compressed to reduce storage requirements?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Text may be stored compressed, but we don’t recommend this practice. The
decompression can cause delays and increases server load when a users
want to view documents.

Do all documents need to be stored in Texis tables?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Documents, either formatted or not, do not have to be stored in Texis
tables. Applications may be created where the text remains outside the
Texis database. The Texis environment is used to provide the required
security and control of the text indexes and application. This
functionality is provided through the INDIRECT data type - with pointers
in the field relating to where the documents are stored externally.
Texis supports operating system files (which could have been created
from CDROM or optical disks) and word processor files.

There are no changes required to run a client application with external
documents. Also full text retrieval functionality is available to
external text, e.g. indexing, searching, display, editing.

Retrieval times are faster however when full-text is actually stored in
a Texis table.

Can we store additional information alongside the text?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Structured data can be stored alongside the text in a Texis table in the
usual way. It can either exist on the database already or be loaded as
part of the text load operation.

Can we work with compound documents that contain text, data and images?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Text and data of any type can be stored in the same Texis table.
Compound documents created by say, MS Word or WordPerfect, that contain
both text and graphics can also be managed. The lexical ’filter’ will
ignore graphic information and other non-text content as it indexes the
files.

How do we get text into the database?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis provides several utilities for loading/indexing data. Timport
(Texis import) program is the most common method. It also supports ODBC,
XBASE/dBase file import, and delimited text.

Where can it originate from?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Text can be generated from a range of sources - word processing files,
OCR, electronic mail, existing Texis tables, etc. and is loaded onto the
database in its original format.

Are facilities for dumping and loading data supplied with the product?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Comprehensive utilities are supplied that enable a wide range of options
for handling text and structured data.

Indexing
--------

What indexing strategy does Texis provide?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis has two main indexing strategies available. It has a
coarse-grained inverted index method, which provides the benefits of
both speed for many queries and index compactness.

The second strategy is a complete inversion, which provides improved
performance for many searches at the cost of a slightly larger index.

What is a Text Index?
~~~~~~~~~~~~~~~~~~~~~

A Text Index is a special indexing facility which enables efficient
retrieval of text information. Text indexes are not natively available
in any other RDBMS.

See index Schema diagram.

Why is the Text Index important?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Text retrieval is based on the identification of individual words or
phrases within natural language. If no index was implemented, the whole
of the document file would have to be scanned, looking for the words and
phrases of the query. This scan would have to be repeated for each user
search request and the search time would increase linearly with the text
database size and may become impracticable.

What indexing utilities are supplied?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To index a text field of any type you use a SQL CREATE INDEX statement.

There is a background utility program that will monitor the tables
within a database to see if the indexes are in need of maintenance. This
utility operates on a per index basis using both size change and time as
controlling parameters. (See CHKIND).

At no time is the user query results affected by the state of the index.
Texis will always provide the same results without regard to index
status.

Is there one index per application or table or database?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each text field may have its own index and index parameters. These
parameters include: index update time rate, index update size rate, and
the lexical ’filter’ that applies to that field.

Text indexes may also be created on Virtual Fields which are logically
created combinations of existing fields.

Is the indexing separate from Texis and where is the resultant text index held?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Text indexing is performed by Texis and the text indexes themselves are
always stored within the Texis database (the same as any other field
type).

You may direct Texis to place index files on separate physical devices
if desired. This is usually done to increase performance.

How are non-text objects indexed, e.g. graphical images?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Through a combination of ’structured data’ attributes, such as Creation
date, Customer name, possible reference number PLUS a textual
description of the object, or on text that has been generated alongside
the object, as say, part of an article containing both text and graphics
(compound documents).

Where are the indexes for external documents?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

External documents are logically part of the text table, even though
they are physically outside. Therefore they are indexed in the same way
as all other documents, with the index being held within the database
for security and integrity.

Is there an overhead in space utilization?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Overhead for a coarse-grained text index will range between 7% and 25%
of the size of the data to which it refers. The average overhead tends
to be 15 The overhead for a fully inverted index will range between 20%
and 60%, depending on the nature of the text. The average overhead is
about 40

Is Texis case sensitive?
~~~~~~~~~~~~~~~~~~~~~~~~

Not by default, but users may perform case sensitive queries on any text
field if they wish.

Can the wordlist be viewed?
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, a utility for viewing the contents of a word index is provided.

Can the wordlist be modified?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No, it is an index and it would be inappropriate to perform
modifications directly to it.

The lexical ’filter’ provides mechanisms for controlling the content of
the words within an index.

Do you supply a STOP LIST and can it be modified?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

We supply a list of about 150 words with the software. It is stored in a
master profile and can be modified.

In what languages do you provide the stop list?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The stop list provided with the software is in English. Users may
redefine this list for other languages if desired.

Do you control suffixes of words in the index?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The index will contain the full form of every word. Morpheme processing
(word form changing) is performed at query time to allow a user to find
and validate the differing versions of the same word.

Does Texis support automatic reindexing?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes.

Texis supports both timed and incremental reindexing. Details on
indexing may be found under the Text Maintenance section.

Document Retrieval
------------------

Is the Retrieval Language based upon SQL?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Texis is based on the industry standard SQL language. To do searching on
text fields we have extended SQL by adding a new ``LIKE`` clause. This
new clause provides the full power of the Metamorph search within the
database environment.

Is this as powerful as other text retrieval software?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Even more so, because the full SQL language, together with the text
content searching extensions, can be used in the same query.

Texis doesn’t just rely on one retrieval method. It currently supports
five different retrieval algorithms which may be used individually or in
combination.

Are the extensions to SQL proprietary?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, at the moment, since there are no defined standards for such
extensions to SQL.

How has ’extended SQL’ been extended?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The extended SQL has an added ``LIKE`` clause that defines the text to
be searched for within the document set. These search conditions have a
rich functionality that includes truncation, wild cards, proximity fuzzy
matching, saved query expressions and a wide range of concept search
facilities. All of these can be used with standard Boolean logic to
build up comprehensive topics.

How complicated is the extended SQL?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

About the same as SQL. Simple queries could be written by end users, but
in general you would develop an application that shields end users from
the language.

Can Natural Language queries be used as input?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Natural language queries may be used inside a ``LIKE`` clause to match
against the content of a field. However, putting in a query of the form:
“Show me all the consultants who know about Texis RDBMS and make less
than $50,000.” cannot be handled. This would require extensions via
third-party products such as QandA or EasyTalk that generate SQL from
english.

What are the options then for generating queries?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are several Options:

-  Using Texis Webscript from a Web browser.

-  Use any Windows ODBC client package. (like: Powerbuilder, Microsoft
   Access, Visual Basic..)

-  Use a C-based application and invoke supplied C function calls.

-  Query via the TSQL command line application.

Can we search on ’noise’ words?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Phrase searching and proximity will take account of noise words, so you
can search on such phrases as ’United Sates of America’ or ’state of the
art’.

Can we do Boolean searches?
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, it can do full Boolean searches based on SQL.

Can we search on numbers or dates?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Any quantity, range, date, or pattern may be located within a text field
through the use of the features provided by the Metamorph search.
Numbers and dates stored within fields may be located with traditional
SQL.

Can we use wildcard searches?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes:

-  word\* Anything beginning with word (forward truncation)

-  w\*d Anything ending in ’d’ and beginning with ’w’

-  w\*d\*f Multiple wildcards.

-  Use the Reqular Expression matcher for more complex items

Do you have a Thesaurus?
~~~~~~~~~~~~~~~~~~~~~~~~

Texis comes with a 250,000 word association thesaurus. Add to that its
ability to derive word forms and its vocabulary is huge. The provided
thesuarus contains only commonly used English. Industry specific terms,
phrases, and acronyms may be added by the user.

What relationships are allowed?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis allows true thesaural and synonym relationships. The concepts that
can be used in a query to expand the search include:

-  link to other thesauri

-  synonyms

-  related terms

-  saved macro expressions

-  rules of inclusion and exclusion

The Thesuarus
-------------

Do the thesaural relationships conform to any standard?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

It includes a full implementation of the ISO 2788 standard for Thesaurus
and Synonym Relationships.

Can we import/export conceptual definitions across databases?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Forms-based interactive browsing and maintenance of the ’concepts’
database is provided, together with dump, load and remove facilities
(via utilities).

Can an independently-supplied thesaurus handler be used in conjunction with Texis?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No

However if the thesaurus handler allows the unloading of its data, then
this data could be loaded into the Texis thesaurus management system.

Note: There may need to be some data format conversion.

Search and Retrieval
--------------------

What is fuzzy matching?
~~~~~~~~~~~~~~~~~~~~~~~

Fuzzy matching is the searching for words that are ’similar’ to the
search term. It is used to compensate for errors in data entry and
phonetics.

How is fuzzy matching implemented?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis uses a proprietary Thunderstone algorithm that generates a
similarity measure between any two words or patterns. This measure is
expresses as a percentage of “closeness” to the users input.

Do we have to view all the documents retrieved by a query?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No

The queries are ordinary select statements, so you can choose which
fields to see. If one of these fields is a key to the table it can later
be used to retrieve the desired document. This allows an application to
build a ’Hitlist’ that allows you to decide which documents you are
interested in.

Can we browse the database as well as the hitlist?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

At any time the application can switch from hitlist browse to database
browse mode, and visa versa.

Is the hitlist available for other application processes?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes.

The hitlist is simply the result set from the query. The application
that executed the query can do whatever it wants with the information.

Can Texis do relevance ranking of retrieved documents?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes. It is possible to have Texis return the result set ordered by
relevance, as well as produce a rank associated with the document. Many
controls are provided to tune the ranking to your needs.

After a search can the resulting text be highlighted?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Texis Webscript has all the functionality builtin to highlight text.

The data specifying the location and size of the match is available to
the application program via the APIs, to display the hit highlighted.

Having done one query, I wish to search on a more limited range. Can I do this without re-executing the entire query?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Absolutely!

The results of a query can be saved into a temporary table, and then
this temporary table searched in a more refined manner.

What is User Profiling and can Texis do it?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

User Profiling (sometimes called Automated Message Handling (AMHS) or
Selective Dissemination of Information (SDl) in Europe) is the ability
to store predefined queries and execute them automatically against new
information in the database. Texis can store queries under users - so an
application could be developed to periodically run them against the
database, or the insertion of the data can fire a trigger to perform the
dissemination. This works well with data in the form of ’real-time’ data
feeds (see question 5.1).

What is fuzzy matching?
~~~~~~~~~~~~~~~~~~~~~~~

Fuzzy matching is the searching for words that are ’similar’ to the
search term. It is used to compensate for errors in data entry and
phonetics.

How is fuzzy matching implemented?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis uses a proprietary Thunderstone algorithm that generates a
similarity measure between any two words or patterns. This measure is
expresses as a percentage of “closeness” to the users input.

Text Maintenance
----------------

Can we edit documents from our text retrieval application?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Any operating system editor, word processor or desktop publishing system
may be used providing that:

-  for non-ASCii documents, there is a conversion filter

-  the text retrieval application has been set up to call the
   appropriate application software

A chosen document is ’passed’ to the editing software - which can be
called from the text application. At the end of editing control is
passed back to the text application.

Note: The Zero Latency feature will handle the indexing of the revised
text - making it always current and available for searching.

Is the text index automatically updated after editing?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When control passes back to the application it will inform Texis that
the text has changed. This will be immediately noted in the index, so
subsequent searches will work as expected. A seperate maintenance
program monitors the index to keep it in an efficient form.

(See prior question)

Can documents be printed as well as displayed?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

This is handled in a manner similar to editing.

Is there an AUDIT trail facility?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

In that you can use the Texis DBMS facilities.

Can documents be deleted from the Texis system?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As documents are treated the same as any other record in Texis they can
be deleted with a SQL ``DELETE`` command.

Once physically deleted, are external documents accessible from the operating system and/or can they be reinstated within Texis?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The external documents are always accessible from the operating system -
only the Texis record and index information are removed. They can be
reinstated by reloading this information into Texis and then indexing.

When maintaining text does the user come from the application or the word processor used to create the text?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The user is taken seamlessly from the application to the word processor
used to create the text. If this route is not taken then the data in the
WP file might be ’out-of-step’ with the text index used by the
application!

External Data Sources
---------------------

Will it load formatted Word Processor (WP) files?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis currently supports about 100 different file formats. Documents
with full WP format control characters can be loaded. Texis’ lexical
filter will reccognize the language content, and discard the control
information.

How does Texis handle formatted WP files?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Documents with full WP format control characters are stored in an Texis
table in their original format.

Can we use a WP directly on retrieved text?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A WP package can be used directly on the text when a document has been
retrieved. The WP package would be launched from the application. Also a
retrieved document could be placed in a standard file and then accessed
by any appropriate WP system.

Can we interface to third party publishing packages like Interleaf, Ventura and PageMaker?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

In exactly the same way as word processor documents are handled.

Does Texis work with mass storage devices such as CD-ROM?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By definition, CDROM is read-only. Thus the only use that can be made of
it is as a SOURCE of data for Texis. However there may be situations
where data is distributed on CDROM, and we want to select certain data
from it (using the retrieval system which is usually supplied with the
disk). This data could then be loaded into Texis in the normal way.

The possible uses of CDROM are:

-  a complete Texis database could be built on CDROM. As a CDROM is
   typically slower than a hard disk, a corresponding reduction in
   retrieval rate will be seen.

-  The files on a CDROM can be indexed by Texis as external documents.
   Again the slower transfer rate must be taken into account.

Can Texis handle structured documents?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes. By correctly defining the ’look’ of the doument structure to
Texis-import.

Do we support HTML/XML/SGML documents?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Texis program Webinator provides full HTML 3.0 import support.
XML/SGML tag / structure information may be described for use by
Texis-import either with XQL or by regular expression.

Can we use spreadsheet information?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, in the same way as word processing files.

Can we use PDF, Postscript and EPS?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Thunderstone supports Adobe Acrobat / PDF files directly on platforms
where the Adobe toolkit is available.

Postscript files may be imported by Texis. Certain postscript files may
not be legible to the text index program however; it depends on the
origin of the file.

Application Development
-----------------------

How can customers generate their required solution?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Thunderstone’s Texis Webscript is the best and fastest way to create
Internet / Intranet Web aplications.

Thunderstone provides a complete API that can be used to build
applications using standard C. These calls allow the easy embedding of
SQL into C programs.

Texis also supports ODBC, and provides an ODBC driver for Windows. The
same ODBC call set is also available to developers on all platforms
supported by Texis.

Can customers integrate documents into existing Texis applications?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Document storage and retrieval can be easily added to existing Web, ODBC
and C-based applications. It involves basically creating new tables and
indexes.

Is ODBC the only API supported?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No.

As well as supporting ODBC on all platforms we also provide our own
proprietary ’C’ APIs which are faster and simpler than ODBC for talking
to Texis.

Is there support for Network Code Generator applications?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An NCG interface is supplied for use by Thunderstone customers to
integrate into their other NCG applications.

Does Texis use Thunderstone’s Metamorph API?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

You can use any of the normal metamorph API functions within a Texis
application, although this is not generally required. Texis does not
however use the 3DB API. In fact it is possible to implement the 3DB API
using Texis.

Can Texis messages in an application be customized?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Because the generated messages are passed to a function in the
application which can analyze and modify them, and optionally display
them.

What areas are covered by the API?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both toolkits consist of over 50 user exits or function calls that can
be classified into the following areas:

-  Query Manipulation - creation and modification of the extended-SQL
   statement.

-  Query Execution - performs the search against the database and
   optionally generates the result set.

-  Result Set Control - builds the resulting results and provides
   functions to access the data with in the result set.

-  Text Display/Printing - ability to determine where in the result set
   the hit occurred.

-  Text Manipulation - provides the required updating capability,
   including the immediate updating of the text index in online mode.

-  System Control - initialization and termination of sessions, saving
   of queries, and invoking of operating system commands.

Is there a new API with this version?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, Texis has it’s own APIs, which are either standards (ODBC), or are
modelled after the rest of the Thunderstone APIs.

Is a command language supplied?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

The command language used is SQL, with some added functions for loading
text. Almost all of the required interaction can be performed entirely
within SQL.

Client/Server Issues
--------------------

Can Texis work in client/server mode?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Absolutely, this is the intented purpose of Texis!

Because an NCG interface and an ODBC interface are provided the Texis
application they will work seamlessly over a network. Any programs
written to the server level API will not work over a network.

How do we invoke Texis over a WAN?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Any WAN issue is handled by NCG or ODBC rather than Texis.

Does the document, if external, have to be in the domain of the database?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, the documents must be accessible to the server, otherwise it is
impossible for Texis to read them for the purpose of indexing them.

Does Texis have to be installed on both the client and the server?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Not if its Web Based.

In client server mode the product is split into two distinct parts. The
server and the client library. The only part required to reside on the
client side is the client library, which only provides access to Texis.

Where does the processing take place?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Query formulation, and text editing will occur at the client. All
indexing and query execution occur on the server.

Where are the text table indexes held?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each table can have its own index and each index can reside on different
physical devices.

Miscellaneous
-------------

Will Texis work in a bitmapped environment?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes

Since applications can run under ODBC then all environments that ODBC
operates in are available to text retrieval applications. Also any
environment capable of calling a standard library can access Texis, so
this opens up almost all environments.

Are hypertext links supported in the product?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, there are a plethora of uses for hyperlinks within HTML
environments using Texis Webscript.

What languages does this version work with?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The text can be in any language. The definition of a word is completely
flexible. Currently the morpheme processing has been tuned to work best
with English like suffixes and prefixes.

What levels of security exist for accessing data within an application?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All normal Texis access and security levels can be used. In addition a
user of a Texis application must be a registered user in the Text
Dictionary (as well as in the Texis Dictionary). Registered users have
username/password combinations. Also, individual document (row) security
level could be easily coded in a 5GL application.

How does Texis handle images?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis stores, manages, and delivers these objects.

The display of images would be via another application or a third party
image system like a web browser, Visual-Basic, Powerbuilder,
Diamondhead, or Microsoft Access.

The index for image objects normally consists of precoded attributes,
stored in the database. If some text can be associated with an image,
via data entry or the use of Optical Character Recognition (OCR)
software, then a text index for the images can be created.

In what type of applications would you use Texis?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  any kind of Web site application

-  Integrated data application market

-  Largescale Document Retrieval applications

-  ’Enterprise and wide’ data-sharing market

-  Personnel records; career development plans, organization skills
   register.

-  Legal records; client history, caselaw.

-  Project Management; activity tracking, contract management.

-  Market research; competitive information, market analysis,
   demographic data.

-  Company Information services; products, pricing, services, sales
   channels, customers, applications, policies.

-  Materials and Services catalogues.

-  Document tracking, drawings management.

-  Bibliographics and Abstracts.

-  Research databases (all research fields).

-  Regulatory information.

-  Help desks.

Which are the key vertical markets where Texis can be used?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These are substantially the same as our installed base:

-  Internet, Intranet - Texis has global apllicability here.

-  Pharmaceuticals - largescale document retrieval - FDA drug
   submission.

-  Energy - safety information, procedures, marketing and product data,
   project information.

-  Legal - Texis will supplant Thunderstone’s already significant share
   of this vertical market.

-  Utilities - need Texis for Regulatory control - especially Nuclear!

-  Government - Military, Intelligence, and Police systems.

Is it targeted at any particular industries?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No

Texis can be used to create software solutions in all industries. Our
customers constantly come up with new and innovative applications. ( In
fact, they are better at it than we are. )

How does the user benefit?
~~~~~~~~~~~~~~~~~~~~~~~~~~

The ability to handle text (especially with structured data) means that
new applications can now be undertaken. The user has control and use of
an important information resource - the written word. We already have
many customers who have gained significant competitive edge by
harnessing the combination of text and structured data.
