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
  Using ``crypto.encrypt("password", data)`` produces the same results as
  ``openssl enc -aes-256-cbc -d -pbkdf2  -pass pass:"password" -in myfile.enc``
  using openssl version 1.1.1 from the command line.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var ciphertext = crypto.encrypt("mypass", "my data", "aes-128-cbc");

decrypt
~~~~~~~

The ``decrypt()`` function takes the same arguments as `encrypt`_ above, but decrypts 
the data.

Return Value:
    A :green:`Buffer` containing the decrypted text.
    Calling ``crypto.decrypt("password", data)`` produces the same results
    as ``openssl enc -aes-256-cbc -d -pbkdf2  -pass pass:"password" -in mydatafile.enc``
    using openssl version 1.1.1 from the command line.

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

RSA Encryption
--------------

rsa_gen_key
~~~~~~~~~~~

Generate an RSA key pair.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var key = crypto.rsa_gen_key([bits][, password]);

Where:

    * ``bits`` is a :green:`Number` such as ``1024``, ``2048``
      ``4096`` or ``8192``.  The number of modulus bits.  Default
      is ``4096`` if not specified.

    * ``password`` is an optional :green:`String`, a password to
      encrypt the private key.

Return:
    An :green:`Object` with the following properties:
    
      * ``public`` - the public key in ``pem`` format.
      * ``private`` - the private key in ``pem`` format, encrypted if
        ``password`` is given.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");
    
    var key = crypto.rsa_gen_key(2048, "mypass");

    rampart.utils.printf("%s\n%s\n", key.private, key.public);

    /* expect output similar to the following:
    -----BEGIN RSA PRIVATE KEY-----
    Proc-Type: 4,ENCRYPTED
    DEK-Info: AES-256-CBC,74913B77FF1C0212CB08E4B4969C0A42

    YNsGbervXJcZzcQEJ+q+HKZ6usp/bEm+UaducORAcEKhOy259LKXCRw9N5kIPwOk
    kAEpDjq64oy86g8Xid8eEXntK+QAJfd96MFBw4fzZxqRFl4rxCVuBy3m9ylGc92s
    yN9wokMbjmk5dSNo7kCP6q6rjHovrk55aiM0GYY4oTMXHr9OWPFdE5ntJ9+E2s6t
    181ePMyMOPFwvw4AwbS+6Ej5/hTGfYpufzWLWxvZC2yTpZybSkZv/SP0EkENEhGk
    wJZiCYz3YwxpnHOc5oLmvZUmhUCxzz0SxSMUlkYfkURwhrp1vXrw8qioXLHV0Wjb
    LK4cAEAmIFSDiitnk4azpMVzLFmIjNIoHD+WK38FqvXmGCqFH08jMPquIGEFtf0N
    JL7Agc6PykRivXCeZtGifj3d5z0C/z1NxPlT2AU3fpSxBgaP31aVDTsF723Die7H
    tH3vrv67Qbq8nVFw8DhkN1/K5vErVI9Cli5MWlZCNnozU1RpNMpQfNEcKpS1ZOGa
    RgvMo7lD8rRECyeqJV0NKO3ENay5s/cV+RWRhpQ2VshQNYhw4XjAZwl3HD/1bVo6
    P8aVbo2evoBpH1hOzpdzApAL2w6qQlhhupcRI009q5l2nmOJBOhCXoGEufHRzSB5
    nFyidHzE0S3Fw9OyyFLGw7ZzZC4J9W5slRVZz+bUhfKWjqcQx6HzITRj/sxrkC1k
    q2lV+hMuzhsZ+kBRPth28Eoo1H5ilLGH80nbKX+w3Rk7nsqlubDFrSdLn3Yq1g0h
    NnjA0x67jjEPBPhQCwR7NPzzJB1Goz4WAmpDsbrtbMDhdaHQKrfduJO9orjbzUWh
    JD+lbF3Hm8WEG653Ap4md7ZlgvhLQY8UZygv3BEyv9CLhwoqzc58q5oK8xtET7lD
    e5aP4W/5UkGWarN02SJo+QQh6aR97UEJGzO25Xf9mYLk7s7dUBs2UJl46EuxZZe+
    dYm6JgbJ7nQfTcrmnCHM3Te4FsB8P4NzDl5bl/TvbDrQ53s8QTsvA3FOTQvLvpLD
    O+NnRHkj7FgwKoOU2/LDXgFQTnAtYv9RbQBUT15Us+dOxOVU58HxA6Y12oOfcie1
    9c6I/40EIEjEBf7ONRfpXadQ1myybZageM+KCZveGhjKHRrD6SZ+JReEyiqRc5no
    Escr1uNdur1b4ORIzCAGDO2PvZY/pHOwXnISeXsH4IBY3u8kx5aYF5JyJx0ny4v7
    C27m98ZPAXyVIKM/bGU4JSjPFdLa0lmJvb/kltowf6Z94DtuIxV72sSptUGhjdBT
    mmiDF+tqVLL/EZbSMeiQj/e1fU/Gtl3BKSygI5NYWGlONLH63sMHrIwe17vTVRmi
    r7cOpayP7M9gdgVjh2fgsZPpdsw/Q0uVxUUI2vrqvoBA0cGl5ZvZX0iIQ3xlssK0
    jj9SvGtFcVD32ZnXex1AKMK1sWErzZF6PEQmNvHwJ0RxxEyPIfXWTfXvC45g79ge
    fh6DooT8V/xBwi2fdblLUyHjPA+WdMl/xKzPekyTsE/b/XVLln02T61MTA4oLAwZ
    k7g8XxqdiSumdTxjA3Jhch+wlH0lD8r73o1zz46dHiz/5ffphuHGxU9Uel7Bekj5
    -----END RSA PRIVATE KEY-----

    -----BEGIN PUBLIC KEY-----
    MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA0V6jNd9YHBopOtk3Sfki
    zcdVftQym5iGZm/C8frhzEG6TGDS9Wj9zijsEEbnxl60sHziaY1DdyEbGQTVvBcd
    fSf2fn9arnWr5r13Gxqdn274Fbc7Ls8dllWhaMlCLsqhTgr83xK0QIR9I7KpyDrx
    Qjyz4AM3jam5j8TR9Y9WyB6tXmWdVaiq0iLGiKJCu5F8rcIGEVcX/t52dKbIbj6j
    X7j/Y+ayTWMNZHZk+ZHVTKsgKn2XFmPbQ6xs5bxTazmWmm7GRDtI5EbqQLMiZAy7
    +P3o6amYz+k5Z9RgLYuNKEyHpOUh8wiNi/CdJ6ScaCoSRmozxqn2NrxdpQQMHBjA
    VQIDAQAB
    -----END PUBLIC KEY-----

    */

