Rampart Thread Functions
========================

Preface
-------

License
~~~~~~~

The rampart.thread functions are built into the rampart executable
and as such are covered under the same MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart.thread functions provides utility functions to create threads
from within JavaScript and run functions asynchronously in them, as well as
share variables between them.

How does it work?
~~~~~~~~~~~~~~~~~

The rampart.thread functions allow for the creation of native POSIX threads
which when created, are paired with a separate JavaScript interpreter and event
loop.  When created the event loop starts immediately and functions can be
passed into the thread's event loop in a manner similar to the
:ref:`setTimeout() function <rampart-main:setTimeout()>`\ .  Variables can
be provided directly or by putting and getting from an internally maintained
clipboard.  Global variables which are defined and set **when the thread is
created** will be copied to the stack of the newly created JavaScript
interpreter.

The clipboard used by `rampart.thread.put()`_ / `rampart.thread.get()`_ and the
related functions is a single, process-wide namespace shared by **every**
thread (including the main thread), keyed by :green:`String`.  Unrelated
threads that use the same key will see each other's values, so choose key names
accordingly.

Functions passed to a thread with `thr.exec()`_ run in that thread's own event
loop, which starts immediately.  Callbacks and delayed (``threadDelay``)
executions run only while the creating thread's event loop is running.  For the
**main** thread that loop does not start until the top-level script finishes,
so a script that exits before then (for example via ``process.exit()``) will
never run its pending callbacks.


Thread Functions
----------------

