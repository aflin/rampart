/* make printf et. al. global */
rampart.globalize(rampart.utils);

var crypto = require("rampart-crypto");

testmodes();

function testFeature(name,test)
{
    var error=false;
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    rampart.utils.printf("testing %-40s - ", name);
    if(test)
        rampart.utils.printf("passed\n")
    else
        rampart.utils.printf(">>>>> FAILED <<<<<\n");
    if(error) console.log(error);
}

testFeature( "sha256 hash of 'hello world'", function(){
  var res=crypto.sha256("hello world");
  return res=='b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9';
});


testFeature("hexify of crypto.rand(32):",function(){
  var res=hexify(crypto.rand(32));
  return res.length==64;
});


testFeature("hexify of crypto.randnum():",function(){
  var res=crypto.randnum();
  return ( res>0 && res<1);
});


testFeature("en/decrypt with key,iv and aes-256-cbc",function(){
  var encBuffer = crypto.encrypt({
    key: "01234567890123456789012345678901",
    iv: "0123456789012345",
    cipher: "aes-256-cbc",
    data: "encrypt and decrypt of a string with key,iv and aes-256-cbc"
  });

  var decBuffer = crypto.decrypt({
    key: "01234567890123456789012345678901",
    iv: "0123456789012345",
    cipher: "aes-256-cbc",
    data: encBuffer,
  });
  return "encrypt and decrypt of a string with key,iv and aes-256-cbc" == bufferToString(decBuffer); 
});

testFeature("en/decrypt with password, no options", function(){
  var encBuffer = crypto.encrypt("mypassword","encrypt and decrypt of a string with password and aes-256-cbc");
  var decBuffer = crypto.decrypt("mypassword",encBuffer);

  return "encrypt and decrypt of a string with password and aes-256-cbc" == bufferToString(decBuffer);
});

testFeature("en/decrypt with password, with options", function(){

  var file=readFile("crypto-test.js");

  var hash=crypto.sha512(file);

  encBuffer = crypto.encrypt({
    pass: "whodathunk",
    iter: 50000,
    cipher: "aes-256-cbc",
    data: file
  });

  decBuffer = crypto.decrypt({
    pass: "whodathunk",
    iter: 50000,
    cipher: "aes-256-cbc",
    data: encBuffer,
  });

  var comphash= crypto.sha512(decBuffer);
  return comphash==hash;
});


testFeature("hmac sha256", function(){
  var ret=crypto.hmac("mysecret","mydata");
  return ret="f49cf057cd4de7c1a4cb0b051570372892674487333ac5ab3ea603f29aec9ffe";
});

testFeature("hmac sha512", function(){
  var ret=crypto.hmac("mysecret","mydata");
  return ret="7f416275882395b49071c91caebc4d300b7aed08cb891680371154b61d867428271d3d00425ea7728b91344e442846db66b15b9043160c9a95d02aac6f514dd7";
});

/*
printf("\ncrypt of this file with aes-256-cbc (sha512 hashes before and after):\n%s\n%s\n",hash,comphash);
fprintf("crypto-copy.js.enc","%B",encBuffer);
printf("using openssl 1.1, you can decode the written file with this:\n%s\n",
        '    openssl aes-256-cbc -d -in crypto-copy.js.enc -out crypto-copy.js -p -pbkdf2 -iter 50000 -k "whodathunk"');
*/

