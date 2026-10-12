The rampart-crypto module
=========================

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
The `OpenSSL <https://www.openssl.org/>`_ 4.0.0 library is released under the
`Apache License 2.0 <https://github.com/openssl/openssl/blob/openssl-4.0/LICENSE.txt>`_\ .

What does it do?
~~~~~~~~~~~~~~~~

The rampart-crypto module provides methods to encrypt, decrypt, hash and
generate HMACs from within Rampart JavaScript.
It also includes the full libssl and libcrypto libraries and is needed for
the rampart-net, rampart-curl and rampart-server modules to operate using
TLS and the https protocol.


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

    var ciphertext = crypto.encrypt(options);

    /* or */

    var ciphertext = crypto.encrypt(pass, data[, cipher_mode]);


Where:

* ``pass`` is a password used to encrypt the data.

* ``data`` is a :green:`String` or :green:`Buffer`, the data to be
  encrypted.

* ``cipher_mode`` is one of the `Supported Modes`_ listed below.  If not specified,
  the default is ``aes-256-cbc``.

* ``options`` is an :green:`Object` which may contain the following:

  * ``data`` - same as ``data`` above.

  * ``cipher`` - same as ``cipher_mode`` above.

  *  ``pass`` - a password used to generate a key/iv pair and encrypt the
     data.

  * ``key`` - required if not using a password - a key of the appropriate length for
    the chosen cipher. ``key`` can be a :green:`Buffer` or a hex encoded :green:`String`.

  * ``iv`` - required if not using a password - an initialization vector of
    the appropriate length to be used for encrypting the data. ``iv`` can be
    a :green:`Buffer` or a hex encoded :green:`String`.

  * ``iter`` - number of iterations for generating a key and iv from ``pass``.
    Default is ``10000``.  If provided, the same value must be passed to
    `decrypt`_ below in order to decrypt the ciphertext.

  * ``aad`` - a :green:`String` or :green:`Buffer`.  Additional
    authenticated data for AEAD modes (``aes-*-gcm``, ``aes-*-ccm``,
    ``aes-*-ocb``, ``chacha20-poly1305``).  Bound to the
    authentication tag but not encrypted.  The same ``aad`` must
    be passed to `decrypt`_.  Ignored for non-AEAD modes.

  * ``tagLength`` - a :green:`Number`, the AEAD tag length in bytes
    (default ``16``).  Used only for AEAD modes.  The tag is appended
    to the ciphertext on encrypt and stripped on decrypt.

Note on AEAD ciphers (``-gcm``, ``-ccm``, ``-ocb``, ``chacha20-poly1305``):
  Ciphertext output is the encrypted bytes followed by the
  authentication tag.  ``decrypt`` validates the tag and throws if it
  fails.  Use ``aad`` to bind unencrypted associated data (e.g. a
  header).

Note on key-wrap ciphers (``aes-*-wrap``):
  RFC 3394 key wrapping uses a fixed IV defined by the spec, so the
  ``iv`` option must not be supplied (an error is thrown).  Input
  ``data`` must be a multiple of 8 bytes, at least 16 bytes long.
  Output is the input length plus 8 bytes of integrity overhead.

Return Value:
  A :green:`Buffer` containing the ciphertext (encrypted data).
  Using ``crypto.encrypt("password", data)`` produces the same results as
  ``openssl enc -aes-256-cbc -e -pbkdf2  -pass pass:"password" -in myfile.txt``
  using openssl version 1.1.1 from the command line.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var ciphertext = crypto.encrypt("mypass", "my data", "aes-128-cbc");

Caveat:
  The choice of ``10000`` iterations is the default used by both the command line
  ``openssl`` tool and rampart-crypto. It is purposefully slow, in order to make
  dictionary attacks on the password difficult.  If computational speed is a
  factor (e.g. in a HTTP server context), choosing a password of random characters
  and significantly lowering the ``iter`` value (or using the ``key`` and ``iv`` 
  options instead of a password) will be more performant.

decrypt
~~~~~~~

The ``decrypt()`` function takes the same arguments as `encrypt`_ above, but decrypts 
the data.

Return Value:
    A :green:`Buffer` containing the decrypted text.
    Calling ``crypto.decrypt("password", data)`` produces the same results
    as ``openssl enc -aes-256-cbc -d -pbkdf2  -pass pass:"password" -in myfile.enc``
    using openssl version 4.0.0 from the command line.

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

    rampart.utils.printf('The decrypted data: "%s"\n', plaintext);

    /* expected output:

    The decrypted data: "my data"

    */

passToKeyIv
~~~~~~~~~~~

The ``passToKeyIv()`` function performs the same password to 
key/iv pair generation as `encrypt`_ above.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var kiv = crypto.passToKeyIv(options);

Where

* ``options`` is an :green:`Object` which may contain the following:

  * ``cipher`` is a :green:`String` and is one of the `Supported Modes`_
    listed below.  If not specified, the default is ``aes-256-cbc``.  This
    option controls the key and iv length.

  * ``pass`` is a :green:`String`, the password used to generate a key/iv pair.

  * ``salt`` is a :green:`String` or :green:`Buffer`, the optional salt for generation 
    of the key and iv.  If not provided, a random salt will be generated. 
    If provided as a :green:`String` it must be a hex encoded string representing at
    least 8 bytes.  If provided as a :green:`Buffer`, it must be at least 8 bytes in length. 
    If longer than 8 bytes, only the first 8 bytes will be used.

  * ``iter`` - number of iterations for generating a key and iv from ``pass``. 
    Default is ``10000``.

  * ``returnBuffer`` is an :green:`Boolean`, if ``true`` the key, iv and salt will be returned
    as binary data in :green:`Buffers`.  Otherwise if not set or ``false``, they will be encoded as a hex
    :green:`Strings`.

