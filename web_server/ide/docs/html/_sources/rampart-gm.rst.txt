The rampart-gm module
=====================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-gm module uses the
`GraphicsMagick <http://www.graphicsmagick.org/>`_ library.
The authors of Rampart extend our thanks to
`the authors and contributors <http://www.graphicsmagick.org/authors.html>`_
of this library.

License
~~~~~~~

The GraphicsMagick library is distributed under the
`MIT License <http://www.graphicsmagick.org/Copyright.html>`_\ .

The rampart-gm module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-gm module provides image processing capabilities using the
GraphicsMagick library.  It supports opening, manipulating, converting, and
saving images in a wide variety of formats.  Operations include resizing,
cropping, blurring, rotating, color adjustments, and many other
transformations available through GraphicsMagick's mogrify interface.
Multiple images may be combined to produce animated GIFs or other
multi-image documents.

How does it work?
~~~~~~~~~~~~~~~~~

The rampart-gm module provides a single ``open()`` function which returns
an *Image Object*.  That object contains methods for manipulating
images, adding images, selecting images, saving to files or buffers,
and querying image properties.  All mogrify operations supported by
GraphicsMagick are available.

Prerequisites
~~~~~~~~~~~~~

The GraphicsMagick shared libraries must be installed on the system
in order to use this module.

* Debian/Ubuntu: ``sudo apt install libgraphicsmagick-q16-3``
* macOS: ``brew install graphicsmagick``
* FreeBSD: ``pkg install GraphicsMagick``

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var gm = require("rampart-gm");

If the GraphicsMagick libraries are not installed, an error will be thrown
with a message indicating that the libraries are missing.

gm Object Functions
-------------------

open()
~~~~~~

    Open one or more image files and return an *Image Object*.

    Usage:

    .. code-block:: javascript

        var images = gm.open(path);

    Where:

    * ``path`` is a :green:`String` or :green:`Array` of :green:`Strings`,
      the path(s) to image file(s) to open.

    Return Value:
        An *Image Object* with the methods described below.

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");

        // open a single image
        var images = gm.open("/path/to/image.jpg");

        // open multiple images into a single image object
        var images = gm.open(["/path/to/image1.jpg", "/path/to/image2.jpg"]);


Image Object Methods
--------------------

The *Image Object* returned by `open()`_ contains the following
methods.  Unless otherwise noted, methods that do not return a specific
value return the *Image Object* itself, allowing calls to be chained.

.. _gm_add:

add()
~~~~~

    Add one or more images to the current *Image Object*.

    Usage:

    .. code-block:: javascript

        images.add(source);

    Where:

    * ``source`` is a :green:`String` (file path), an *Image Object*
      (as returned from `open()`_), a :green:`Buffer` (as returned from
      `toBuffer()`_), or an :green:`Array` containing any combination of
      the above.

    Return Value:
        The *Image Object* (chainable).

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");
        var images = gm.open("/path/to/image1.jpg");
        var image2 = gm.open("/path/to/image2.jpg");

        // add by path
        images.add("/path/to/image3.jpg");

        // add by image object
        images.add(image2);

        // add by buffer
        images.add(image2.toBuffer("JPG"));

        // add multiple at once
        images.add([image2, "/path/to/image4.jpg"]);


.. _gm_mogrify:

mogrify()
~~~~~~~~~

    Apply one or more GraphicsMagick transformations to all images in the
    *Image Object*.  See the
    `GraphicsMagick documentation <http://www.graphicsmagick.org/mogrify.html>`_
    for a complete list of available options.

    Usage:

    .. code-block:: javascript

        images.mogrify(options[, value]);

    The function accepts several calling conventions:

    * A single :green:`String` containing one or more options and their
      values, separated by spaces.

    * Two :green:`String` arguments: the option name and its value.  The
      leading ``-`` on the option name is optional.

    * An :green:`Object` where each key is an option name and each value is
      the corresponding option value.  Options that take no value should be
      set to ``true``.  Setting an option to ``false`` causes it to be
      skipped.  Options that use the ``+`` prefix (to disable a feature)
      should include the ``+`` in the key name.

    Return Value:
        The *Image Object* (chainable).

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");
        var images = gm.open("/path/to/image.jpg");

        // single string with options and values
        images.mogrify("-blur 20x30 -auto-orient +contrast");

        // option and value as separate arguments
        images.mogrify("-blur", "20x30");
        images.mogrify("blur", "20x30");   // leading "-" is optional

        // object form
        images.mogrify({
            blur: "20x30",
            "auto-orient": true,
            "+contrast": true
        });


.. _gm_save:

save()
~~~~~~

    Save the image(s) to a file.  The output format is determined by the
    file extension.  If the *Image Object* contains multiple images
    and the output format does not support multi-image documents (such as
    GIF), use `select()`_ to choose a single image before saving.

    Usage:

    .. code-block:: javascript

        images.save(path);

    Where:

    * ``path`` is a :green:`String`, the file path to save the image to.

    Return Value:
        The *Image Object* (chainable).

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");
        var images = gm.open("/path/to/image.jpg");

        images.mogrify({blur: "20x30"});
        images.save("/path/to/output.png");


.. _gm_toBuffer:

toBuffer()
~~~~~~~~~~

    Convert the image(s) to a :green:`Buffer` in the specified format.
    This is useful for serving images directly from a web server without
    writing to disk.

    Usage:

    .. code-block:: javascript

        var buf = images.toBuffer(format);

    Where:

    * ``format`` is a :green:`String`, the output format (e.g. ``"JPG"``,
      ``"PNG"``, ``"GIF"``).  The format name is case-insensitive.

    Return Value:
        A :green:`Buffer` containing the image data in the specified format.

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");
        var images = gm.open("/path/to/image.jpg");
        var buf = images.toBuffer("PNG");

        // write the buffer to a file
        rampart.utils.fprintf("/path/to/output.png", "%s", buf);

        // or return from a rampart-server callback
        return {png: buf};


.. _gm_select:

select()
~~~~~~~~

    Select a single image from a multi-*Image Object* by index.
    This is useful when saving to a format that does not support multiple
    images.

    Usage:

    .. code-block:: javascript

        images.select(index);

    Where:

    * ``index`` is a :green:`Number`, the zero-based index of the image
      to select.  Negative indices count from the end (e.g. ``-1`` selects
      the last image).

    Return Value:
        The *Image Object* (chainable).

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");
        var images = gm.open(["/path/to/image1.jpg", "/path/to/image2.jpg"]);

        // select and save the second image
        images.select(1).save("/path/to/output.jpg");

        // select the last image
        images.select(-1).save("/path/to/last.jpg");


.. _gm_list:

list()
~~~~~~

    Return a list of images in the *Image Object*.

    Usage:

    .. code-block:: javascript

        var info = images.list();

    Return Value:
        An :green:`Array` of :green:`Strings`, the file name of each image in
        the *Image Object*.  For a full description of an image, see
        :ref:`identify() <gm_identify>`.

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");
        var images = gm.open(["/path/to/image1.jpg", "/path/to/image2.jpg"]);

        rampart.utils.printf("%3J\n", images.list());

        /* expected output:
           [
              "/path/to/image1.jpg",
              "/path/to/image2.jpg"
           ]
        */


.. _gm_getCount:

getCount()
~~~~~~~~~~

    Return the number of images in the *Image Object*.

    Usage:

    .. code-block:: javascript

        var count = images.getCount();

    Return Value:
        A :green:`Number`, the count of images in the *Image Object*.

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");
        var images = gm.open(["/path/to/image1.jpg", "/path/to/image2.jpg"]);

        rampart.utils.printf("we have %d images\n", images.getCount());
        // output: "we have 2 images"


.. _gm_identify:

identify()
~~~~~~~~~~

    Return descriptive information about the currently selected image,
    similar to the ``gm identify`` command-line tool.

    Usage:

    .. code-block:: javascript

        var info = images.identify([verbose]);

    Where:

    * ``verbose`` is an optional :green:`Boolean`.  If ``true``, return
      detailed information including EXIF data, color profiles, and image
      statistics.  Default is ``false``.

    Return Value:
        An :green:`Object` containing image properties.  At a minimum, the
        returned :green:`Object` includes:

        * ``filename`` - a :green:`String`, the original file name.

        * ``magick`` - a :green:`String`, the image format (e.g. ``"JPEG"``).

        * ``width`` - a :green:`Number`, the image width in pixels.

        * ``height`` - a :green:`Number`, the image height in pixels.

        When ``verbose`` is ``true``, additional properties are returned
        including color depth, channel statistics, EXIF metadata, IPTC
        profiles, and other format-specific details.

    Example:

    .. code-block:: javascript

        var gm = require("rampart-gm");
        var images = gm.open("/path/to/photo.jpg");

        // basic info
        var info = images.identify();
        rampart.utils.printf("Format: %s, Size: %dx%d\n",
            info.magick, info.width, info.height);

        // verbose info with EXIF data
        var detail = images.identify(true);
        rampart.utils.printf("%3J\n", detail);


.. _gm_close:

close()
~~~~~~~

    Close the *Image Object* and free associated resources.  This
    is optional; resources are automatically freed when the variable goes
    out of scope.

    Usage:

    .. code-block:: javascript

        images.close();


Example: Creating an Animated GIF
----------------------------------

.. code-block:: javascript

    var gm = require("rampart-gm");

    // open and configure the first frame
    var images = gm.open("image1.jpg")
        .mogrify({"auto-orient": true})
        .mogrify({delay: 20});   // 200ms per frame

    // open and configure the second frame
    var image2 = gm.open("image2.jpg")
        .mogrify({
            "auto-orient": true,
            delay: 60            // 600ms for this frame
        });

    // combine and save
    images.add(image2);
    images.mogrify({loop: 5});   // stop after 5 loops
    images.save("animated.gif");

Example: Thumbnail Server
--------------------------

.. code-block:: javascript

    var server = require("rampart-server");
    var gm = require("rampart-gm");

    server.start({
        bind: "0.0.0.0:8080",
        map: {
            "/thumbnail": function(req) {
                var images = gm.open("/path/to/photos/" + req.params.file);
                images.mogrify({
                    thumbnail: "256x256",
                    "auto-orient": true,
                    quality: "80"
                });
                return {jpg: images.toBuffer("JPG")};
            }
        }
    });
