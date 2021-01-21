The rampart-curl module
==============================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-robots module uses 
`Googles Robotstxt library <https://github.com/google/robotstxt>`_.
The authors of Rampart extend our thanks to 
`the authors and contributers <https://github.com/google/robotstxt/graphs/contributors>`_
to this library.

License
~~~~~~~

The Robotstxt library is licensed under the 
`Apache 2.0 License <https://github.com/google/robotstxt/blob/master/LICENSE>`_.
The rampart-robots module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-robots module checks a given URL against a robots.txt file to determine whether
crawling the content is allowed.  For a background on the purpose of the 
robots.txt file, a good primer can be found `here <https://moz.com/learn/seo/robotstxt>`_.


How does it work?
~~~~~~~~~~~~~~~~~

The rampart-robots module exports a single function which takes as its input, a robots.txt
file, a user agent string and a URL and returns an answer indicating whether the download of
the url by the user agent is allowed by the rules set forth in the robots.txt file.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var isAllowed = require("rampart-robots");



Main Function
-------------

The rampart-robots module exports a single function, which in this documentation 
will be named and referred to as ``isAllowed()``.

isAllowed()
~~~~~~~~~~~

    The ``isAllowed`` function takes three :green:`Strings`, the text of a 
    robots.txt file, a user agent string, and a URL.
    
    Usage:

    .. code-block:: javascript
    
        var isAllowed = require("rampart-robots");
        
        var res = isAllowed(user_agent, robotstxt, url); 

    Where:
    
    * ``user_agent`` is a :green:`String`, the name of the user agent to check.

    * ``robotstxt`` is a :green:`String`, the contents of a robots.txt file.

    * ``url`` is a :green:`String`, the URL of the resource to be accessed.
    
    Return Value:
        A :green:`Boolean` - ``true`` if access of the URL is allowed by the 
        ``robotstxt``  rules, or ``false`` if disallowed.

Example
-------

.. code-block:: javascript

    var isAllowed = require("rampart-robots");
    var curl = require("rampart-curl");

    var agent = "myUniqueBotName";
    var rtxt=curl.fetch("https://www.google.com/robots.txt", {"user-agent": agent});
    var url1 = "https://www.google.com/";
    var url2 = "https://www.google.com/search?q=funny+gifs";

    if(rtxt.status != 200) {
        console.log("Failed to download robots.txt file");
        process.exit(1);
    }

    var res1 = isAllowed(agent, rtxt.text, url1); 
    var res2 = isAllowed(agent, rtxt.text, url1);

    /* expected results: 
        res1 == true
        res2 == false
    */