Return Value:
  An :green:`Object` containing the key, iv and salt as hex encoded :green:`Strings` or
  as binary data in :green:`Buffers`.
  The function ``crypto.passToKeyIv`` produces the same results as
  ``openssl enc -<cipher_mode> -pbkdf2  -k <password> [-S <salt_as_hex>] -P``
  using openssl version 1.1.1 from the command line.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var salt = crypto.sha1("a unique string for one time use as salt");

    var kiv = crypto.passToKeyIv({
       pass: "mypass",
       salt: salt
    });

    var ciphertext = crypto.encrypt({
        key:  kiv.key,
        iv:   kiv.iv,
        data: "my data"
    });

    /* 
       note that when key/iv is used in encrypt instead of a password, salt
       is not stored in the ciphertext, and the ciphertext must be decrypted
       with the same key, iv derived using both 'password' and 'salt'.
    */

    var plaintext = crypto.decrypt({
        key:  kiv.key,
        iv:   kiv.iv,
        data: ciphertext
    });

    rampart.utils.printf('Key/Iv/Salt: "%3J"\n\n', kiv);

    rampart.utils.printf('The decrypted data: "%s"\n', plaintext);

    /* expected output:

    Key/Iv/Salt: "{
       "key": "215a744a875c4604046f05a34164507cf9f8c54342f75b1d58ad5d1f428aadd2",
       "iv": "daffcd9ff10128eee4f19375f1aa4dde",
       "salt": "ce37ddf6cda911f4"
    }"

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
|aes-128-gcm          |128 bit AES in GCM mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-192-gcm          |192 bit AES in GCM mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-256-gcm          |256 bit AES in GCM mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-128-ccm          |128 bit AES in CCM mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-192-ccm          |192 bit AES in CCM mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-256-ccm          |256 bit AES in CCM mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-128-ocb          |128 bit AES in OCB mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-192-ocb          |192 bit AES in OCB mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-256-ocb          |256 bit AES in OCB mode (AEAD)        |
+---------------------+--------------------------------------+
|aes-128-wrap         |128 bit AES key wrap (RFC 3394)       |
+---------------------+--------------------------------------+
|aes-192-wrap         |192 bit AES key wrap (RFC 3394)       |
+---------------------+--------------------------------------+
|aes-256-wrap         |256 bit AES key wrap (RFC 3394)       |
+---------------------+--------------------------------------+
|chacha20             |ChaCha20 stream cipher (no auth tag)  |
+---------------------+--------------------------------------+
|chacha20-poly1305    |ChaCha20-Poly1305 AEAD (RFC 8439)     |
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

Return Value:
    An :green:`Object` with the following properties:
    
      * ``public`` - the public key in pkcs8 ``pem`` format.
      * ``private`` - the private key in pkcs8 ``pem`` format, encrypted if
        ``password`` is given.

      * ``rsa_public`` - the public key in pkcs1 rsa public key ``pem`` format.
      * ``rsa_private`` - the private key in pkcs1 rsa private key ``pem`` format, encrypted if
        ``password`` is given.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    /* for demo - generally should be 2048 or greater */
    var key = crypto.rsa_gen_key(1024, "mypass");

    rampart.utils.printf( "%s\n%s\n%s\n%s\n", 
       key.private, 
       key.public,
       key.rsa_private,
       key.rsa_public
    );

    /* expect output similar to the following:
    -----BEGIN ENCRYPTED PRIVATE KEY-----
    MIIC3TBXBgkqhkiG9w0BBQ0wSjApBgkqhkiG9w0BBQwwHAQI5x3aqPg9MqgCAggA
    MAwGCCqGSIb3DQIJBQAwHQYJYIZIAWUDBAEqBBAoAyT6LBBnFh3Hd7HhQp+XBIIC
    gHv1acYJPZeNkeTIVX2531fJXmRhWYC1CA6T6eb6fSTLo7ZEnX1kYA34kyhyhj0R
    MOi1mkCZSkdsf8Z/emRCHycWcuJqtAscwpBfURHcTKTzOb2MwQ8hnNLc4lmLOwD2
    Vp6TwqO1JRrR+xeoLuTas+vfzklaRX1c4zSfAU9S2GXdXHJbCtvnFY5HrpMnm0bb
    5d9q0SuMXUFVQM5R5EcXwu7mwuVQbNFK1LZEggzBjdueq5mF3MDvLwaDvoOIffz1
    dPKoj4YPwCFT/RCUhBFz16uHXKK2glPYVYQ2/LYpJK9+hKvWYLWg5veqNyu5TMjb
    crLKvgKE0k/5eJb89hWkOTn00+pcP3b0jAF/iSSwbOokW0H7gZChjRy2CFuJf+t6
    Gx0kndn2hV1722XDaPj+L3tQrjmatSdYEUPMLYfY8NED54GbXndBRY27zJ8ulSjS
    GbMW6iwB2jdO5kKkZrjechLt3pJOC4W6BKlrZXESnZO9TIy1/erwMg3ppId0RtKT
    HgC7b8q8Vw/+9rwi3ksyqWcsEC+CCOaCTjfr6JOiDG1EFQ+wBH4ysoojjo3AQGjY
    mve01KNEBD14+SdLO1Tm6wJfHarUDV0EliSr9cXHHUTZPkFLa4n06C31GfD1McJM
    ky9gSK59qP6n55YDEokVeT6Ei7Q+tgBftg+HisP5QUU2pzlmE5kBfb8lSizUW/Pj
    uBoEVedCxAHQ3Yl3TrMv5URNkFhb3Prsb5YTm7lczEsmk80NAF+obl7iqii4X4Wn
    E2QYpF370fhUmjYsA2G0xugYI+uOf6DepUUEan20SsLRWQk5cqrIFnJlNbnKzaRt
    FaY/wG/NAIHOVONb87bu1Z4=
    -----END ENCRYPTED PRIVATE KEY-----

    -----BEGIN PUBLIC KEY-----
    MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQC7lkrZ6gREJZT6ZWjvFxrm+lPY
    dyE1uplaTbV87AirYHfQRTef3y87B/yL/Qud3brcPUePryqNz20wxZk8hDe0PAHC
    IcM1c3STPxAvo+YJXbjt6DmoC+UK9nkIKXLg1lR9VMVYr9Gri8KWmyXAxHdmTSpf
    njNlXdlur240f9negQIDAQAB
    -----END PUBLIC KEY-----

    -----BEGIN RSA PRIVATE KEY-----
    Proc-Type: 4,ENCRYPTED
    DEK-Info: AES-256-CBC,19216181CEB7C4E59C2D9B8F1F8E4323

    E+1namuNDSCDzRU2O5tq5t78zQ4EobMVNLXzy0yjA9IN1zW6IMxH5WE7wZ48/FZl
    uRFokQrr1xMJB667U0ZtSMS0Ol3q1DZAZvOakpXV21LtKgVEO/E3XM+la5+O+hJ3
    Nhzb58gJKnC3GfexxlxrLeFx+rXwtYNY0wZqAy9yo+QNHEE7JZgYqHipIfxhKlqx
    hmYA5c3ztd7+j2aEq4boRWQdqL5GBzjhAOKYi7goic3SU/kQQmsu7bA7q4KqVn8P
    l0aygNweimO9xkFuZrngdtVeZ/8nA6TsNVJOyI6NanA/iV7SuGYXczqP198P62m7
    2sJkHGJwiR0X6tb95+0sjEofujbRv/6eFV1Tv8r42zEXkESet1XMjxOoEwBLWLbH
    +5RThxkGLfAWDsssq6bo9ilgw2qI0xW9CEtcBmkn574+j0ScIk/2J69cyiIdJNZn
    WNtC0mzKGHMEn+xpYsszyUbS7EgAg3LrV0irl2Kbjm3xTgtKhRXXC7lqbrBoAJF4
    gwwfusEF9jNMoWBkl15oIuUK2/PIgd4IRVBDGX76pcjoTIeTRqulsXuxcl6GKHm9
    KskhZBP08MlN7j4cXc7GmmO4MnzghHNUeqs3Aok2JV4ulimL/7IiaJFQvh01WVk+
    hrQUPnjRVnSzHejBNFqCFCr9XKh72NbTr/6qvzJg8pIjNemb2Vo4rrc2ITzHcS/g
    O88JtrnZjroB37Av6ELTrqJ/G02pdVs8i8FEb/Vnvd6MsTaSwSHJEAFMP6LqhPI0
    ukVkqYB7E1HL0iWS3mgC7eLmWfrx6i0XSMQoWJFNKJAzOlo8K2+McluDl7x/3Cfz
    -----END RSA PRIVATE KEY-----

    -----BEGIN RSA PUBLIC KEY-----
    MIGJAoGBALuWStnqBEQllPplaO8XGub6U9h3ITW6mVpNtXzsCKtgd9BFN5/fLzsH
    /Iv9C53dutw9R4+vKo3PbTDFmTyEN7Q8AcIhwzVzdJM/EC+j5glduO3oOagL5Qr2
    eQgpcuDWVH1UxViv0auLwpabJcDEd2ZNKl+eM2Vd2W6vbjR/2d6BAgMBAAE=
    -----END RSA PUBLIC KEY-----

    */

rsa_import_priv_key
~~~~~~~~~~~~~~~~~~~

Import an existing private key and generate a new public and private keys.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var key = crypto.rsa_import_priv_key(oldprivate_key[, opts]);
    
    /* or */
    
    var key = crypto.rsa_import_priv_key(oldprivate_key[, oldpass][, newpass]);
    
Where:

    * ``oldprivate_key`` is an :green:`Object` or :green:`String`, the pem formatted private key.
    * ``opts`` is an :green:`Object` with the properties ``{decryptPassword: "oldpass", encryptPassword: "newpass"}``.
    * ``oldpass`` is a :green:`String`, the password to decrypt ``oldprivate_key``, if encrypted.
    * ``newpass`` is a :green:`String`, an optional password to encrypt the return private keys.

Return Value:
    An :green:`Object` with the following properties:
    
      * ``public`` - the public key in pkcs8 ``pem`` format.
      * ``private`` - the private key in pkcs8 ``pem`` format, encrypted if
        ``newpass`` is given.

      * ``rsa_public`` - the public key in pkcs1 rsa public key ``pem`` format.
      * ``rsa_private`` - the private key in pkcs1 rsa private key ``pem`` format, encrypted if
        ``newpass`` is given.

rsa_components
~~~~~~~~~~~~~~

Get the component parts of an RSA public or private key.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");
    
    var components = crypto.rsa_components(key);

Return Value:
    An :green:`Object`.

    If ``key`` is a public key, the following properties are set:

        * ``exponent`` - a :green:`String` with the hex encoded value of the exponent.
        * ``modulus`` - a :green:`String` with the hex encoded value of the modulus.

    If ``key`` is a private key, in addition to the above:
    
        * ``privateExponent`` - a :green:`String` with the hex encoded value of the private exponent.
        * ``privateFactorq``  - a :green:`String` with the hex encoded value of the private factor ``q``.
        * ``privateFactorp``  - a :green:`String` with the hex encoded value of the private factor ``p``.

rsa_pub_encrypt
~~~~~~~~~~~~~~~

Encrypt data using an RSA public key.  The public key can be in either 
pem format generated by `rsa_gen_key`_\ ().

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.rsa_pub_encrypt(data, public_key[, paddingMode]);

    /* or with options object: */

    var res = crypto.rsa_pub_encrypt(data, public_key, opts);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      encrypt.

    * ``public_key`` is a :green:`String` or :green:`Buffer` with the content of
      the public key.

    * ``opts`` is an :green:`Object` for fine-grained OAEP control:

        * ``padding`` - a :green:`String`, same values as ``paddingMode``
          below.  Default ``"pkcs"``.
        * ``hash`` - a :green:`String`, the OAEP hash and MGF1 hash
          name (e.g. ``"sha256"``).  Only used when ``padding`` is
          ``"oaep"``.  Default is ``"sha1"`` to match openssl's
          ``-oaep`` behavior.
        * ``label`` - a :green:`String` or :green:`Buffer`, optional
          OAEP label.  Must match the value passed to `rsa_priv_decrypt`_.

    * ``paddingMode`` is an optional :green:`String` that is one of the
      following (as described
      `here <https://www.openssl.org/docs/man1.1.1/man3/RSA_public_encrypt.html>`_):

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
      
      Note: PKCS #1 v1.5 is vulnerable to Bleichenbacher padding-
      oracle attacks.  OpenSSL 4.0's decrypt call now silently
      returns a fake plaintext on bad input instead of an error, so
      a server using 4.0 no longer leaks the padding-validity signal
      these attacks need — but only on the decrypting side, and only
      for the default provider.  Peers still running older OpenSSL
      remain vulnerable.  For new code, prefer OAEP padding.  See
      `the OpenSSL 4.0 docs <https://docs.openssl.org/4.0/man3/RSA_public_encrypt/#warnings>`_
      for details.

      Note also that the length of ``data`` cannot be more than the number of bytes of
      the modulus used to create the key pair minus 11 (or minus 42 in the case of
      ``"oaep"``, or minus 0 in the case of ``raw``).  Exceeding this limit throws
      an error naming the actual input length and the computed maximum; ``data``
      is never silently truncated.  The ``"oaep"`` overhead of
      42 is for the default SHA-1 hash; if a stronger hash is selected via
      ``{padding:"oaep", hash:"<name>"}``, the overhead becomes
      ``2 × hash_output_size + 2`` (e.g. 66 for SHA-256, 98 for SHA-384,
      130 for SHA-512).

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
Decrypt encrypted data using an RSA private key. The private key can be in either 
pem format generated by `rsa_gen_key`_\ ().

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.rsa_priv_decrypt(data, private_key[, paddingMode][, password]);

    /* or with options object: */

    var res = crypto.rsa_priv_decrypt(data, private_key, opts);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      decrypt.

    * ``private_key`` is a :green:`String` or :green:`Buffer` with the contents of the
      private key.

    * ``paddingMode`` - a :green:`String`. See above - the same padding mode used to encrypt
      the data.

    * ``password`` - a :green:`String`, if ``private_key`` is password
      protected, the password used to encrypt the private key.

    * ``opts`` is an :green:`Object` accepting the same fields as
      `rsa_pub_encrypt`_: ``padding``, ``hash``, ``label``, plus an
      optional ``password`` to decrypt ``private_key``.  The
      ``hash`` and ``label`` values must match those used at encrypt
      time.

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