function testmodes()
{
  printf("working modes:\n");

  modes={
  "base64": "Base 64",
  "bf-cbc": "Blowfish in CBC mode",
  "bf-cfb": "Blowfish in CFB mode",
  "bf-ecb": "Blowfish in ECB mode",
  "bf-ofb": "Blowfish in OFB mode",
  "cast-cbc": "CAST in CBC mode",
  "cast5-cbc": "CAST5 in CBC mode",
  "cast5-cfb": "CAST5 in CFB mode",
  "cast5-ecb": "CAST5 in ECB mode",
  "cast5-ofb": "CAST5 in OFB mode",
  "des-cbc": "DES in CBC mode",
  "des-cfb": "DES in CBC mode",
  "des-ofb": "DES in OFB mode",
  "des-ecb": "DES in ECB mode",
  "des-ede-cbc": "Two key triple DES EDE in CBC mode",
  "des-ede": "Two key triple DES EDE in ECB mode",
  "des-ede-cfb": "Two key triple DES EDE in CFB mode",
  "des-ede-ofb": "Two key triple DES EDE in OFB mode",
  "des-ede3-cbc": "Three key triple DES EDE in CBC mode",
  "des-ede3": "Three key triple DES EDE in ECB mode",
  "des-ede3-cfb": "Three key triple DES EDE CFB mode",
  "des-ede3-ofb": "Three key triple DES EDE in OFB mode",
  "desx": "DESX algorithm.",
  "gost89": "GOST 28147-89 in CFB mode (provided by ccgost engine)",
  "gost89-cnt": "GOST 28147-89 in CNT mode (provided by ccgost engine)",
  "idea-cbc": "IDEA algorithm in CBC mode",
  "idea-cfb": "IDEA in CFB mode",
  "idea-ecb": "IDEA in ECB mode",
  "idea-ofb": "IDEA in OFB mode",
  "rc2-cbc": "128 bit RC2 in CBC mode",
  "rc2-cfb": "128 bit RC2 in CFB mode",
  "rc2-ecb": "128 bit RC2 in ECB mode",
  "rc2-ofb": "128 bit RC2 in OFB mode",
  "rc2-64-cbc": "64 bit RC2 in CBC mode",
  "rc2-40-cbc": "40 bit RC2 in CBC mode",
  "rc4": "128 bit RC4",
  "rc4-64": "64 bit RC4",
  "rc4-40": "40 bit RC4",
  "rc5-cbc": "RC5 cipher in CBC mode",
  "rc5-cfb": "RC5 cipher in CFB mode",
  "rc5-ecb": "RC5 cipher in ECB mode",
  "rc5-ofb": "RC5 cipher in OFB mode",
  "aes-256-cbc": "256 bit AES in CBC mode",
  "aes-256-cfb": "256 bit AES in 128 bit CFB mode",
  "aes-256-cfb1": "256 bit AES in 1 bit CFB mode",
  "aes-256-cfb8": "256 bit AES in 8 bit CFB mode",
  "aes-256-ecb": "256 bit AES in ECB mode",
  "aes-256-ofb": "256 bit AES in OFB mode",
  "aes-192-cbc": "192 bit AES in CBC mode",
  "aes-192-cfb": "192 bit AES in 128 bit CFB mode",
  "aes-192-cfb1": "192 bit AES in 1 bit CFB mode",
  "aes-192-cfb8": "192 bit AES in 8 bit CFB mode",
  "aes-192-ecb": "192 bit AES in ECB mode",
  "aes-192-ofb": "192 bit AES in OFB mode",
  "aes-128-cbc": "128 bit AES in CBC mode",
  "aes-128-cfb": "128 bit AES in 128 bit CFB mode",
  "aes-128-cfb1": "128 bit AES in 1 bit CFB mode",
  "aes-128-cfb8": "128 bit AES in 8 bit CFB mode",
  "aes-128-ecb": "128 bit AES in ECB mode",
  "aes-128-ofb": "128 bit AES in OFB mode",
  }

  var mcodes=Object.keys(modes);
  var i=0;

  var plaintext="asdfqwerzxcvtyuighjkbnm,op][l;'m,./1234567890-=SAFZXVQWRTYIGHKJBNM<IOP{KL:\"M<?>!@#$%^&*()(_+~`";
  var pass=";lkjhgfdsaqwer";
  for (i=0;i<mcodes.length;i++)
  {
    printf("testing %-40s - ",modes[mcodes[i]]);
    try{
      encBuffer = crypto.encrypt({
        pass: pass,
        cipher: mcodes[i],
        data: plaintext
      });

      decBuffer = crypto.decrypt({
        pass: pass,
        cipher: mcodes[i],
        data: encBuffer,
      });
    } catch(e) {
      printf(">>>>> UNSUPPORTED <<<<<\n");
      continue;
    }
      
    if(plaintext==bufferToString(decBuffer))
      printf("passed\n");
    else
      printf(">>>>> FAILED <<<<<\n");
  }
}