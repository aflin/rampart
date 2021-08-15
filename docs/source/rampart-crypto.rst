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

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      encrypt.

    * ``public_key`` is a :green:`String` or :green:`Buffer` with the content of
      the public key.

    * ``paddingMode`` is an optiona :green:`String` that is one of the
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
      
      Note that Openssl Library includes this warning:
      
      "Decryption failures in the RSA_PKCS1_PADDING mode leak information
      which can potentially be used to mount a Bleichenbacher padding oracle
      attack. This is an inherent weakness in the PKCS #1 v1.5 padding
      design. Prefer RSA_PKCS1_OAEP_PADDING."  - see 
      `this document <https://www.openssl.org/docs/man1.1.1/man3/RSA_public_encrypt.html>`_.

      Note also that the length of ``data`` cannot be more than the number of bytes of
      the modulus used to create the key pair minus 11 (or minus 42 in the case of
      ``"oaep"``, or minus 0 in the case of ``raw``).

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

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      decrypt.

    * ``private_key`` is a :green:`String` or :green:`Buffer` with the contents of the 
      private key.

    * ``paddingMode`` - a :green:`String`. See above - the same padding mode used to encrypt
      the data.

    * ``password`` - a :green:`String`, if ``private_key`` is password
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

Sign a message with an RSA private key. The private key can be in either 
pem format generated by `rsa_gen_key`_\ ().

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var signature = crypto.rsa_sign(message, private_key[, password]);

Where:

    * ``message`` is a :green:`String` or :green:`Buffer` with the content to
      sign.

    * ``private_key`` is a :green:`String` or :green:`Buffer` with the contents of the 
      private key.

    * ``password`` - a :green:`String`, if ``private_key`` is password
      protected, the password used to encrypt the private key.

Return Value:
    A :green:`Buffer` with the content of the signature.  Same as 
    ``openssl dgst -sha256 -sign private_key.pem -out sig msg.txt``


rsa_verify
~~~~~~~~~~

Verify a signed message with an RSA public key. The public key can be in either 
pem format generated by `rsa_gen_key`_\ ().

Usage:

.. code-block:: javascript

    var crypto = require("rampart-crypto");

    var verified = crypto.rsa_verify(data, public_key, signature);

Where:

    * ``data`` is a :green:`String` or :green:`Buffer` with the content to
      verify.

    * ``public_key`` is a :green:`String` or :green:`Buffer` with the contents of the 
      public key.

    * ``signature`` - a :green:`Buffer` containing the signature
      generated with ``rsa_sign`` above, or with openssl.

Return Value:
    A :green:`Boolean` - ``true`` if verification succeeded.  Otherwise
    ``false``. Same as 
    ``openssl dgst -sha256 -verify public_key.pem -signature sig msg.txt``.

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
          should be used, particularily where the requested certificate will cover more than one domain name.

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

Return Value:
    A :green:`String` or :green:`Buffer`, the hash of the data.


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

Return Value:
    A :green:`String` or :green:`Buffer`, the hmac hash of the data.

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
  ``/dev/urandom``.

* ``bytes`` - a :green:`Number` - Number of bytes to retrieve from ``file``. 
  Default is ``32``.

BigInt
------

The rampart-crypto module includes functions which handle arbitrarly long
integers using openssl's ``BIGNUM`` library.  It is designed to be compatible with the 
`JSBI <https://github.com/GoogleChromeLabs/jsbi>`_ library and includes the
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
`JSBI <https://www.npmjs.com/package/jsbi>`_ library.  Please
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