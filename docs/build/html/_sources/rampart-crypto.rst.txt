The rampart-crypto module
==============================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-crypto module uses the `OpenSSL <https://www.openssl.org/>`_
library.  We extend our thanks to the developers for this indispensable
tool.

License
~~~~~~~

The rampart-crypto module is released under the MIT license. 
The `OpenSSL <https://www.openssl.org/>`_ library is released under the
`Apache 2.0 License <https://github.com/openssl/openssl/blob/master/LICENSE.txt>`_\ .

What does it do?
~~~~~~~~~~~~~~~~

The rampart-crypto module provides methods to encrypt, decrypt, hash and
generate HMACs from within Rampart JavaScript.
It also includes the full libssl and libcrypto libraries and is needed for
the rampart-curl and rampart-server modules to operate using the https
protocol.


How does it work?
~~~~~~~~~~~~~~~~~

After the module is loaded, functions are provided to perform crypto
operations on JavaScript :green:`Strings` or :green:`Buffers`.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

Encryption and Decryption
-------------------------

encrypt
~~~~~~~

The ``encrypt()`` function encrypts the contents of a :green:`String` or
:green:`Buffer`.  Encryption can be done by providing a key/iv pair or by
providing a password.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    crypto.encrypt(options);

    /* or */

    crypto.encrypt(pass, data[, cipher_mode]);


Where

* ``pass`` is a password used to encrypt the data.

* ``data`` is a :green:`String` or :green:`Buffer`, the data to be
  encrypted.

* ``cipher_mode`` is one of the modes listed below.  If not specified,
  the default is ``aes-256-cbc``.

* ``options`` is an :green:`Object` which may contain the following:

  * ``data`` - same as ``data`` above.

  * ``cipher`` - same as ``cipher_mode`` above.

  *  ``pass`` - a password used to generate a key/iv pair and encrypt the
     data.

  * ``key`` - required if not using a password - a key of the appropriate length for
    the chosen cipher.

  * ``iv`` - required if not using a password - an initialization vector of
    the appropriate length to be used for encrypting the data.

  * ``iter`` - number of iterations for generating a key from ``pass``. 
    Default is ``10000``.  If provided, the same must be passed to
   `decrypt`_ below in order to decrypt the ciphertext.

Return Value:
  A :green:`Buffer` containing the ciphertext (encrypted data).

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var ciphertext = crypto.encrypt("mypass", "my data", "aes-128-cbc");

decrypt
~~~~~~~

The ``decrypt()`` function takes the same arguments as `encrypt`_ above, but decrypts 
the data.  Data is returned in a :green:`Buffer`.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var ciphertext = crypto.encrypt({
        pass: "mypass", 
        data: "my data"
    });

    var plaintext = crypto.decrypt({
        pass: "mypass", 
        data: ciphertext
    });

    printf('The decrypted data: "%s"\n', plaintext);

    /* expected output:

    The decrypted data: "my data"

    */

Supported Modes
~~~~~~~~~~~~~~~

The following cipher/modes are supported in rampart:

