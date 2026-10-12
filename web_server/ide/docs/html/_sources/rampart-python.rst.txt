The rampart-python module
=========================

Preface
-------

License
~~~~~~~

The Python library is licensed under the `PSF LICENSE <https://docs.python.org/3/license.html#psf-license>`_\ .

The rampart-python module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-python module includes functions which import Python modules
and scripts, executes them and translates variables between Python and
JavaScript.

How does it work?
~~~~~~~~~~~~~~~~~

The module and the embedded Python interpreter are used to load Python
modules and scripts into the Python environment.  Translation of variables
between the two languages are handled automatically.  As the Python Library
does not function in parallel in multiple threads, if run in
:ref:`rampart.threads <rampart-thread:Rampart Thread Functions>` the module
will run in multiple processes.


Using Python in Threads
~~~~~~~~~~~~~~~~~~~~~~~

In a :ref:`rampart.thread <rampart-thread:Rampart Thread Functions>`, each
thread uses its own Python process.  As a result:

* Modules (and models they load) are loaded separately in each thread.
* A Python variable can only be used in the thread that created it.
* Functions called with `rampart.call`_ must be global in that thread.
* Each call to Python costs a little more than in the main thread.

Loading the Javascript Module
-----------------------------

    Loading of the python module from within Rampart JavaScript is a simple matter
    of using the ``require`` statement:

    .. code-block:: javascript

        var python=require("rampart-python");

    Return value:
        An :green:`Object` with the functions listed below.


Python Module Functions
-----------------------

python.import()
~~~~~~~~~~~~~~~

    Import a python module.

    Usage:

    .. code-block:: javascript

        var python=require("rampart-python");

        /* same as "import mymod" in Python */
        var mymod=python.import("mymod");


    Return Value:
        An :green:`Object` with callable properties corresponding to the functions of the imported module.

    Example:

    .. code-block:: javascript

        var python = require("rampart-python");

        var pathlib = python.import('pathlib');

        var pvar = pathlib.PosixPath('./');

python.importString()
~~~~~~~~~~~~~~~~~~~~~

    Import a python module or script from a :green:`String`.

    Usage:

    .. code-block:: javascript

        var python=require("rampart-python");

        var mymod = python.importString(pyscript[, scriptName]);

    Where:

    * ``pyscript`` is a :green:`String`, the python source code
    * ``scriptName`` is a :green:`String`, an optional name for this script for
      error reporting.  Default is ``"module_from_string"``.

    Return Value:
        An :green:`Object` with callable properties corresponding to the functions of the imported module.

    Example:

    .. code-block:: javascript

        var python=require("rampart-python");

        var pyscript=`
        def makedict(k,v):
            return {k:v}
        `;

        var mymod = python.importString(pyscript);

        var pvar = mymod.makedict("mykey", ["val1", "val2"]);

python.importFile()
~~~~~~~~~~~~~~~~~~~

    Import a python module or script from a file.  Same as
    `python.importString()`_ except the source is loaded from
    the named file.

    Usage:

    .. code-block:: javascript

        var python=require("rampart-python");

        var mymod = python.importFile(fileName);

    Where:

    * ``fileName`` is a :green:`String`, the path of the file to be imported.

    Return Value:
        An :green:`Object` with callable properties corresponding to the functions of the imported module.

pvar.toString()
~~~~~~~~~~~~~~~

    Return the string version of the python variable.

    Example:

    .. code-block:: javascript

        var python = require("rampart-python");

        var pathlib = python.import('pathlib');

        var pvar = pathlib.PosixPath('./');

        rampart.utils.printf( "pathlib=%s\npvar=%s\npvar.resolve()=%s\n",
            pathlib.toString(), pvar.toString(), pvar.resolve().toString() );

        /* output:
            pathlib=<module 'pathlib' from '/usr/local/rampart/modules/python3-lib/pathlib.py'>
            pvar=.
            pvar.resolve()=/path/to/my/current/directory
        */

    Return Value:
        A :green:`String`.

pvar.toValue()
~~~~~~~~~~~~~~

    Translate the python variable referenced in ``pyvar`` to a JavaScript
    variable.

    Example:

    .. code-block:: javascript

        var python=require("rampart-python");
        var printf = rampart.utils.printf;

        var mymod = python.importFile("/path/to/myscript.py");

        var pvar = mymod.makedict("mykey", ["val1", "val2"]);

        printf( "mykey = %s\nmykey.toValue=%3J\n",
            pvar.mykey.toString(), pvar.mykey.toValue() );

        /* output:
            mykey = ('val1', 'val2')
            mykey.toValue=[
               "val1",
               "val2"
            ]
        */