Sign a message with an RSA private key. The private key can be in either 
pem format generated by `rsa_gen_key`_\ ().

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var signature = crypto.rsa_sign(message, private_key[, password]);

    /* or with options object (for PSS, non-default hash, etc.): */

    var signature = crypto.rsa_sign(message, private_key, opts);

Where:

    * ``message`` is a :green:`String` or :green:`Buffer` with the content to
      sign.

    * ``private_key`` is a :green:`String` or :green:`Buffer` with the contents of the
      private key.

    * ``password`` - a :green:`String`, if ``private_key`` is password
      protected, the password used to encrypt the private key.

    * ``opts`` is an :green:`Object` which may contain:

        * ``padding`` - a :green:`String`, ``"pkcs1"`` (default) or ``"pss"``.
        * ``hash`` - a :green:`String`, hash name (e.g. ``"sha256"``,
          ``"sha384"``, ``"sha512"``).  Default is ``"sha256"``.
        * ``mgfHash`` - a :green:`String`, MGF1 hash name used for
          PSS.  Defaults to ``hash``.
        * ``saltLength`` - a :green:`Number`, PSS salt length.
          ``-1`` (default) = digest length, ``-2`` = maximum; a value of
          ``0`` or greater = explicit byte count.
        * ``password`` - a :green:`String`, the private-key password
          if encrypted.

Return Value:
    A :green:`Buffer` with the content of the signature.  Same as
    ``openssl dgst -sha256 -sign private_key.pem -out sig msg.txt``
    (or ``-sigopt rsa_padding_mode:pss`` for PSS signatures).


rsa_verify
~~~~~~~~~~

Verify a signed message with an RSA public key. The public key can be in either 
pem format generated by `rsa_gen_key`_\ ().

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var verified = crypto.rsa_verify(data, public_key, signature[, opts]);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      verify.

    * ``public_key`` is a :green:`String` or :green:`Buffer` with the contents of the
      public key.

    * ``signature`` - a :green:`Buffer` containing the signature
      generated with ``rsa_sign`` above, or with openssl.

    * ``opts`` is an :green:`Object` accepting the same ``padding``,
      ``hash``, ``mgfHash`` and ``saltLength`` fields as `rsa_sign`_.
      These must match the values used at sign time.

Return Value:
    A :green:`Boolean` - ``true`` if verification succeeded.  Otherwise
    ``false``. Same as 
    ``openssl dgst -sha256 -verify public_key.pem -signature sig msg.txt``.

EC Encryption
-------------

Elliptic-curve key generation, ECDSA signing/verification, and ECDH
key agreement on the NIST P-curves (P-256, P-384, P-521).  The
calling convention and return-shape mirror the `RSA Encryption`_
functions above — keys are PEM strings, the same positional and
opts-object forms are accepted, and `rsa_import_priv_key`_-style
password / rekey support is built in.

ec_gen_key
~~~~~~~~~~

Generate an EC key pair on a named curve.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var key = crypto.ec_gen_key([curve][, password]);

    /* or with an options object: */

    var key = crypto.ec_gen_key({curve: "P-256", password: "mypass"});

Where:

    * ``curve`` is an optional :green:`String`, one of ``"P-256"``
      (the default), ``"P-384"`` or ``"P-521"``.  Case-insensitive;
      the SEC1 aliases ``"prime256v1"``, ``"secp384r1"`` and
      ``"secp521r1"`` are also accepted.

    * ``password`` is an optional :green:`String` used to encrypt
      the returned private-key PEMs.  Omit for an unencrypted key.

Return Value:
    An :green:`Object` with:

      * ``public`` - the public key in PKCS#8 SPKI ``pem`` format.
      * ``private`` - the private key in PKCS#8 ``pem`` format,
        encrypted if ``password`` is given.
      * ``ec_private`` - the private key in the legacy SEC1
        ``-----BEGIN EC PRIVATE KEY-----`` ``pem`` format, encrypted
        if ``password`` is given.  This is the analog of RSA's
        ``rsa_private``.

ec_import_pub_key
~~~~~~~~~~~~~~~~~

Import an EC public key from PEM, DER, or raw bytes and return the
canonical SPKI :green:`String` (PEM).

Usage:

.. code-block:: javascript

    var pem = crypto.ec_import_pub_key(public_key[, curve]);

    /* or with an options object: */

    var pem = crypto.ec_import_pub_key({key: raw, curve: "P-256", format: "raw"});

Where:

    * ``public_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER SPKI, or raw uncompressed point ``04||X||Y``).  Input
      format is auto-detected for PEM/DER; raw bytes require an
      explicit ``curve`` argument.

    * ``curve`` is required when ``public_key`` is raw bytes.

Return Value:
    A :green:`String` containing the canonical SPKI PEM.

ec_import_priv_key
~~~~~~~~~~~~~~~~~~

Import an existing EC private key and return both PEM forms (mirrors
`rsa_import_priv_key`_).  Optionally re-encrypts the returned
private keys with a new password.

Usage:

.. code-block:: javascript

    var key = crypto.ec_import_priv_key(oldprivate_key[, oldpass][, newpass]);

Where:

    * ``oldprivate_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER).  Format is auto-detected.
    * ``oldpass`` is the password to decrypt ``oldprivate_key`` (if
      encrypted), or ``null``.
    * ``newpass`` is the optional password to encrypt the returned
      private-key PEMs.

    Note that unlike `rsa_import_priv_key`_, this function does not
    accept an options-:green:`Object` form; passwords must be passed
    positionally.

Return Value:
    The same shape as `ec_gen_key`_:

      * ``public`` - the public key in PKCS#8 SPKI ``pem`` format.
      * ``private`` - the private key in PKCS#8 ``pem`` format,
        encrypted if ``newpass`` is given.
      * ``ec_private`` - the private key in SEC1 ``pem`` format,
        encrypted if ``newpass`` is given.

ec_components
~~~~~~~~~~~~~

Get the component parts of an EC public or private key (mirrors
`rsa_components`_).

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var components = crypto.ec_components(key);

Return Value:
    An :green:`Object`.

    If ``key`` is a public key:

        * ``curve`` - a :green:`String`, the curve name (``"P-256"``
          / ``"P-384"`` / ``"P-521"``).
        * ``x`` - a :green:`String` with the hex-encoded X-coordinate.
        * ``y`` - a :green:`String` with the hex-encoded Y-coordinate.

    If ``key`` is a private key, in addition to the above:

        * ``scalar`` - a :green:`String` with the hex-encoded private
          scalar.

ecdsa_sign
~~~~~~~~~~

Sign a message with an ECDSA private key.

Usage:

.. code-block:: javascript

    var signature = crypto.ecdsa_sign(message, private_key[, password]);

    /* or with options object (for non-default hash, signature format, etc.): */

    var signature = crypto.ecdsa_sign(message, private_key, opts);

Where:

    * ``message`` is a :green:`String` or :green:`Buffer`, the
      content to sign.
    * ``private_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER).
    * ``password`` is a :green:`String`, the password to decrypt
      ``private_key`` (if encrypted).
    * ``opts`` is an :green:`Object` which may contain:

        * ``hash`` - a :green:`String`, hash algorithm name.  Default
          is ``"sha256"``.
        * ``format`` - a :green:`String`, ``"der"`` (default) or
          ``"p1363"``.  ``"der"`` is the standard ASN.1 SEQUENCE
          used by OpenSSL.  ``"p1363"`` is the fixed-length
          ``r || s`` form used by Web Crypto and JWS.
        * ``password`` - same as the positional ``password`` above.

Return Value:
    A :green:`Buffer` containing the signature.  For ``"p1363"`` the
    length is ``2 * scalar_size`` (64 / 96 / 132 bytes); ``"der"``
    sigs vary in length signature to signature.