+---------------------+--------------------------------------+
|mode name            |Description                           |
+=====================+======================================+
|bf-cbc               |Blowfish in CBC mode                  |
+---------------------+--------------------------------------+
|bf-cfb               |Blowfish in CFB mode                  |
+---------------------+--------------------------------------+
|bf-ecb               |Blowfish in ECB mode                  |
+---------------------+--------------------------------------+
|bf-ofb               |Blowfish in OFB mode                  |
+---------------------+--------------------------------------+
|cast-cbc             |CAST in CBC mode                      |
+---------------------+--------------------------------------+
|cast5-cbc            |CAST5 in CBC mode                     |
+---------------------+--------------------------------------+
|cast5-cfb            |CAST5 in CFB mode                     |
+---------------------+--------------------------------------+
|cast5-ecb            |CAST5 in ECB mode                     |
+---------------------+--------------------------------------+
|cast5-ofb            |CAST5 in OFB mode                     |
+---------------------+--------------------------------------+
|des-cbc              |DES in CBC mode                       |
+---------------------+--------------------------------------+
|des-cfb              |DES in CBC mode                       |
+---------------------+--------------------------------------+
|des-ofb              |DES in OFB mode                       |
+---------------------+--------------------------------------+
|des-ecb              |DES in ECB mode                       |
+---------------------+--------------------------------------+
|des-ede-cbc          |Two key triple DES EDE in CBC mode    |
+---------------------+--------------------------------------+
|des-ede              |Two key triple DES EDE in ECB mode    |
+---------------------+--------------------------------------+
|des-ede-cfb          |Two key triple DES EDE in CFB mode    |
+---------------------+--------------------------------------+
|des-ede-ofb          |Two key triple DES EDE in OFB mode    |
+---------------------+--------------------------------------+
|des-ede3-cbc         |Three key triple DES EDE in CBC mode  |
+---------------------+--------------------------------------+
|des-ede3             |Three key triple DES EDE in ECB mode  |
+---------------------+--------------------------------------+
|des-ede3-cfb         |Three key triple DES EDE CFB mode     |
+---------------------+--------------------------------------+
|des-ede3-ofb         |Three key triple DES EDE in OFB mode  |
+---------------------+--------------------------------------+
|desx                 |DESX algorithm.                       |
+---------------------+--------------------------------------+
|idea-cbc             |IDEA algorithm in CBC mode            |
+---------------------+--------------------------------------+
|idea-cfb             |IDEA in CFB mode                      |
+---------------------+--------------------------------------+
|idea-ecb             |IDEA in ECB mode                      |
+---------------------+--------------------------------------+
|idea-ofb             |IDEA in OFB mode                      |
+---------------------+--------------------------------------+
|rc2-cbc              |128 bit RC2 in CBC mode               |
+---------------------+--------------------------------------+
|rc2-cfb              |128 bit RC2 in CFB mode               |
+---------------------+--------------------------------------+
|rc2-ecb              |128 bit RC2 in ECB mode               |
+---------------------+--------------------------------------+
|rc2-ofb              |128 bit RC2 in OFB mode               |
+---------------------+--------------------------------------+
|rc2-64-cbc           |64 bit RC2 in CBC mode                |
+---------------------+--------------------------------------+
|rc2-40-cbc           |40 bit RC2 in CBC mode                |
+---------------------+--------------------------------------+
|rc4                  |128 bit RC4                           |
+---------------------+--------------------------------------+
|rc4-40               |40 bit RC4                            |
+---------------------+--------------------------------------+
|aes-256-cbc          |256 bit AES in CBC mode               |
+---------------------+--------------------------------------+
|aes-256-cfb          |256 bit AES in 128 bit CFB mode       |
+---------------------+--------------------------------------+
|aes-256-cfb1         |256 bit AES in 1 bit CFB mode         |
+---------------------+--------------------------------------+
|aes-256-cfb8         |256 bit AES in 8 bit CFB mode         |
+---------------------+--------------------------------------+
|aes-256-ecb          |256 bit AES in ECB mode               |
+---------------------+--------------------------------------+
|aes-256-ofb          |256 bit AES in OFB mode               |
+---------------------+--------------------------------------+
|aes-192-cbc          |192 bit AES in CBC mode               |
+---------------------+--------------------------------------+
|aes-192-cfb          |192 bit AES in 128 bit CFB mode       |
+---------------------+--------------------------------------+
|aes-192-cfb1         |192 bit AES in 1 bit CFB mode         |
+---------------------+--------------------------------------+
|aes-192-cfb8         |192 bit AES in 8 bit CFB mode         |
+---------------------+--------------------------------------+
|aes-192-ecb          |192 bit AES in ECB mode               |
+---------------------+--------------------------------------+
|aes-192-ofb          |192 bit AES in OFB mode               |
+---------------------+--------------------------------------+
|aes-128-cbc          |128 bit AES in CBC mode               |
+---------------------+--------------------------------------+
|aes-128-cfb          |128 bit AES in 128 bit CFB mode       |
+---------------------+--------------------------------------+
|aes-128-cfb1         |128 bit AES in 1 bit CFB mode         |
+---------------------+--------------------------------------+
|aes-128-cfb8         |128 bit AES in 8 bit CFB mode         |
+---------------------+--------------------------------------+
|aes-128-ecb          |128 bit AES in ECB mode               |
+---------------------+--------------------------------------+
|aes-128-ofb          |128 bit AES in OFB mode               |
+---------------------+--------------------------------------+