Accessing Attributes and Items
------------------------------

    Attributes of a Python variable are looked up when they are accessed,
    and are read fresh each time.  ``pvar.name`` returns the attribute;
    for dictionaries and other subscriptable objects without such an
    attribute, ``pvar["key"]`` and ``pvar[0]`` return the item (the same
    as ``pvar["key"]`` and ``pvar[0]`` in Python).

    If an item has the same name as an attribute, the attribute is
    returned.  For example, ``df["count"]`` on a pandas DataFrame returns
    the ``count`` method, not a column named "count".  Use
    ``pvar.__getitem__("key")`` to get the item.

    Because attributes are looked up only when accessed, ``Object.keys()``
    and ``for...in`` do not list them.  To list the attributes of a Python
    variable, use Python's ``dir()``:

    .. code-block:: javascript

        var python = require("rampart-python");
        var builtins = python.import("builtins");
        var pathlib = python.import("pathlib");

        var names = builtins.dir(pathlib).toValue();  // an Array of names

Handling Variables
------------------

From Javascript to Python
~~~~~~~~~~~~~~~~~~~~~~~~~

    Variables passed to Python functions are automatically converted as follows:

    +-----------------------+------------------------------------------+
    |    JavaScript Type    | Python Type                              |
    +=======================+==========================================+
    |  :green:`Number`      | Float                                    |
    +-----------------------+------------------------------------------+
    |  :green:`String`      | String                                   |
    +-----------------------+------------------------------------------+
    |  :green:`Array`       | Tuple                                    |
    +-----------------------+------------------------------------------+
    |  :green:`Object`      | Dictionary                               |
    +-----------------------+------------------------------------------+
    |  :green:`Buffer`      | Bytes Object                             |
    +-----------------------+------------------------------------------+
    |  :green:`TypedArray`, | Bytes Object (the underlying bytes)      |
    |  :green:`ArrayBuffer`,|                                          |
    |  :green:`DataView`    |                                          |
    +-----------------------+------------------------------------------+
    |  :green:`Date`        | Datetime (naive, in UTC)                 |
    +-----------------------+------------------------------------------+
    |  :green:`Undefined`   | None                                     |
    +-----------------------+------------------------------------------+
    |  :green:`null`        | None                                     |
    +-----------------------+------------------------------------------+

    Note that JavaScript :green:`Numbers` always convert to Python floats.
    When a Python function requires an integer (common in modules such as
    ``numpy`` and ``torch``), pass it explicitly as
    ``{pyType: "int", value: n}``.

    Dates cross the boundary in UTC: a JavaScript :green:`Date` becomes a
    naive Python datetime holding the UTC time, and a naive datetime
    returned from Python is read as UTC.  Timezone-aware datetimes are
    converted using their own offset.  A round trip returns the original
    time.

    Where possible, translations can be specified by creating an
    :green:`Object` with ``pyType`` and ``value`` properties set.

    Example:

    .. code-block:: javascript

        var python=require("rampart-python");
        var printf = rampart.utils.printf;

        var pyscript=`
        def printvar(v):
            print( "%-30s %s" % (type(v), v))
        `;

        var mymod = python.importString(pyscript);

        mymod.printvar({pyType: "date",    value: 946713599999});
        mymod.printvar({pyType: "int",     value: "1234567800000000000000000000000000000000000000"});
        mymod.printvar({pyType: "list",    value: ["a", "b", "c"]});
        mymod.printvar({pyType: "tuple",   value: "d"});
        mymod.printvar({pyType: "complex", value: [1,2]});
        mymod.printvar({pyType: "dict",    value: "e"});

        /* output:
            <class 'datetime.datetime'>    2000-01-01 07:59:59.999000
            <class 'int'>                  1234567800000000000000000000000000000000000000
            <class 'list'>                 ['a', 'b', 'c']
            <class 'tuple'>                ('d',)
            <class 'complex'>              (1+2j)
            <class 'dict'>                 {'0': 'e'}
        */

From Python to JavaScript
~~~~~~~~~~~~~~~~~~~~~~~~~

    Translation of return values from Python are automatic when using
    ``.toValue()``.  For types which cannot be translated, a string
    representation (same as ``.toString()``) will be returned instead.

    Example:

    .. code-block:: javascript

        var python=require("rampart-python");
        var printf = rampart.utils.printf;

        var pyscript=`
        def retvar(v):
            return v
        `;

        var mymod = python.importString(pyscript);

        var ret;

        ret=mymod.retvar({pyType:"date", value: 946713599999});
        printf("%J\n", ret.toValue());

        ret=mymod.retvar({pyType: "int",     value: "1234567800000000000000000000000000000000000000"});
        printf("%J\n", ret.toValue());

        ret=mymod.retvar({pyType: "list",    value: ["a", "b", "c"]});
        printf("%J\n", ret.toValue());

        ret=mymod.retvar({pyType: "tuple",   value: "d"});
        printf("%J\n", ret.toValue());

        ret=mymod.retvar({pyType: "complex", value: [1,2]});
        printf("%J\n", ret.toValue());

        ret=mymod.retvar({pyType: "dict",    value: "e"});
        printf("%J\n", ret.toValue());

        ret=mymod.retvar(mymod);
        printf("%J\n", ret.toValue());

        /* output:
            "2000-01-01T07:59:59.999Z"
            1.2345678e+45
            ["a","b","c"]
            ["d"]
            [1,2]
            {"0":"e"}
            <module 'module_from_string' from '/home/user/src/mytest.js'>
        */