ecdsa_verify
~~~~~~~~~~~~

Verify an ECDSA signature.

Usage:

.. code-block:: javascript

    var ok = crypto.ecdsa_verify(data, public_key, signature[, opts]);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer`, the message.
    * ``public_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER).
    * ``signature`` is a :green:`Buffer`, the signature to verify.
    * ``opts`` is an :green:`Object` accepting the same ``hash`` and
      ``format`` fields as `ecdsa_sign`_; both must match the values
      used at sign time.

Return Value:
    A :green:`Boolean` - ``true`` if the signature is valid,
    ``false`` otherwise.

ecdh
~~~~

Compute an ECDH shared secret.

Usage:

.. code-block:: javascript

    var shared = crypto.ecdh(private_key, public_key[, password]);

    /* or with an options object: */

    var shared = crypto.ecdh({private: priv, public: pub, password: "pw"});

Where:

    * ``private_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER) of an EC private key.
    * ``public_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER) of an EC public key on the same curve.
    * ``password`` is a :green:`String`, the password to decrypt
      ``private_key`` (if encrypted).

Return Value:
    A :green:`Buffer` containing the raw shared X-coordinate (32
    bytes for P-256, 48 for P-384, 66 for P-521).  The shared secret
    should normally be passed through a KDF (e.g. `hkdf`_) before
    use as a symmetric key.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var alice = crypto.ec_gen_key("P-256");
    var bob   = crypto.ec_gen_key("P-256");

    var sharedA = crypto.ecdh(alice.private, bob.public);
    var sharedB = crypto.ecdh(bob.private,   alice.public);
    /* sharedA equals sharedB */

    /* Derive an AES-256 key from the shared secret. */
    var aesKey = crypto.hkdf({
        ikm:    sharedA,
        info:   "aes-256-gcm",
        length: 32,
        hash:   "sha256"
    });


X25519 and Ed25519
------------------

Modern Curve25519-family keys: X25519 for ECDH-style key agreement,
Ed25519 for signing.  Faster, simpler and more side-channel resistant
than the NIST P-curves.  Raw keys are always 32 bytes; Ed25519
signatures are always 64 bytes.

Calling convention mirrors `EC Encryption`_ — positional or opts
form, keys as PEM strings, password / rekey support.

x25519_gen_key
~~~~~~~~~~~~~~

Generate an X25519 key pair.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var key = crypto.x25519_gen_key([password]);

    /* or */

    var key = crypto.x25519_gen_key({password: "mypass"});

Where:

    * ``password`` is an optional :green:`String` used to encrypt
      the returned private-key PEM.

Return Value:
    An :green:`Object` with:

      * ``public`` - the public key in PKCS#8 SPKI ``pem`` format
        (44-byte body).
      * ``private`` - the private key in PKCS#8 ``pem`` format
        (48-byte body), encrypted if ``password`` is given.

x25519_import_pub_key
~~~~~~~~~~~~~~~~~~~~~

Import an X25519 public key from PEM, DER, or raw 32-byte point and
return the canonical SPKI PEM :green:`String`.

Usage:

.. code-block:: javascript

    var pem = crypto.x25519_import_pub_key(public_key);

    /* or with options object (required for raw bytes): */

    var pem = crypto.x25519_import_pub_key({key: raw32, format: "raw"});

Where:

    * ``public_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER) of an X25519 public key.
    * The opts form additionally accepts ``format: "raw"`` for a
      32-byte u-coordinate buffer.

Return Value:
    A :green:`String` containing the canonical SPKI PEM.

x25519_import_priv_key
~~~~~~~~~~~~~~~~~~~~~~

Import an X25519 private key and return both PEM forms (mirrors
`ec_import_priv_key`_ / `rsa_import_priv_key`_).

Usage:

.. code-block:: javascript

    var key = crypto.x25519_import_priv_key(oldprivate_key[, oldpass[, newpass]]);

    /* or for raw 32-byte scalar: */

    var key = crypto.x25519_import_priv_key({key: raw32, format: "raw"});

Where:

    * ``oldprivate_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER).
    * ``oldpass`` is the password to decrypt the input (if encrypted),
      or ``null``.
    * ``newpass`` is the optional password to encrypt the returned
      private-key PEM.

    Note that passwords must be passed positionally; there is no
    ``{decryptPassword, encryptPassword}`` options form.

Return Value:
    An :green:`Object` with ``public`` and ``private`` PEM strings
    (same shape as `x25519_gen_key`_).

x25519_components
~~~~~~~~~~~~~~~~~

Get the component parts of an X25519 public or private key.

Usage:

.. code-block:: javascript

    var components = crypto.x25519_components(key);

Return Value:
    An :green:`Object`.

    If ``key`` is a public key:

        * ``curve`` - a :green:`String`, ``"X25519"``.
        * ``public`` - a :green:`String`, hex-encoded 32-byte u-coordinate.

    If ``key`` is a private key, in addition:

        * ``private`` - a :green:`String`, hex-encoded 32-byte scalar.

x25519_derive
~~~~~~~~~~~~~

Compute an X25519 shared secret.

Usage:

.. code-block:: javascript

    var shared = crypto.x25519_derive(private_key, public_key[, password]);

    /* or with options object: */

    var shared = crypto.x25519_derive({private: priv, public: pub, password: "pw"});

Where:

    * ``private_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER) of an X25519 private key.
    * ``public_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER) of an X25519 public key.
    * ``password`` is a :green:`String`, the password to decrypt
      ``private_key`` (if encrypted).

Return Value:
    A :green:`Buffer` containing the 32-byte shared secret.  Should
    normally be passed through a KDF (e.g. `hkdf`_) before use as a
    symmetric key.

ed25519_gen_key
~~~~~~~~~~~~~~~

Generate an Ed25519 key pair.  Same options and return shape as
`x25519_gen_key`_.

ed25519_import_pub_key
~~~~~~~~~~~~~~~~~~~~~~

Import an Ed25519 public key.  Same call shape as `x25519_import_pub_key`_.

ed25519_import_priv_key
~~~~~~~~~~~~~~~~~~~~~~~

Import an Ed25519 private key.  Same call shape as `x25519_import_priv_key`_.

ed25519_components
~~~~~~~~~~~~~~~~~~

Get the component parts of an Ed25519 key.  Same shape as
`x25519_components`_, but ``curve`` is ``"Ed25519"`` and the ``public``
hex is the compressed point encoding.

ed25519_sign
~~~~~~~~~~~~

Sign a message with an Ed25519 private key.  Ed25519 is a "pure"
signature scheme — no hash parameter; the full message is signed
directly.

Usage:

.. code-block:: javascript

    var signature = crypto.ed25519_sign(message, private_key[, password]);

Where:

    * ``message`` is a :green:`String` or :green:`Buffer`.
    * ``private_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER).
    * ``password`` is the password to decrypt ``private_key`` (if
      encrypted).

Return Value:
    A :green:`Buffer` containing the 64-byte signature.

ed25519_verify
~~~~~~~~~~~~~~

Verify an Ed25519 signature.

Usage:

.. code-block:: javascript

    var ok = crypto.ed25519_verify(data, public_key, signature);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer`.
    * ``public_key`` is a :green:`String` (PEM) or :green:`Buffer` (DER).
    * ``signature`` is a :green:`Buffer`, the 64-byte signature.

Return Value:
    A :green:`Boolean` - ``true`` if valid, ``false`` otherwise.


X448 and Ed448
--------------

Curve448 family — higher security strength (~224 bits) than the 25519
family (~128 bits), at the cost of larger keys and signatures.  Call
shapes are identical to `X25519 and Ed25519`_ — substitute ``x448``
for ``x25519`` and ``ed448`` for ``ed25519`` in any function name and
the semantics carry over.  The functions provided are:

  * ``x448_gen_key([password])`` — same shape as `x25519_gen_key`_.
  * ``x448_import_pub_key(pub)`` — same shape as `x25519_import_pub_key`_.
  * ``x448_import_priv_key(priv[, oldpass[, newpass]])`` — same shape
    as `x25519_import_priv_key`_.
  * ``x448_components(key)`` — returns ``{curve:"X448", public, private?}``;
    raw scalar/u-coordinate is 56 bytes (112 hex chars).
  * ``x448_derive(private_key, public_key[, password])`` — returns
    a 56-byte shared secret.
  * ``ed448_gen_key([password])``, ``ed448_import_pub_key(pub)``,
    ``ed448_import_priv_key(priv[, oldpass[, newpass]])``,
    ``ed448_components(key)``,
    ``ed448_sign(message, private_key[, password])``,
    ``ed448_verify(data, public_key, signature)``.
  * Ed448 raw keys are 57 bytes (compressed point / seed); Ed448
    signatures are 114 bytes.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    /* X448 key agreement */
    var alice = crypto.x448_gen_key();
    var bob   = crypto.x448_gen_key();
    var shared = crypto.x448_derive(alice.private, bob.public);   /* 56 bytes */

    /* Ed448 sign/verify */
    var keys = crypto.ed448_gen_key();
    var sig = crypto.ed448_sign("message", keys.private);          /* 114 bytes */
    var ok  = crypto.ed448_verify("message", keys.public, sig);


Post-Quantum (ML-KEM, ML-DSA)
-----------------------------