Hashing
-------


hash
~~~~

The ``hash()`` function calculates a hash of the data in a :green:`String` or
:green:`Buffer` and returns it in a hex encoded :green:`String` or as 
binary data in a :green:`Buffer`.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = hash(data[, hash_func][, return_buffer]);

Where:

* ``data`` is a :green:`String` or :green:`Buffer`, the data to be
  hashed.

* ``hash_func`` is an optional :green:`String`, one of the following:

  ``sha1``, ``sha224``, ``sha256``, ``sha384``, ``sha512``, ``md4``, ``md5``, ``sha3-224``,
  ``sha3-256``, ``sha3-384``, ``sha3-512``, ``blake2b512``, ``blake2s256``, ``mdc2``,
  ``rmd160``, ``sha512-224``, ``sha512-256``, ``shake128``, ``shake256``,
  ``sm3``. Default is ``sha256``.

* return_buffer is a :green:`Boolean`, if ``true``, the output will be
  binary data in a :green:`Buffer`, and not hex encoded.


Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.hash("hello world", "sha256");

    /* 
        res == 'b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9'

    */

alias functions
~~~~~~~~~~~~~~~

The hash function has an alias for each of the possible ``hash_func``
value above.  Thus, using ``crypto.hash("hello world", "sha256")`` is equivalent to
``crypto.sha256("hello world")``.  For ``hash_func`` names with a dash
(``-``), an underscore (``_``) is used instead.  Thus 
``crypto.hash("hello world", "sha3-256")`` is equivalent to 
``crypto.sha3_256("hello world")``.

hmac
~~~~

The ``hmac()`` function computes a HMAC from the provided data and key.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = hmac(secret, data[, hash_func][, return_buffer]);

Where:

* ``secret`` is the HMAC function key.

* ``data`` is a :green:`String` or :green:`Buffer`, the data to be
  hashed.

* ``hash_func`` is an optional :green:`String`, one of the following:

  ``sha1``, ``sha224``, ``sha256``, ``sha384``, ``sha512``, ``md4``, ``md5``, ``sha3-224``,
  ``sha3-256``, ``sha3-384``, ``sha3-512``, ``blake2b512``, ``blake2s256``, ``mdc2``,
  ``rmd160``, ``sha512-224``, ``sha512-256``,
  ``sm3``. Default is ``sha256``.

* return_buffer is a :green:`Boolean`, if ``true``, the output will be
  binary data in a :green:`Buffer`, and not hex encoded.

Random
------

rand
~~~~

The ``rand()`` function returns random generated bytes in a buffer.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.rand(nBytes);

Where ``nBytes`` is the number of bytes to return.

Return Value:
    a :green:`Buffer` with ``nBytes`` bytes of random data.

randnum
~~~~~~~

The ``randnum()`` function returns a random :green:`Number`
between ``0.0`` and ``1.0``.

gaussrand
~~~~~~~~~

The ``gaussrand([sigma])`` function returns a random :green:`Number` based on
a normal distribution centered at zero (``0.0``), where ``sigma`` is one
standard deviation.  ``sigma`` is optional, defaulting to ``1.0``.

normrand
~~~~~~~~

The ``normrand([scale])`` function returns a random :green:`Number` based on
a normal distribution centered at zero (``0.0``) and clamped between ``-scale``
and ``scale``.

Similar to the `gaussrand`_ above.  It is equivelant to:

.. code-block:: javascript

    var nrand = scale * crypto.gaussrand(1.0)/5.0;

    if(nrand>scale)
        nrand=scale;
    else if (nrand < -scale)
        nrand = -scale;   


With a ``scale`` of ``1.0`` (the default), the distribution of numbers has a
standard deviation of ``0.2``.

seed
~~~~

The ``seed()`` function seeds the random number generator from a file.
There is no return value.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.seed([options]);

Where options is an :green:`Object`, if provided, and contains the following
properties:

* ``file`` - a :green:`String` - location of the file.  Default is
  ``/dev/random``.

* ``bytes`` - a :green:`Number` - Number of bytes to retrieve from ``file``. 
  Default is ``32``.
 