Python to Python
~~~~~~~~~~~~~~~~

    Variables returned from a Python function can be used as parameters to other Python functions.
    No translation will be performed, including when they are inside an :green:`Array` or
    :green:`Object` (e.g. ``torch.stack([t1, t2])``).

    Example:

    .. code-block:: javascript

        var python=require("rampart-python");
        var printf = rampart.utils.printf;

        var pyscript=`
        def retvar(v):
            return v

        def add(a,b):
            return a+b;
        `;

        var mymod = python.importString(pyscript);

        var a = mymod.retvar({pyType: "complex", value: [1,2]});
        var b = mymod.retvar({pyType: "complex", value: [3,4]});
        var ret = mymod.add(a, b);
        printf("%J\n", ret.toValue());

        /* output:
            [4,6]

           note that var a and b hold the Python variables and are
           not translated when mymod.add(a,b) is called.
       */

Python Named Arguments
~~~~~~~~~~~~~~~~~~~~~~

    Named arguments to Python functions may be used as shown
    in the following example:

    .. code-block:: javascript

        var python=require("rampart-python");
        var printf = rampart.utils.printf;

        var pyscript=`
        def retvar(v):
            return v

        def add(a,b):
            return a+b;
        `;

        var mymod = python.importString(pyscript);

        var comp1 = mymod.retvar({pyType: "complex", value: [1,2]});
        var comp2 = mymod.retvar({pyType: "complex", value: [3,4]});

        var myNamedArgs = { pyArgs: {a:comp1, b:comp2} };

        var ret = mymod.add( myNamedArgs );
        printf("%J\n", ret.toValue());

        /*
           calling in JavaScript:
               mymod.add( {pyArgs: {a:comp1, b:comp2} } );
           is equivalent to calling with named arguments in python:
               add(a=comp1, b=comp2);
        */

Calling rampart from within Python
----------------------------------

When python scripts are executed from within rampart, the ``rampart`` module
is available to python.  It includes two methods: ``call`` and
``triggerEvent``.

rampart.call
~~~~~~~~~~~~

    Call a global rampart function from within python.

    Example:

    .. code-block:: javascript

        var python = require('rampart-python');

        var iscript =
        `
        #when operating from within rampart, the rampart module is available
        import rampart

        #call a rampart global func
        def callRampartFunc(funcName, var1, var2):
            res = rampart.call(funcName, var1, var2);
            print(res);
        `;


        function add(a,b) {
            return [ `${a} + ${b}`, a+b ];
        }

        var r=python.importString(iscript);

        r.callRampartFunc("add", 3, 4);

        /* expected results:
            ('3 + 4', 7.0)
        */


    Notes:

    * An exception thrown by the JavaScript function is raised in Python
      as a ``RuntimeError`` (with the JavaScript error message and stack),
      so it can be caught with ``try``/``except``.
    * ``rampart.call`` may be used from Python threads, such as the thread
      pools used by LangChain's ``batch()`` or by agents running tools, and
      the JavaScript function may itself call Python functions.  JavaScript
      functions called this way run one at a time; however, when one of them
      calls a Python function, others may run until that Python function
      returns (similar to ``await`` in an async function).
    * JavaScript can only be called while it is waiting on a Python function
      to return.  A Python thread that calls ``rampart.call`` after that
      (e.g. a background thread still running after the function that
      started it returned) gets a ``RuntimeError``.

rampart.triggerEvent
~~~~~~~~~~~~~~~~~~~~

    A registered event in a thread may be triggered from within rampart

    Example:

    .. code-block:: javascript

        rampart.globalize(rampart.utils);
        var python = require('rampart-python');

        var iscript =
        `
        #when operating from within rampart, the rampart module is available
        import rampart

        #trigger a rampart event and pass a "triggerVar" to it
        def trigger(eventName, triggervar):
            rampart.triggerEvent(eventName, triggervar);
        `;


        function pytrigger(val,err){
            //check for errors in thrfunc
            if(!val)
                console.log(err);
            // load script into python and return its functions
            var r=python.importString(iscript);
            console.log("trigger myev");
            // execute "trigger" function in python script with a triggervar
            r.trigger("myev","Hello from Python");
        }

        // create a new thread in rampart
        var thr = new rampart.thread();

        // the function which will be run in the rampart.thread.
        function thrfunc() {
            console.log("setup myev");
            // register an event in this thread
            rampart.event.on(
                // the name of the event
                "myev",
                // the name of the function (required but not used here)
                "myfunc",
                // the function to be executed when triggered
                function(uservar,triggervar){
                    printf("Uservar='%s'\nTriggervar='%s'\n", uservar, triggervar);
                    //remove the event so thread is empty of events and rampart can exit
                    rampart.event.remove("myev");
                },
                //the user variable to be passed upon triggering
                "Hello from JS main thread"
            );
            return 1;
        }

        //execute the function thrfunc in the thread, and then run
        //pytriggervar in the main thread.
        thr.exec(thrfunc,pytrigger);

        /* expected results:
            setup myev
            trigger myev
            Uservar='Hello from JS main thread'
            Triggervar='Hello from Python'
        */