NIST-standardized post-quantum primitives (FIPS 203 and 204).  Use
these alongside classical algorithms in hybrid schemes for forward
security against future quantum attacks.

ML-KEM (Key Encapsulation Mechanism)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

NIST FIPS 203, formerly CRYSTALS-Kyber.  A key encapsulation
mechanism, not a key agreement — the sender derives a shared secret
*and* a ciphertext from the recipient's public key in one call; the
recipient runs the ciphertext through their private key to recover
the same shared secret.

Three variants, listed with their security strength (roughly the
equivalent of an AES key size against quantum attack):

  * ``"ml-kem-512"``  — ~AES-128 equivalent.  Public 800 B, ciphertext 768 B.
  * ``"ml-kem-768"``  — ~AES-192 equivalent.  Public 1184 B, ciphertext 1088 B.
  * ``"ml-kem-1024"`` — ~AES-256 equivalent.  Public 1568 B, ciphertext 1568 B.

The shared secret is always 32 bytes.

mlkem_gen_key
^^^^^^^^^^^^^

Generate an ML-KEM key pair.

Usage:

.. code-block:: javascript

    var keys = crypto.mlkem_gen_key(variant[, password]);

Where:

    * ``variant`` is a :green:`String`, one of ``"ml-kem-512"``,
      ``"ml-kem-768"``, ``"ml-kem-1024"`` (case-insensitive).
    * ``password`` is an optional :green:`String` used to encrypt
      the returned private-key PEM.

Return Value:
    An :green:`Object` with ``public`` and ``private`` PEM strings
    (same shape as `rsa_gen_key`_).

mlkem_encapsulate
^^^^^^^^^^^^^^^^^

Generate a ciphertext + shared secret from a recipient's public key.

Usage:

.. code-block:: javascript

    var out = crypto.mlkem_encapsulate(public_key);
    /* out.ciphertext — to send to the recipient
     * out.sharedSecret — to use locally as the shared key  */

Where:

    * ``public_key`` is a :green:`String` (PEM) or :green:`Buffer`
      (DER) of an ML-KEM public key.

Return Value:
    An :green:`Object` with two :green:`Buffers`:

      * ``ciphertext`` — the encapsulated bytes to transmit to the
        recipient (variant-specific size).
      * ``sharedSecret`` — the 32-byte shared secret to use locally.

mlkem_decapsulate
^^^^^^^^^^^^^^^^^

Recover the shared secret from a ciphertext using the recipient's
private key.

Usage:

.. code-block:: javascript

    var shared = crypto.mlkem_decapsulate(ciphertext, private_key[, password]);

Where:

    * ``ciphertext`` is a :green:`Buffer`, as produced by
      `mlkem_encapsulate`_.
    * ``private_key`` is a :green:`String` (PEM) or :green:`Buffer` (DER).
    * ``password`` is the password to decrypt ``private_key`` (if
      encrypted).

Return Value:
    A 32-byte :green:`Buffer` containing the shared secret.

    Note: ML-KEM is IND-CCA2.  Decapsulating with a *wrong* private
    key does **not** throw — it deterministically returns a
    pseudo-random secret derived from the key and ciphertext.  This
    is "implicit rejection" by design; if the secret then fails to
    decrypt downstream traffic, the attacker can't tell whether the
    failure was due to wrong key vs. wrong padding.

mlkem_import_pub_key / mlkem_import_priv_key / mlkem_components
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Same call shapes as the corresponding `x25519_import_pub_key`_ /
`x25519_import_priv_key`_ / `x25519_components`_.  ``_components``
returns ``{variant, public, private?}`` with raw bytes as hex
strings; the ``variant`` field is the OpenSSL form (``"ML-KEM-768"``).

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    /* Alice (sender side) */
    var bobPub = /* received Bob's public key */;
    var out = crypto.mlkem_encapsulate(bobPub);
    sendToBob(out.ciphertext);
    var aliceShared = out.sharedSecret;

    /* Bob (recipient side) */
    var bobKey = /* Bob's loaded private key */;
    var bobShared = crypto.mlkem_decapsulate(receivedCt, bobKey.private);
    /* aliceShared == bobShared */


ML-DSA (Module-Lattice Digital Signature Algorithm)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

NIST FIPS 204, formerly CRYSTALS-Dilithium.  Post-quantum digital
signatures.  Like Ed25519, ML-DSA is a "pure" signature scheme — no
hash parameter; the full message is signed directly.

Three variants:

  * ``"ml-dsa-44"`` — ~AES-128 equivalent.  Public 1312 B, signature 2420 B.
  * ``"ml-dsa-65"`` — ~AES-192 equivalent.  Public 1952 B, signature 3309 B.
  * ``"ml-dsa-87"`` — ~AES-256 equivalent.  Public 2592 B, signature 4627 B.

Note that ML-DSA signing is non-deterministic — same key + same
message produces different signatures on different invocations (the
algorithm samples fresh randomness internally).

mldsa_gen_key
^^^^^^^^^^^^^

Usage:

.. code-block:: javascript

    var keys = crypto.mldsa_gen_key(variant[, password]);

Same shape as `mlkem_gen_key`_ — variant string + optional password,
returns ``{public, private}`` PEMs.

mldsa_sign
^^^^^^^^^^

Usage:

.. code-block:: javascript

    var sig = crypto.mldsa_sign(message, private_key[, password]);

Same call shape as `ed25519_sign`_.

mldsa_verify
^^^^^^^^^^^^

Usage:

.. code-block:: javascript

    var ok = crypto.mldsa_verify(data, public_key, signature);

Same call shape as `ed25519_verify`_.

mldsa_import_pub_key / mldsa_import_priv_key / mldsa_components
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Same shapes as the corresponding ML-KEM functions above.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var keys = crypto.mldsa_gen_key("ml-dsa-65");
    var sig = crypto.mldsa_sign("the message", keys.private);
    var ok  = crypto.mldsa_verify("the message", keys.public, sig);


gen_csr
~~~~~~~

Generate a certificate signing request.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");
    
    var csr = crypto.gen_csr(private_key, opts[, password]);

Where:

    * ``private_key`` is a :green:`String`, a pem formatted private key.
    * ``opts`` is an :green:`Object`, with the following optional property :green:`Strings`:

        * ``name`` - The "Common Name", usually the relevant domain name.
        * ``country`` - A two letter country code (i.e. ``US`` or ``DE``).
        * ``state`` - State or Province name.
        * ``city`` - The locality or city of your organization.
        * ``organization`` - The full legal name of your organization.
        * ``organizationUnit`` - The department of your organization.
        * ``email`` - Contact email.
        * ``subjectAltName`` - text to be placed in the ``Attributes`` -> ``Requested Extensions`` -> ``X509v3 Subject Alternative Name``
          section of the certificate request.  Also accepts an :green:`Array` of :green:`Strings` for multiple values.

        * ``subjectAltNameType`` - The type used for values in ``subjectAltName``.  If, e.g., ``dns`` is set and ``subjectAltName`` is set to
          ``["example.com", "www.example.com"]``, the certificate signing request will include the 
          ``X509v3 Subject Alternative Name`` value of ``DNS:example.com, DNS:www.example.com``.  Possible values are ``dns`` (the
          default if not specified), ``ip``, ``email``, ``uri``, ``x400``, ``dirname``, ``rid`` or ``othername`` (case insensitive).
          See openssl documentation for meaning and usage of each.  For requesting an SSL/TLS certificate for a webserver, ``dns``
          should be used, particularly where the requested certificate will cover more than one domain name.

    * ``password`` - if ``private_key`` is password protected, the password to decrypt the private key.

Return Value:
    A :green:`Object` with the following properties:

        * ``pem`` - A :green:`String` - the generated certificate signing request in pem format.
        * ``der`` - A :green:`Buffer` - the generated certificate signing request in der binary format.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");
    
    /* generate a server key */
    var key = crypto.rsa_gen_key(4096 /* ,"password" */);

    /* save it for use with webserver */
    rampart.utils.fprintf("./server.key", '%s', key.private);

    /* generate a signing request for current domains */
    var csr = crypto.gen_csr(
        key.private,
        {
            name: "example.com",
            subjectAltName: ["example.com", "www.example.com"]
        }
        /* , "password" */
    );
    /* csr == {pem: pem_formatted_csr, der: der_formatted_csr} */

gen_cert
~~~~~~~~

Generate a self-signed X509v3 certificate and RSA private key.  This is
the equivalent of running
``openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout key.pem -out cert.pem``
from the command line.  The generated certificate is suitable for use with
web servers such as nginx or the rampart-server module.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.gen_cert(options);

    /* or */

    var res = crypto.gen_cert(subject_string[, options]);

Where:

* ``options`` is an :green:`Object` which may contain the following properties:

  Subject properties (all optional :green:`Strings`):

    * ``name`` - The "Common Name" (CN), usually the relevant domain name.
    * ``country`` - A two letter country code (e.g. ``US`` or ``DE``).
    * ``state`` - State or Province name.
    * ``city`` - The locality or city of your organization.
    * ``organization`` - The full legal name of your organization.
    * ``organizationUnit`` - The department of your organization.
    * ``email`` - Contact email address.

  Certificate options:

    * ``bits`` - a :green:`Number`, the RSA key size in bits.  Default is ``2048``.
    * ``days`` - a :green:`Number`, the number of days the certificate is valid.  Default is ``365``.

  Extension properties:

    * ``basicConstraints`` - a :green:`String`, the value for the X509v3 Basic Constraints
      extension.  Default is ``"CA:FALSE"``.
    * ``keyUsage`` - a :green:`String`, the value for the X509v3 Key Usage extension.
      Default is ``"digitalSignature, keyEncipherment"``.
    * ``subjectAltName`` - a :green:`String` or :green:`Array` of :green:`Strings`,
      the Subject Alternative Name entries for the certificate.
    * ``subjectAltNameType`` - a :green:`String`, the type prefix for ``subjectAltName``
      entries.  One of ``"dns"`` (the default), ``"ip"``, ``"email"`` or ``"uri"``
      (case insensitive).  For web server certificates, ``"dns"`` should be used.

* ``subject_string`` is a :green:`String` specifying the certificate subject in the
  format used by ``openssl req -subj``, e.g. ``"/C=US/ST=Delaware/O=My Org/CN=example.com"``.
  Standard OpenSSL short names are accepted (``C``, ``ST``, ``L``, ``O``, ``OU``, ``CN``,
  ``emailAddress``, etc.).  When using this form, certificate and extension options may be
  passed in an optional second argument ``options`` :green:`Object` (the subject properties
  listed above are ignored in that case).

Return Value:
    An :green:`Object` with the following properties:

      * ``key`` - a :green:`String`, the generated RSA private key in PEM format.
      * ``cert`` - a :green:`String`, the generated self-signed certificate in PEM format.

Example using an :green:`Object`:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.gen_cert({
        country: "US",
        state: "Delaware",
        city: "Wilmington",
        organization: "My Company",
        name: "example.com",
        bits: 2048,
        days: 365,
        subjectAltName: ["example.com", "*.example.com"]
    });

    /* write key and cert to files for use with a web server */
    rampart.utils.fprintf("/path/to/server.key", "%s", res.key);
    rampart.utils.fprintf("/path/to/server.crt", "%s", res.cert);

Example using a subject :green:`String`:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.gen_cert(
        "/C=US/ST=Delaware/L=Wilmington/O=My Company/CN=example.com",
        {
            days: 730,
            subjectAltName: ["example.com", "*.example.com"]
        }
    );

    rampart.utils.fprintf("/path/to/server.key", "%s", res.key);
    rampart.utils.fprintf("/path/to/server.crt", "%s", res.cert);

Example for use with the rampart-server module:

.. code-block:: javascript

    var crypto = require("rampart-crypto");
    var server = require("rampart-server");

    var certdir = process.scriptPath + "/certs";
    var keyfile = certdir + "/server.key";
    var certfile = certdir + "/server.crt";

    /* generate certs if they don't exist */
    if(!rampart.utils.stat(keyfile) || !rampart.utils.stat(certfile))
    {
        rampart.utils.mkdir(certdir);
        var res = crypto.gen_cert({
            name: "localhost",
            subjectAltName: ["localhost", "*.localhost"]
        });
        rampart.utils.fprintf(keyfile, "%s", res.key);
        rampart.utils.fprintf(certfile, "%s", res.cert);
    }

    server.start({
        bind: "0.0.0.0:8443",
        secure: true,
        sslKeyFile: keyfile,
        sslCertFile: certfile,
        map: {
            "/": function(req) { return {text: "Hello, HTTPS!"}; }
        }
    });

cert_info
~~~~~~~~~

Parse a PEM formatted X509 certificate and return its details.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var info = crypto.cert_info(pem);

Where:

    * ``pem`` is a :green:`String` or :green:`Buffer` containing the PEM formatted
      certificate.

Return Value:
    An :green:`Object` with properties describing the certificate, including
    ``version``, ``serialNumber``, ``issuer``, ``subject``, ``notBefore``,
    ``notAfter``, ``extensions`` and other fields as available in the certificate.
    Note that ``notBefore`` and ``notAfter`` are JavaScript :green:`Date` objects
    (shown below as strings due to JSON serialization).

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.gen_cert("/CN=example.com/O=My Company");
    var info = crypto.cert_info(res.cert);

    rampart.utils.printf("%3J\n", info);

    /* sample output (abbreviated):
    {
       "version": 2,
       "serialNumber": "...",
       "issuer": "O=My Company, CN=example.com",
       "subject": "O=My Company, CN=example.com",
       "notBefore": "2026-03-10T00:00:00.000Z",
       "notAfter": "2027-03-10T00:00:00.000Z",
       "extensions": {
          "X509v3 Basic Constraints": "CA:FALSE",
          "X509v3 Key Usage": "Digital Signature, Key Encipherment"
       }
    }
    */

Password
--------

passwd
~~~~~~

Compute a salt/password entry using a standard algorithm. 

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var pwentry = crypto.passwd(pass[, salt[, algo]]);

Where

    * ``pass`` is a :green:`String`, the password to be hashed.

    * ``salt`` is a :green:`String`, the salt to use to create the hash, or
      ``null`` to generate a salt.

    * ``algo`` is a :green:`String`, one of ``sha512`` (default),
      ``sha256``, ``md5``, ``apr1``, ``aixmd5``, or ``crypt``.

Return Value:
    An :green:`Object` with keys ``line``, ``salt``, ``hash`` and ``mode``.

See example below in `passwdCheck`_\ .

passwdComponents
~~~~~~~~~~~~~~~~

Given a hash entry from a file such as ``/etc/passwd`` or ``/etc/shadow``,
return the components.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var components = crypto.passwdComponents(line);

Return Value:
    Same as `passwd`_ above.

passwdCheck
~~~~~~~~~~~

Given a hash entry and a password, check if they match.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var passok = crypto.passwdCheck(line, passwd);

Where:

    * ``line`` is a :green:`String`, the ``line`` property from the return of `passwd`_ or a
      hash entry from a file such as ``/etc/passwd`` or ``/etc/shadow``.

    * ``passwd`` is a :green:`String`, the password to check.

Return Value:
    a :green:`Boolean`, ``true`` if the password matches the hash.  Otherwise ``false``.


Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");
    var pw = crypto.passwd("mypasswd", null, "md5");
    rampart.utils.printf("%3J\n", pw);

    /* Sample Output:
        {
           "line": "$1$YR/UxoNq$cSEfWsMQH5OC7xY/AYM8S1",
           "salt": "YR/UxoNq",
           "hash": "cSEfWsMQH5OC7xY/AYM8S1",
           "mode": "md5"
        }
    */

    var line="$1$epCdp7c5$QROtiBBauvN/HPi5e22ty1";
    var components = crypto.passwdComponents(line);
    rampart.utils.printf("%3J\n", components);

    /* Expected Output:
        {
           "line": "$1$epCdp7c5$QROtiBBauvN/HPi5e22ty1",
           "salt": "epCdp7c5",
           "hash": "QROtiBBauvN/HPi5e22ty1",
           "mode": "md5"
        }
    */


    if(crypto.passwdCheck(line, "mypasswd"))
        console.log("password ok");
    else
        console.log("password fail");

    /* Expected Output:
        password ok
    */

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

    var res = hash(data[, hash_func][, return_option]);

Where:

* ``data`` is a :green:`String` or :green:`Buffer`, the data to be
  hashed.

* ``hash_func`` is an optional :green:`String`, one of the following:

  ``sha1``, ``sha224``, ``sha256``, ``sha384``, ``sha512``, ``md4``, ``md5``, ``sha3-224``,
  ``sha3-256``, ``sha3-384``, ``sha3-512``, ``blake2b512``, ``blake2s256``, ``mdc2``,
  ``rmd160``, ``sha512-224``, ``sha512-256``, ``shake128``, ``shake256``,
  ``sm3``. Default is ``sha256``.

* ``return_option`` controls the type of the returned value. It may be
  either a :green:`Boolean` or an :green:`Object`:

    * If omitted or :green:`false`, returns a hex-encoded :green:`String`
      (the default).
    * If :green:`true`, returns a raw :green:`Uint8Array`. This is the
      historical "raw" form.
    * If an options :green:`Object` of the form
      ``{returnType: "hex" | "uint8array" | "buffer"}``:

        * ``"hex"`` — hex-encoded :green:`String` (same as default).
        * ``"uint8array"`` — raw :green:`Uint8Array` (same as ``true``).
        * ``"buffer"`` — a node-style :green:`Buffer` (a ``Uint8Array``
          subclass with node's prototype methods, e.g.
          ``buf.toString('hex')``, ``buf.toString('base64')``,
          ``buf.indexOf(...)``).  Used by ``rampart-nodeshim`` for
          node-compatible code.

Return Value:
    A :green:`String`, :green:`Uint8Array`, or :green:`Buffer`,
    depending on ``return_option`` above. Default is hex
    :green:`String`.


Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.hash("hello world", "sha256");
    /* res == 'b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9' */

    /* As a node-style Buffer (so .toString('base64') etc. work): */
    var buf = crypto.hash("hello world", "sha256", {returnType: "buffer"});
    var b64 = buf.toString('base64');

alias functions
~~~~~~~~~~~~~~~

The hash function has an alias for each of the possible ``hash_func``
value above.  Thus, using ``crypto.hash("hello world", "sha256")`` is equivalent to
``crypto.sha256("hello world")``.  For ``hash_func`` names with a dash
(``-``), an underscore (``_``) is used instead.  Thus
``crypto.hash("hello world", "sha3-256")`` is equivalent to
``crypto.sha3_256("hello world")``.