new rampart.thread()
~~~~~~~~~~~~~~~~~~~~

    Calling ``new rampart.thread()`` function creates the new thread.

    Usage:

    .. code-block:: javascript

        var thr = new rampart.thread([persist]);

            /* or, with an options object */

        var thr = new rampart.thread([options]);

    Where ``persist`` is a :green:`Boolean`, whether to create a
    `persistent` thread that will keep the thread open when the thread's
    event loop is empty and a parent thread is otherwise ready to exit.

    Normally at the end of a script, the main thread's event loop waits for
    any child threads to finish pending events and allow it to exit.  This
    option thus requires an explicit `thr.close()`_ for a parent thread to
    completely exit.

    Alternatively, an ``options`` :green:`Object` may be passed with the
    following keys:

        * ``keepOpen`` - a :green:`Boolean`, equivalent to the ``persist``
          argument above.

        * ``bare`` - a :green:`Boolean`.  When ``true``, the new thread is
          created with a **minimal** JavaScript heap: only ECMAScript
          primordials (``Math``, ``JSON``, ``Array``, ``Object``,
          ``Promise``, ``Map``/``Set``, etc.) plus the
          ``rampart.thread``/``rampart.event``/``rampart.lock``
          message-passing surface are installed.

          The bare thread does **not** receive:

            - The rest of the ``rampart`` namespace (``rampart.utils``,
              ``rampart.crypto``, ``rampart.sql``, ``rampart.import``,
              ``rampart.vector``, etc.).
            - The ``process`` global.
            - The ``require()`` function.
            - Most WHATWG / Web platform globals (``URL``, ``crypto``,
              ``fetch``, ``setTimeout``, etc.) — these are not installed and
              their lazy getters are not present.  Note that a few remain
              available in a bare thread: ``Blob``, ``File``,
              ``TextEncoder``, ``TextDecoder`` and ``console``.
            - Any properties from the parent thread's ``globalThis`` —
              the normal "copy of globals at thread creation" step is
              skipped (compare to the bullet under `thr.exec()`_
              Caveats: "Only global variables are copied to threads at
              the time of creation").

          This is useful for building sandboxed execution environments —
          for example, the rampart-nodeshim ``vm`` module uses a bare
          thread per ``vm.createContext()`` to give each sandbox its own
          ECMAScript realm with no access to the host's ``rampart``
          surface or globals.  Bare threads can still communicate with
          the parent via ``rampart.thread.put`` / ``get`` / ``getwait`` /
          ``onGet`` / ``waitfor`` / ``del`` because the ``rampart.thread``
          surface is installed.

    Examples:

    .. code-block:: javascript

        /* Legacy boolean — persistent thread. */
        var thr1 = new rampart.thread(true);

        /* Options-object form — equivalent. */
        var thr2 = new rampart.thread({ keepOpen: true });

        /* Bare (sandbox) thread with no host-global leakage. */
        var thr3 = new rampart.thread({ bare: true });

        /* Both options together are supported. */
        var thr4 = new rampart.thread({ bare: true, keepOpen: true });

    Return Value:
        An :green:`Object` with the functions `thr.exec()`_, `thr.close()`_,
        `thr.terminate()`_ and `thr.getId()`_.


thr.exec()
~~~~~~~~~~

    Execute a function in a thread.

    Usage:

    .. code-block:: javascript

        var thr = new rampart.thread();

        thr.exec(options);

            /* or */

        thr.exec(threadFunc[, threadArg[, callbackFunc[, threadDelay]]]);

    Where:

        * ``options`` - an :green:`Object` with optional keys ``threadFunc``,
          ``threadArg``, ``callbackFunc`` and/or ``threadDelay``.

        * ``threadFunc`` - a :green:`Function` that will be executed in the
          thread.

        * ``threadArg`` - a single variable of any type that will be passed to
          the ``threadFunc`` :green:`Function` as its sole parameter.

        * ``callbackFunc`` - a :green:`Function` that will be executed in the
          current event loop (in the thread in which ``new rampart.thread()``
          was called).  It will be passed two parameters
          (``function(value,error){...}``) where either ``value`` (the
          return value of ``threadFunc``, if any) or ``error`` (any errors in thrown
          in ``threadFunc``) may be defined.

        * ``threadDelay`` - a delay, similar to the :ref:`setTimeout() function <rampart-main:setTimeout()>`\ ,
          measured in milliseconds.  If omitted, the ``threadFunc`` :green:`Function`
          will execute immediately.

    Note:
       If no ``callbackFunc`` is provided, errors thrown in ``threadFunc`` wil be printed to stderr.

    Example:

    .. code-block:: javascript

        var iscopied = true;

        var thr = new rampart.thread();

        var notcopied = true;

        function thrfunc(myarg) {
            /* var iscopied is available, var notcopied is not */
            console.log("from inside the thread:", myarg);
            console.log("iscopied =", iscopied)
            console.log("notcopied = ", notcopied);

            return myarg + 1;
        }

        function callback(myarg, err) {
            if(err)
               console.log("error:", err);
            else
               console.log("back in the main thread with myarg = ", myarg);
        }

        thr.exec({
            threadFunc: thrfunc,
            threadArg: 3,
            callbackFunc: callback,
            threadDelay: 1000
        });

        /* or */

        // thr.exec(thrfunc, 3, callback, 1000);

        console.log("end of main, start event loop");

        /*
            output will be:
                end of main, start event loop
                < one second delay >
                from inside the thread: 3
                iscopied = true
                notcopied =  undefined
                back in the main thread with myarg = 4
        */

    Return Value:
        ``undefined``.

    Caveats:

        * Threads may be created in inside threads, but they must be called
          from within the thread in which they were created.

        * :ref:`rampart.utils.fork <rampart-utils:fork>` and
          :ref:`rampart.utils.daemon <rampart-utils:daemon>` will throw an error
          if called while threads are open.

        * Only global variables are copied to threads at the time of
          creation.  The variable ``iscopied`` in the example above will be copied and
          available to the thread as a global variable.  The variable
          ``notcopied`` will not be copied since it was set after the thread was
          created.

        * In the positional form, optional arguments cannot be skipped with a
          ``null`` placeholder --
          ``thr.exec(threadFunc, threadArg, null, threadDelay)`` throws.
          ``undefined`` *is* accepted in that position, but for clarity use
          the ``options`` :green:`Object` form to set ``threadDelay`` without
          a ``callbackFunc``.

        * A ``callbackFunc`` runs in the event loop of the thread that called
          ``new rampart.thread()``.  If that is the main thread, it will not
          run until the top-level script finishes (see `How does it work?`_).

thr.close()
~~~~~~~~~~~

    Close a thread, releasing its resources.

    Example:

    .. code-block:: javascript

      var thr = new rampart.thread(true);

      /* function to be run in child thread */
      function thrfunc(myarg) {
          console.log("from inside the thread:", myarg);
          return myarg + 1;
      }

      /* callback function to be run in main thread */
      function callback(myarg) {
          console.log("back in the main thread:", myarg);
          // terminate the persistent child thread
          thr.close();
      }

      thr.exec(thrfunc, 3, callback, 1000);

      /*
          after one second, output will be:
              from inside the thread: 3
              back in the main thread: 4

          Since thr was created as a persistent thread
          with "thr = rampart.thread(true)"
          it needs to be manually closed.  If not closed
          manually, the main thread will wait forever.
          
      */

    Return Value:
        ``undefined``.

thr.terminate()
~~~~~~~~~~~~~~~

    Forcibly shut down a thread, discarding any future-scheduled events.
    Unlike `thr.close()`_, which politely waits for the thread's event loop
    to drain (and never returns control if subscriptions like
    `rampart.thread.onGet()`_ keep the loop alive), ``terminate()`` causes
    the thread's event loop to exit at the end of its current iteration.

    Behavior:

        * Any JavaScript callback that is **currently executing** in the
          thread completes naturally.  ``terminate()`` does not preempt
          running code.

        * Any callback that is **scheduled but has not yet started** (for
          example, an unfired ``thr.exec()`` with a ``threadDelay`` still
          counting down, or a future ``thread.onGet()`` notification) will
          **not** run.  Its arguments and associated allocations are
          released.

        * Any registered ``thread.onGet()`` subscriptions in the thread
          are torn down.  The thread's event loop drains and the pthread
          exits.

        * Calling ``terminate()`` on a thread that has already exited, or
          calling it more than once, is a safe no-op.

    Usage:

    .. code-block:: javascript

        var thr = new rampart.thread();

        // ... while inside a threaded function or another thread
        thr.terminate();

    Example:

    .. code-block:: javascript

      var thread = rampart.thread;

      var thr = new thread();

      // worker registers an onGet subscription, then idles forever
      thr.exec(function() {
          thread.onGet("cmd", function(key, val) {
              console.log("worker got:", val);
          });
      });

      // give the worker time to subscribe, then send a message
      setTimeout(function() {
          thread.put("cmd", "hello");
      }, 100);

      // forcibly shut down the worker after 500ms.  thr.close() would
      // never succeed here because the onGet subscription keeps the
      // worker's event loop non-empty; terminate() does.
      setTimeout(function() {
          thr.terminate();
      }, 500);

    Return Value:
        ``undefined``.

    Notes:

        * ``terminate()`` is asynchronous.  When it returns to the caller,
          the thread has been *asked* to shut down but may not have done so
          yet (the in-flight JavaScript callback, if any, finishes first).

        * To wait for the thread to actually exit, use a
          `rampart.thread.put()`_ / `rampart.thread.onGet()`_ acknowledgement
          pattern, or simply let the parent's event loop run -- the parent
          will not exit while any child thread is still running.

thr.getId()
~~~~~~~~~~~

    Get ``thr`` thread's unique identification number.  This number is used to
    identify a thread. The number may be reused after the thread is closed.

    Return Value:
        A :green:`Number`, the positive integer for identifying the thread.

rampart.thread.getCurrentId()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

   Return the thread id of the current thread.  Unlike `thr.getId()`_\ ,
   this function returns the thread number of the current thread (i.e. the
   thread in which it is called).

   Return Value:
        A :green:`Number`, the positive integer for identifying the current thread.

rampart.thread.put()
~~~~~~~~~~~~~~~~~~~~

    Put a named variable to the clipboard.

    Usage:

    .. code-block:: javascript

        rampart.thread.put(varName, varValue);

    Where:

        * ``varName`` is a :green:`String`, the key to later retrieve
          ``varValue``.

        * ``varValue`` is any normal JavaScript variable.

    Return Value:
        ``undefined``.

rampart.thread.get()
~~~~~~~~~~~~~~~~~~~~

    Get a named variable from the clipboard.

    Usage:

    .. code-block:: javascript

        var myval = rampart.thread.get(varName[, timeOut]);

    Where:

        * ``varName`` is a :green:`String`, the key used in
          `rampart.thread.put()`_.

        * ``timeOut`` is a positive :green:`Number`.  If provided and the
          return value would be ``undefined``,  `rampart.thread.waitfor()`_
          below will be called with ``varName`` and ``timeOut``.

    Return Value:
        A **copy** of the variable stored with `rampart.thread.put()`_.

    Caveat:
        The value returned is a deep copy taken **when**
        `rampart.thread.put()`_ was called.  Altering the original variable
        after it was put does not affect the stored (or retrieved) value.

    Note:
        ``get()`` is *level-triggered*: if ``varName`` already has a value it
        is returned immediately and ``timeOut`` is ignored.  ``timeOut``
        applies only when the value is currently ``undefined``, in which case
        ``get()`` waits (via `rampart.thread.waitfor()`_) for the next update.
        To block until a value *changes* even when it is already defined, use
        `rampart.thread.waitfor()`_ instead.

    Example:

    .. code-block:: javascript

        var thr1 = new rampart.thread();
        var thr2 = new rampart.thread();

        function thrfunc(myarg) {
            console.log("from inside thread 1:", myarg);
            rampart.thread.put('mycopiedvar', myarg + 10);
            return myarg + 1;
        }

        function callback(myarg) {
            console.log("back in the main thread:", myarg);
        }

        thr1.exec({
            threadFunc: thrfunc,
            threadArg: 3,
            callbackFunc: callback,
            threadDelay: 1000
        });

        thr2.exec(function(){

            // wait a max of five seconds for 'mycopiedvar' to be defined
            var retrieved_var = rampart.thread.get('mycopiedvar', 5000);

            console.log("from inside thread 2:", retrieved_var);
        });

        /* output:
            from inside thread 1: 3
            from inside thread 2: 13
            back in the main thread: 4

           Note that thr1's callbackFunc runs last.  The event loop
           of threads start immediately while the main event loop in
           which the callbackFunc runs starts at the end of the script.
        */

rampart.thread.getwait()
~~~~~~~~~~~~~~~~~~~~~~~~~

    Get a named variable from the clipboard, waiting for it if necessary.

    Usage:

    .. code-block:: javascript

        var myval = rampart.thread.getwait(varName[, timeOut]);

    Where ``varName`` and ``timeOut`` are as for `rampart.thread.get()`_.

    Like `rampart.thread.get()`_, ``getwait()`` returns the current value of
    ``varName`` immediately if one is set.  If the value is currently
    ``undefined`` it waits for another thread to set it -- up to ``timeOut``
    milliseconds if given, or **indefinitely** if ``timeOut`` is omitted.
    (This is the one difference from `rampart.thread.get()`_, which returns
    ``undefined`` immediately when the value is unset and no ``timeOut`` is
    given.)

    Return Value:
        A **copy** of the variable stored with `rampart.thread.put()`_, or
        ``undefined`` if the ``timeOut`` is reached.

rampart.thread.onGet()
~~~~~~~~~~~~~~~~~~~~~~

    Continually listen for changes to a variable on the clipboard.  Run
    a callback for each change.

    Usage:

    .. code-block:: javascript

        var myev = rampart.thread.onGet([varName|varGlob], callback );

    Where:

        * ``varName`` is a :green:`String`, the key used in
          `rampart.thread.put()`_\ .

        * ``varGlob`` is a :green:`String`, ``*`` to match any key used in
          `rampart.thread.put()`_ or ``pref*`` to perform a substring
          match for keys beginning with ``pref``.

        * ``callback`` is a :green:`Function` as such: 
          ``function(key,val,match)`` - where ``key`` is the same as 
          the key used in `rampart.thread.put()`_\ , ``val`` is the updated value
          of the corresponding ``key`` and ``match`` is the ``varName`` or ``varGlob``
          set above.

    Return Value:
        An :green:`Object`, representing the event, and with the function ``remove()``, which
        when called will remove the event.  Note that this :green:`Object` is bound to ``this``
        in the callback.

    Note:
        ``onGet()`` only fires for updates made **after** the subscription is
        registered; a `rampart.thread.put()`_ that happens before the
        ``onGet()`` call is not seen.  When the watcher runs in a worker
        thread, a common pattern is to have the worker register its
        ``onGet()`` and then `rampart.thread.put()`_ a "ready" flag that the
        producer waits on (with `rampart.thread.get()`_) before sending data.

    Note:
        Avoid calling `rampart.thread.put()`_ from inside an ``onGet()``
        callback with a key that would match the callback's own ``varName`` /
        ``varGlob`` -- each such put re-triggers the callback, creating an
        unbounded feedback loop that eventually exhausts the interpreter
        stack.  This is the same hazard as a function that calls itself with
        no base case.  Either give the watcher a base case (call
        ``this.remove()``, or stop putting once a condition is met -- as in
        the example below), or publish to a key that does **not** match the
        watch.

    Example:

    .. code-block:: javascript

      rampart.globalize(rampart.utils)

      var thread = rampart.thread;

      var thr = new thread();
      var x=0; //x is not copied to thread.

      thr.exec(
          // Function is run in thread "thr".
          function() {

              // Set event to match any key that is updated 
              // with thread.put() and begins with 'm'.
              var ev=thread.onGet("m*", function(key,val,match){
                  printf("1 got %s=%s, match=%s extra='%s'\n", key,val,match, this.extra);
                  if(val>3) //stop watching
                      this.remove(); //this == ev
              });

              // Add some extra data available to the above onGet callback
              ev.extra = "my extra data";

              // Match only if "myvar" is updated with thread.put("myvar", val)
              var ev2=thread.onGet("myvar", function(key,val,match){
                  printf("2 got %s=%s, match=%s\n", key,val,match);
                  if(val>1)
                      ev2.remove(); //ev2 == this
              });
              // Two events are now registered in this thread. Return and run
              // function below in main thread.
          },

          // This function run in main thread after above function returns.
          function(){
              var iv=setInterval(function(){
                  // whenever thread.put is called, it triggers the "onGet" event
                  thread.put("myvar",++x);
                  printf("put %d\n", x);

                  if(x>5)
                  {
                      printf("done\n");
                      clearInterval(iv);
                  }
              }, 100);
          }
      );
      /* output:
         put 1
         2 got myvar=1, match=myvar
         1 got myvar=1, match=m* extra='my extra data'
         put 2
         2 got myvar=2, match=myvar
         1 got myvar=2, match=m* extra='my extra data'
         put 3
         1 got myvar=3, match=m* extra='my extra data'
         put 4
         1 got myvar=4, match=m* extra='my extra data'
         put 5
         put 6
         done
      */



rampart.thread.del()
~~~~~~~~~~~~~~~~~~~~

    Get a named variable and remove it from the clipboard.

    Usage:

    .. code-block:: javascript

        var myval = rampart.thread.del(varName[, timeOut]);

    Where:

        * ``varName`` is a :green:`String`, the key used in
          `rampart.thread.put()`_.

        * ``timeOut`` is a positive :green:`Number`.  If provided and the
          return value would be ``undefined``,  `rampart.thread.waitfor()`_
          below will be called with ``varName`` and ``timeOut``.

    Return Value:
        A **copy** of the variable stored with `rampart.thread.put()`_.

rampart.thread.waitfor()
~~~~~~~~~~~~~~~~~~~~~~~~

    Wait for the named variable to be updated by another thread and return
    it.

    .. code-block:: javascript

        var myval = rampart.thread.waitfor(varName[, timeOut]);

    Where:

        * ``varName`` is a :green:`String`, the key used in
          `rampart.thread.put()`_.

        * ``timeOut`` is a positive :green:`Number` in milliseconds.  If
          provided, return ``undefined`` if the varable ``varName`` has not been
          updated within the given time. If ``timeout`` is omitted, the
          function will wait indefinely for the variable to be updated.

    Return Value:
        A **copy** of the variable stored with `rampart.thread.put()`_ or
        ``undefined`` if the ``timeOut`` is reached.

    Note:
        ``waitfor()`` is *edge-triggered*: unlike `rampart.thread.get()`_
        above, it ignores any current value of ``varName`` and returns only
        when the value is next updated by another thread, or when the
        ``timeOut`` is reached.

Lock Functions
--------------

new rampart.lock()
~~~~~~~~~~~~~~~~~~

     Calling ``new rampart.lock()`` function creates a new
     `POSIX backed mutex <https://linux.die.net/man/3/pthread_mutex_lock>`_
     which can be used to isolate critical sections of code running in
     multiple threads.

    Usage:

    .. code-block:: javascript

        var thrlock = new rampart.lock();

    Return Value:
        An :green:`Object` with the functions `thrlock.lock()`_,
        `thrlock.unlock()`_, `thrlock.trylock()`_ and
        `thrlock.getLockingThread()`_.

thrlock.lock()
~~~~~~~~~~~~~~

    Lock the mutex referenced by ``thrlock``.  If the mutex is not locked,
    a lock will be obtained.  If the mutex is locked in another thread,
    execution in the current thread will be paused until the other thread
    calls `thrlock.unlock()`_ and a lock will be obtained.

    Usage:

    .. code-block:: javascript

        var thrlock = new rampart.lock();

        // ... while inside a threaded function

        thrlock.lock();

    Return Value:
        ``undefined``.

thrlock.unlock()
~~~~~~~~~~~~~~~~

    Unlock the mutex referenced by ``thrlock``.

    Usage:

    .. code-block:: javascript

        var thrlock = new rampart.lock();

        // ... while inside a threaded function

        thrlock.lock();
        /* critical section */
        thrlock.unlock();

    Return Value:
        ``undefined``.

thrlock.trylock()
~~~~~~~~~~~~~~~~~

    Same as `thrlock.lock()` except it always returns immediatedly.

    Usage:

    .. code-block:: javascript

        var thrlock = new rampart.lock();

        // ... while inside a threaded function

        if(thrlock.trylock())
        {
            /* critical section */
            thrlock.unlock();
        } else {
            /* do something else */
        }

    Return Value:
        A :green:`Boolean`: ``true`` if the lock was obtained and ``false``
        if not.

thrlock.getLockingThread()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    Return the id of the thread currently holding ``thrlock`` (the same id
    space as `rampart.thread.getCurrentId()`_), or ``-1`` if the lock is not
    currently held.

    Usage:

    .. code-block:: javascript

        var holder = thrlock.getLockingThread();

    Return Value:
        A :green:`Number`: the holding thread's id, or ``-1`` if unlocked.

Lock Caveats
~~~~~~~~~~~~

    * A mutex locked in a thread must be unlocked in the same thread.

    * A ``rampart.lock`` must be created as a global variable before the
      threads in which it will be used are created so that the ``thrlock``
      variable is copied to each thread.  Alternatively a ``rampart.lock``
      can be passed as a ``threadArg`` to `thr.exec()`_\ .

    * Normal operations do not require explicit locking using
      ``rampart.lock``.  However updating variables on the clipboard might
      require using a lock.

    Example:

    .. code-block:: javascript

        var thread=rampart.thread;

        // thrlock is a global, and will be copied to thr1 and thr2 below.
        var thrlock = new rampart.lock();

        var thr1 = new thread();
        var thr2 = new thread();

        // copy 0 to the clipboard prior to executing functions in threads.
        thread.put("i", 0);

        thr1.exec(function() {
            var i, j=0;

            for(j=0; j<50; j++)
            {
                //get the variable and increment, blocking thr2
                thrlock.lock();

                i=thread.get("i");
                i++;
                thread.put("i", i);

                thrlock.unlock();
            }
        });

        thr2.exec(function() {
            var i, j=0;

            for(j=0; j<50; j++)
            {
               //get the variable and increment, blocking thr1
                thrlock.lock();

                i=thread.get("i");
                i++;
                thread.put("i", i);

                thrlock.unlock();
            }

            rampart.utils.sleep(0.25);
            i=thread.get("i");
            console.log("i =",i);
        });

        /* output:
            i = 100
        */