rsa_pub_encrypt
~~~~~~~~~~~~~~~

Encrypt data using an RSA public key.

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.rsa_pub_encrypt(data, pubkey[, paddingMode]);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      encrypt.

    * ``pubkey`` is a :green:`String` or :green:`Buffer` with the content of
      the public key.

    * ``paddingMode`` is an optiona :green:`String` that is one of the
      following (as described 
      `here <https://www.openssl.org/docs/man1.1.1/man3/RSA_public_encrypt.html>`_:

        * ``"pkcs"`` - default if not specified.  Use PKCS #1 v1.5 padding.
          This currently is the most widely used mode.
          
        * ``"oaep"`` - Use EME-OAEP as defined in PKCS #1 v2.0 with SHA-1,
          MGF1 and an empty encoding parameter. This mode is recommended for
          all new applications.
          
        * ``"ssl"`` - PKCS #1 v1.5 padding with an SSL-specific modification
          that denotes that the server is SSL3 capable.
          
        * ``"raw"`` - Raw RSA encryption. This mode should only be used to
          implement cryptographically sound padding modes in the application
          code. Encrypting user data directly with RSA is insecure. 
      
      Note that Openssl Library includes this warning:
      
      "Decryption failures in the RSA_PKCS1_PADDING mode leak information
      which can potentially be used to mount a Bleichenbacher padding oracle
      attack. This is an inherent weakness in the PKCS #1 v1.5 padding
      design. Prefer RSA_PKCS1_OAEP_PADDING."  - see 
      `this document <https://www.openssl.org/docs/man1.1.1/man3/RSA_public_encrypt.html>`_.

Note also that the length of ``data`` cannot be more than the number of bits of
the modulus used to create the key pair minus 11 (or minus 42 in the case of
``padding: "oaep``).

Return Value:
    A :green:`Buffer` containing the encrypted text.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var data = ""
    var str  = "contents of my potentially long data file...\n";

    /* make content longer than can fit in rsa encrypted text */
    for (i=0; i<100; i++)
        data+=str;
    
    /* seed the random number generator before use */
    crypto.seed();

    /* generate random data and base64 encode for easy use*/
    var symmetric_passwd = rampart.utils.sprintf("%B", crypto.rand(48));
    
    /* encrypt data using the random base64 data as the password */
    var ciphertext = crypto.encrypt(symmetric_passwd, data);
    
    /* rsa encrypt the password with public key */
    var encrypted_passwd = crypto.rsa_pub_encrypt(
        symmetric_passwd,
        rampart.utils.readFile("pubkey.pem")
    ); 
            
    /* transmit ciphertext and encrypted password to
       owner of the corresponding private key        */


rsa_priv_decrypt
~~~~~~~~~~~~~~~~
Decrypt encrypted data using an RSA private key.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.rsa_priv_decrypt(data, privkey[, paddingMode][, password]);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      decrypt.

    * ``privkey`` is a :green:`String` or :green:`Buffer` with the contents of the 
      private key.

    * ``paddingMode`` - a :green:`String`. See above - the same padding mode used to encrypt
      the data.

    * ``password`` - a :green:`String`, if ``privkey`` is password
      protected, the password used to encrypt the private key.

Return Value:
    A :green:`Buffer` containing the decrypted text.

Example:

.. code-block:: javascript

    /* continuing example from above, owner of privatekey.pem can do this */
    var crypto = require("rampart-crypto");

    /* receive ciphertext and encrypted password from above */

    symmetric_passwd = crypto.rsa_priv_decrypt(
        encrypted_passwd,
        rampart.utils.readFile("privatekey.pem"),
        null, /* use default "pkcs" */
        "mysecretpassword"
    );

    /* decrypt message
       password must be a string */
    var plaintext = crypto.decrypt(
        rampart.utils.bufferToString(symmetric_passwd),
        ciphertext
    );

    rampart.utils.printf("%s", plaintext);

    /* expected output:
    contents of my potentially long data file...
    contents of my potentially long data file...
    ...
    contents of my potentially long data file...
    */


rsa_sign
~~~~~~~~

Sign a message with an RSA private key.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var signature = crypto.rsa_sign(message, privkey[, password]);

Where:

    * ``message`` is a :green:`String` or :green:`Buffer` with the content to
      sign.

    * ``privkey`` is a :green:`String` or :green:`Buffer` with the contents of the 
      private key.

    * ``password`` - a :green:`String`, if ``privkey`` is password
      protected, the password used to encrypt the private key.

Return Value:
    A :green:`Buffer` with the content of the signature.  Same as 
    ``openssl dgst -sha256 -sign privkey.pem -out sig msg.txt``


rsa_verify
~~~~~~~~~~

Verify a signed message with an RSA public key.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var verified = crypto.rsa_verify(data, pubkey, signature);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      sign.

    * ``privkey`` is a :green:`String` or :green:`Buffer` with the contents of the 
      public key.

    * ``signature`` - a :green:`Buffer` containing the signature
      generated with ``rsa_sign`` above, or with openssl.

Return Value:
    A :green:`Boolean` - ``true`` if verification succeeded.  Otherwise
    ``false``. Same as 
    `openssl dgst -sha256 -verify publickey.pem -signature sig msg.txt``.


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

    var res = crypto.hmac(secret, data[, hash_func][, return_buffer]);

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
 