HashStream
~~~~~~~~~~

Incremental (streaming) hashing.  ``new crypto.HashStream()`` returns a
handle that accepts data in any number of pieces — for hashing content
that is never in memory all at once, such as a large file read in
chunks or a download as it arrives.

Producing the digest with ``final()`` does not end the stream: more
data may be added afterwards and ``final()`` called again, each call
returning the digest of everything added so far.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var h = new crypto.HashStream([hash_func]);

    h.update(data);                        /* any number of times */

    var digest = h.final([return_option]);

Where:

* ``hash_func`` is an optional :green:`String`, any of the algorithms
  accepted by `hash`_ above.  Default is ``sha256``.

* ``data`` is a :green:`String` or :green:`Buffer` (any buffer-data
  type: a plain buffer, an ``ArrayBuffer``, a TypedArray view such as
  ``Uint8Array``, or a ``DataView`` — views contribute only the bytes
  they cover), the next piece of data to add to the hash.

* ``return_option`` is the same as for `hash`_ above: omitted for a
  hex-encoded :green:`String`, ``true`` for a raw :green:`Uint8Array`,
  or ``{returnType: "hex" | "uint8array" | "buffer"}``.

Return Value:
    A HashStream handle :green:`Object` with:

    * ``update(data)`` — add a piece of data.  Returns the handle, so
      calls may be chained.
    * ``final([return_option])`` — the digest of all data added so
      far.  Non-destructive; the handle remains usable.
    * ``type`` — a :green:`String`, the algorithm name.

Note:
    The ``new`` keyword is optional, but constructing with ``new`` is
    the documented form.

    A handle copied to a thread created with ``rampart.thread`` shares
    its hash state with the original: bytes added in either thread are
    part of the digest either sees.  Updates are internally locked, but
    the byte order of interleaved concurrent updates is unspecified —
    have one thread at a time feed a given handle.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");
    rampart.globalize(rampart.utils);

    /* the sha256 of a file too large to hash in one piece */
    var h = new crypto.HashStream("sha256");

    var fh = fopen("/path/to/large.file", "r"), chunk;
    while( (chunk = fh.fread(1048576)) && chunk.byteLength )
        h.update(chunk);
    fh.fclose();

    printf("sha256: %s\n", h.final());

    /* equivalent to crypto.sha256() of the whole content at once,
       e.g.: crypto.sha256(readFile("/path/to/large.file"))       */

hmac
~~~~

The ``hmac()`` function computes a HMAC from the provided data and key.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.hmac(secret, data[, hash_func][, return_option]);

Where:

    * ``secret`` is the HMAC function key.

    * ``data`` is a :green:`String` or :green:`Buffer`, the data to be
      hashed.

    * ``hash_func`` is an optional :green:`String`, one of the following:

      ``sha1``, ``sha224``, ``sha256``, ``sha384``, ``sha512``, ``md4``, ``md5``, ``sha3-224``,
      ``sha3-256``, ``sha3-384``, ``sha3-512``, ``blake2b512``, ``blake2s256``, ``mdc2``,
      ``rmd160``, ``sha512-224``, ``sha512-256``,
      ``sm3``. Default is ``sha256``.

    * ``return_option`` controls the type of the returned value. It
      may be either a :green:`Boolean` or an :green:`Object`:

        * If omitted or :green:`false`, returns a hex-encoded
          :green:`String` (the default).
        * If :green:`true`, returns a raw :green:`Uint8Array`.
        * If an options :green:`Object` of the form
          ``{returnType: "hex" | "uint8array" | "buffer"}``, selects
          the corresponding return shape.  ``"buffer"`` yields a
          node-style :green:`Buffer` (used by ``rampart-nodeshim``).

Return Value:
    A :green:`String`, :green:`Uint8Array`, or :green:`Buffer`,
    depending on ``return_option``.

kmac
~~~~

KMAC-128 / KMAC-256 (NIST SP 800-185) — a KECCAK-based MAC with an
optional customization string for domain separation.  Built on the
SHA-3 sponge, so faster than HMAC-SHA-3 and with arbitrary output
length.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var tag = crypto.kmac(key, data[, variant][, opts]);

Where:

    * ``key`` is a :green:`String` or :green:`Buffer`.  Minimum
      length 4 bytes (OpenSSL constraint).  ≥16 bytes recommended
      for KMAC-128, ≥32 bytes recommended for KMAC-256 to match
      the variant's security strength.
    * ``data`` is a :green:`String` or :green:`Buffer`.
    * ``variant`` is an optional :green:`String`, ``"kmac-128"``
      (default) or ``"kmac-256"`` (case-insensitive; ``"kmac128"`` /
      ``"kmac256"`` also accepted).
    * ``opts`` is an :green:`Object` with:

        * ``length`` - a :green:`Number`, output bytes.  Default
          is 32 for KMAC-128 and 64 for KMAC-256 (the variant's
          natural security strength).
        * ``customization`` - a :green:`String` or :green:`Buffer`,
          domain-separation string (S in NIST notation).
        * ``returnType`` - optional :green:`String`, same as for
          `pbkdf2`_.

Return Value:
    A :green:`Uint8Array` of ``length`` bytes by default, or a hex
    :green:`String` / Node :green:`Buffer` depending on
    ``returnType``.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    /* NIST SP 800-185 KMAC128 sample #1 */
    var key = Buffer.alloc(32);
    for (var i = 0; i < 32; i++) key[i] = 0x40 + i;
    var tag = crypto.kmac(key, Buffer.from([0,1,2,3]), "kmac-128",
                          {returnType: "hex"});
    /* tag == "E5780B0D3EA6F7D3A429C5706AA43A00FADBD7D49628839E3187243F456EE14E" */

cshake128 / cshake256
~~~~~~~~~~~~~~~~~~~~~