Installing Python Packages
--------------------------

    Rampart ships its own Python runtime along with two helper commands in
    the rampart ``bin`` directory:

    * ``pip3r`` - the bundled pip.  Use it to install packages for the
      embedded Python (e.g. ``pip3r install numpy``).
    * ``python3r`` - the bundled Python interpreter, with the same module
      search path as `python.import()`_.

    Where packages are installed depends on whether the rampart
    installation is writable:

    * If the rampart installation is writable (e.g. rampart was installed
      in your home directory, or pip3r is run as root), packages are
      installed system-wide inside the rampart installation.
    * Otherwise pip falls back to a per-user installation in
      ``~/.rampart/modules/python3-lib``.

    Both locations are automatically included in the module search path of
    the embedded interpreter, with per-user packages taking precedence.
    The per-user location may be overridden by setting the
    ``PYTHONUSERBASE`` environment variable before running rampart or
    ``pip3r``.

    Example, as a normal user with rampart installed in a system location:

    .. code-block:: none

        $ pip3r install requests
        Defaulting to user installation because normal site-packages is not writeable
        ...
        Successfully installed requests-2.32.3

    .. code-block:: javascript

        var python = require('rampart-python');

        /* finds the package installed under ~/.rampart */
        var requests = python.import('requests');

Example Use Importing Data
--------------------------

    .. code-block:: javascript

        var python = require('rampart-python');
        var Sql = require('rampart-sql');
        var printf = rampart.utils.printf;

        /* create the rampart sql db*/
        var sql = new Sql.init("./pytest-sql", true);

        /* the sqlite db */
        var dbfile="./test.db";

        /* use python to create and connect to sqlite db */
        var pysql = python.import('sqlite3');
        var connection = pysql.connect(dbfile);
        var cursor = connection.cursor();

        /* create a test table */
        cursor.execute("create table IF NOT EXISTS test(i int, i2 int);");

        /* insert some test data into the db */
        for (var i=0; i<100; i+=2) {
            cursor.execute("insert into test values(?,?)", [i,   i+1]);
        }

        /* print out what we have */
        cursor.execute("select * from test");
        res = cursor.fetchall().toValue();
        printf("Dump of sqlite table:\n%J\n", res);


        /* create rampart sql table and copy data from sqlite */
        sql.exec("create table test (i int, i2 int);");
        for (i=0;i<res.length;i++) {
            sql.exec("insert into test values(?,?);", res[i]);
        }

        var res2 = sql.exec("select * from test", {returnType:"array", maxRows:-1});
        printf("Dump of rampart sql table:\n%J\n", res2.rows);

        /* output:
            Dump of sqlite table:
            [[0,1],[2,3],[4,5],[6,7],[8,9],[10,11],[12,13],[14,15],[16,17],[18,19],[20,21],[22,23],[24,25],
              [26,27],[28,29],[30,31],[32,33],[34,35],[36,37],[38,39],[40,41],[42,43],[44,45],[46,47],[48,49],
              [50,51],[52,53],[54,55],[56,57],[58,59],[60,61],[62,63],[64,65],[66,67],[68,69],[70,71],[72,73],
              [74,75],[76,77],[78,79],[80,81],[82,83],[84,85],[86,87],[88,89],[90,91],[92,93],[94,95],[96,97],[98,99]]
            Dump of rampart sql table:
            [[0,1],[2,3],[4,5],[6,7],[8,9],[10,11],[12,13],[14,15],[16,17],[18,19],[20,21],[22,23],[24,25],
              [26,27],[28,29],[30,31],[32,33],[34,35],[36,37],[38,39],[40,41],[42,43],[44,45],[46,47],[48,49],
              [50,51],[52,53],[54,55],[56,57],[58,59],[60,61],[62,63],[64,65],[66,67],[68,69],[70,71],[72,73],
              [74,75],[76,77],[78,79],[80,81],[82,83],[84,85],[86,87],[88,89],[90,91],[92,93],[94,95],[96,97],[98,99]]

        */