Customizable SHAKE (NIST SP 800-185) — like the regular
`hash`_ ``shake128``/``shake256`` XOFs, but with an optional function-name
N and customization S string for domain separation.  With both
strings empty, cSHAKE-X is identical to SHAKE-X.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var bytes = crypto.cshake128(data[, opts]);
    var bytes = crypto.cshake256(data[, opts]);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer`.
    * ``opts`` is an :green:`Object` with:

        * ``length`` - a :green:`Number`, output bytes.  Default
          16 for cSHAKE-128, 32 for cSHAKE-256 (matching the
          underlying SHAKE defaults).
        * ``customization`` - a :green:`String` or :green:`Buffer`,
          the S parameter for domain separation.
        * ``functionName`` - a :green:`String` or :green:`Buffer`,
          the N parameter (typically reserved by other NIST functions
          built on cSHAKE, such as KMAC).  Note that OpenSSL accepts
          only the empty string (the default) or ``"KMAC"`` here; any
          other value raises ``Provider routines::invalid function
          name``.
        * ``returnType`` - optional :green:`String`, same as for
          `pbkdf2`_.

Return Value:
    A :green:`Uint8Array` of ``length`` bytes by default.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    /* NIST SP 800-185 cSHAKE-128 sample #2 */
    var X = []; for (var i = 0; i <= 0xC7; i++) X.push(i);
    var out = crypto.cshake128(Buffer.from(X),
        {length: 32, customization: "Email Signature", returnType: "hex"});
    /* out == "c5221d50e4f822d96a2e8881a961420f294b7b24fe3d2094baed2c6524cc166b" */

Key Derivation
--------------

pbkdf2
~~~~~~

PBKDF2 (RFC 2898) — derive a key from a password by repeatedly
applying an HMAC.  Suitable for password storage and for stretching
human-typed secrets into symmetric keys.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var dk = crypto.pbkdf2(opts);

Where:

    * ``opts`` is an :green:`Object` with:

        * ``pass`` - a :green:`String` or :green:`Buffer`, the
          password.
        * ``salt`` - a :green:`String` or :green:`Buffer`, the salt
          (at least 16 bytes is recommended).
        * ``iter`` - a :green:`Number`, the iteration count.
        * ``length`` - a :green:`Number`, the derived-key length in
          bytes.
        * ``hash`` - a :green:`String`, HMAC hash name (e.g.
          ``"sha256"``, ``"sha512"``).
        * ``returnType`` - optional :green:`String`, one of ``"hex"``
          (hex :green:`String`), ``"uint8array"`` (:green:`Uint8Array`,
          the default) or ``"buffer"`` (node-style :green:`Buffer`,
          used by ``rampart-nodeshim``).

Return Value:
    A :green:`Uint8Array` of ``length`` bytes by default; a hex
    :green:`String` if ``returnType: "hex"``; or a :green:`Buffer` if
    ``returnType: "buffer"``.  Same output as
    ``openssl kdf -keylen <length> -kdfopt digest:<hash> -kdfopt pass:<pass>
    -kdfopt salt:<salt> -kdfopt iter:<iter> PBKDF2``.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    /* RFC 6070 test vector #2 */
    var dk = crypto.pbkdf2({
        pass: "password",
        salt: "salt",
        iter: 2,
        length: 20,
        hash: "sha1",
        returnType: "hex"
    });
    /* dk == "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957" */

hkdf
~~~~

HKDF (RFC 5869) — extract-and-expand key derivation.  Suitable for
deriving symmetric keys from high-entropy input keying material
(e.g. an `ecdh`_ shared secret).  Not designed for password input —
use `pbkdf2`_ for that.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var okm = crypto.hkdf(opts);

Where:

    * ``opts`` is an :green:`Object` with:

        * ``ikm`` - a :green:`String` or :green:`Buffer`, the input
          keying material.
        * ``salt`` - optional :green:`String` or :green:`Buffer`.  May
          be omitted or empty.
        * ``info`` - optional :green:`String` or :green:`Buffer`,
          contextual information binding the derived key to a
          specific purpose.
        * ``length`` - a :green:`Number`, the derived-key length
          (max ``255 * hash_digest_size``).
        * ``hash`` - a :green:`String`, the underlying hash name.
        * ``returnType`` - optional :green:`String`, same as `pbkdf2`_.

Return Value:
    A :green:`Uint8Array` of ``length`` bytes (or a hex :green:`String`
    if ``returnType: "hex"``).

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    /* Derive a 32-byte AES key from an ECDH shared secret. */
    var alice = crypto.ec_gen_key({curve: "P-256"});
    var bob   = crypto.ec_gen_key({curve: "P-256"});
    var sharedSecret = crypto.ecdh({
        private: alice.private,
        public:  bob.public
    });
    var aesKey = crypto.hkdf({
        ikm:    sharedSecret,
        info:   "aes-256-gcm key",
        length: 32,
        hash:   "sha256"
    });

scrypt
~~~~~~

Memory-hard password-based KDF (RFC 7914).  Like `pbkdf2`_, designed
for stretching human-typed secrets — but tuned so that brute-force
hardware (GPUs, ASICs) gains less advantage thanks to the large
working memory each derivation requires.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var dk = crypto.scrypt(opts);

Where:

    * ``opts`` is an :green:`Object` with:

        * ``pass`` - a :green:`String` or :green:`Buffer`, the
          password.
        * ``salt`` - a :green:`String` or :green:`Buffer`, the salt
          (at least 16 bytes recommended).
        * ``N`` - a :green:`Number`, the CPU/memory cost factor.  Must
          be a power of two ≥ 2.  Typical: ``16384`` (interactive),
          ``1048576`` (sensitive storage).
        * ``r`` - a :green:`Number`, the block size.  Usually ``8``.
        * ``p`` - a :green:`Number`, the parallelization factor.
          Usually ``1``.
        * ``length`` - a :green:`Number`, the derived-key length in
          bytes.
        * ``returnType`` - optional :green:`String`, same as `pbkdf2`_.

Return Value:
    A :green:`Uint8Array` of ``length`` bytes by default; a hex
    :green:`String` if ``returnType: "hex"``; or a :green:`Buffer` if
    ``returnType: "buffer"``.

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    /* RFC 7914 Test Vector 3 */
    var dk = crypto.scrypt({
        pass: "pleaseletmein",
        salt: "SodiumChloride",
        N: 16384, r: 8, p: 1,
        length: 64,
        returnType: "hex"
    });
    /* dk == "7023bdcb3afd7348..." (128 hex chars) */

Random
------

rand
~~~~

The ``rand()`` function returns random generated bytes.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var res = crypto.rand(nBytes[, return_option]);

Where:

    * ``nBytes`` is the number of bytes to return.

    * ``return_option`` controls the type of the returned value. When
      omitted, ``rand()`` returns a raw :green:`Uint8Array` (historical
      default — note this is unlike ``hash`` / ``hmac``, which default
      to hex). It may also be:

        * :green:`true` — same as omitted (raw :green:`Uint8Array`).
        * :green:`false` — returns a hex-encoded :green:`String`.
        * An options :green:`Object` of the form
          ``{returnType: "hex" | "uint8array" | "buffer"}`` to select
          the return shape. ``"buffer"`` returns a node-style
          :green:`Buffer` (used by ``rampart-nodeshim``).

Return Value:
    A :green:`Uint8Array`, :green:`Buffer` or :green:`String` depending
    on ``return_option``. Default is :green:`Uint8Array` of ``nBytes``
    bytes.

Examples:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var raw = crypto.rand(16);                                  // Uint8Array
    var hex = crypto.rand(16, {returnType: "hex"});             // string of 32 hex chars
    var buf = crypto.rand(16, {returnType: "buffer"});          // node-style Buffer
    /* buf.toString('base64') works directly */

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
  ``/dev/urandom``.

* ``bytes`` - a :green:`Number` - Number of bytes to retrieve from ``file``. 
  Default is ``32``.

Comparison
----------

timingSafeEqual
~~~~~~~~~~~~~~~

Compare two byte sequences in constant time — does not short-circuit
on the first mismatching byte, so the comparison takes the same
amount of time regardless of where (or whether) inputs differ.  Use
this whenever comparing a secret value (HMAC tag, password hash,
session token) against an untrusted input, to avoid leaking
information through timing.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var equal = crypto.timingSafeEqual(a, b);

Where:

    * ``a``, ``b`` - :green:`Strings` or :green:`Buffers`.  Must have
      the same length, or an error is thrown.

Return Value:
    A :green:`Boolean`, ``true`` if the inputs are byte-identical.

PEM / DER conversion
--------------------

Pure encoding utilities — convert between text-friendly PEM (base64
with banner lines) and binary DER.  Useful when interoperating with
tools or wire formats that expect one form rather than the other.

pemToDer
~~~~~~~~

Decode any single PEM block to its underlying DER :green:`Buffer`.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var der = crypto.pemToDer(pem);

Where:

    * ``pem`` is a :green:`String` or :green:`Buffer` containing a
      single PEM block with ``-----BEGIN <type>-----`` /
      ``-----END <type>-----`` framing.

Return Value:
    A :green:`Buffer` containing the base64-decoded body.

derToPem
~~~~~~~~

Wrap DER bytes in PEM framing with the given type label.

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var pem = crypto.derToPem(der, "PUBLIC KEY");

Where:

    * ``der`` is a :green:`String` or :green:`Buffer` of DER-encoded
      bytes.
    * ``type`` is a :green:`String`, the PEM type label (e.g.
      ``"PUBLIC KEY"``, ``"PRIVATE KEY"``, ``"EC PRIVATE KEY"``,
      ``"CERTIFICATE"``, ``"CERTIFICATE REQUEST"``).  Arbitrary
      DER doesn't self-identify which banner to use, so the caller
      provides the label.

Return Value:
    A :green:`String` containing the PEM-wrapped form.

BigInt
------

The rampart-crypto module includes functions which handle arbitrarily long
integers using openssl's ``BIGNUM`` library.  It is designed to be compatible with the 
`JSBI <https://github.com/GoogleChromeLabs/jsbi>`__ library and includes the
same published functions.  See `JSBI Node Module <https://www.npmjs.com/package/jsbi>`_
for more information.

Usage as documented below is as such:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var JSBI = crypto.JSBI;

JSBI.BigInt
~~~~~~~~~~~

Create a BigInt from a :green:`String` or :green:`Number`.

Possible Strings:

* ``1234``
* ``-1234``
* ``0x123e``
* ``-0x123e``
* ``-0b11110011``
* ``-0b11110011``


Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var JSBI = crypto.JSBI;

    var mybignum = JSBI.BigInt("123456789012345678901234567890");

JSBI Compatible functions
~~~~~~~~~~~~~~~~~~~~~~~~~

JSBI functions aspire to operate in a manner that mirrors the 
`JSBI <https://www.npmjs.com/package/jsbi>`__ library.  Please
see that library for details.  Available commands include:

``JSBI.BigInt(num).toString()``, ``JSBI.toNumber()``, ``JSBI.asIntN()``, ``JSBI.asUintN()``,
``a instanceof JSBI``, ``JSBI.add()``, ``JSBI.subtract()``, ``JSBI.multiply()``,
``JSBI.divide()``, ``JSBI.remainder()``, ``JSBI.exponentiate()``, ``JSBI.unaryMinus()``,
``JSBI.bitwiseNot()``, ``JSBI.leftShift()``, ``JSBI.signedRightShift()``,
``JSBI.bitwiseAnd()``, ``JSBI.bitwiseOr()``, ``JSBI.bitwiseXor()``, ``JSBI.equal()``,
``JSBI.notEqual()``, ``JSBI.lessThan()``, ``JSBI.lessThanOrEqual()``,
``JSBI.greaterThan()``, ``JSBI.greaterThanOrEqual()``, ``JSBI.EQ()``, ``JSBI.NE()``,
``JSBI.LT()``, ``JSBI.LE()``, ``JSBI.GT()``, ``JSBI.GE()``, ``JSBI.ADD()``.


Note that in rampart, ``JSBI.BigInt().toString()`` only accepts ``2``, ``10`` and ``16``
as possible arguments, with ``10`` being the default.
 
toSignedString
~~~~~~~~~~~~~~

``JSBI.BigInt().toSignedString()`` will convert a BigInt into a 
string representing the equivalent signed number.  This differs from ``JSBI.BigInt().toString()``
only when used for a signed binary integer (using ``JSBI.BigInt(num).toSignedString(2)``).

Example:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var JSBI = crypto.JSBI;

    var mybignum = JSBI.BigInt("-256");

    console.log( mybignum.toString(2) );

    console.log( mybignum.toSignedString(2) );

    /* expected output:

    -100000000
    1111111100000000

    */