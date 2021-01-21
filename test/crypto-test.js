/* make printf et. al. global */
rampart.globalize(rampart.utils);

var crypto = require("rampart-crypto");

testmodes();

function testFeature(name,test)
{
    var error=false;
    rampart.utils.printf("testing %-40s - ", name);
    fflush(stdout);
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    if(test)
        rampart.utils.printf("passed\n")
    else
    {
        rampart.utils.printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}

var hashes = [
"2aae6c35c94fcfb415dbe95f408b9ce91ee846ed", "2f05477fc24bb4faefd86517156dafdecec45b8ad3cf2522a563582b", "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9", "fdbd8e75a67f29f701a4e040385e2e23986303ea10239211af907fcbb83578b3e417cb71ce646efd0819dd8c088de1bd", "309ecc489c12d6eb4cc40f50c902f2b4d0ed77ee511a7c7a9bcd3ca86d4cd86f989dd35bc5ff499670da34255b45b0cfd830e81f605dcf7dc5542e93ae9cd76f", "aa010fbc1d14c795d86ef98c95479d17", "5eb63bbbe01eeed093cb22bb8f5acdc3", "dfb7f18c77e928bb56faeb2da27291bd790bc1045cde45f3210bb6c5", "644bcc7e564373040999aac89e7622f3ca71fba1d972fd94a31c3bfbf24e3938", "83bff28dde1b1bf5810071c6643c08e5b05bdb836effd70b403ea8ea0a634dc4997eb1053aa3593f590f9c63630dd90b", "840006653e9ac9e95117a15c915caab81662918e925de9e004f774ff82d7079a40d4d27b1b372657c61d46d470304c88c788b3a4527ad074d1dccbee5dbaa99a", "021ced8799296ceca557832ab941a50b4a11f83478cf141f51f933f653ab9fbcc05a037cddbed06e309bf334942c4e58cdf1a46e237911ccd7fcf9787cbc7fd0", "9aec6806794561107e594b1f6a8a6b0c92a0cba9acf5e5e93cca06f781813b0b", "9ce411cc3449bf73a54568d783b5900d", "98c615784ccb5fe5936fbc0cbe9dfdb408d92f0f", "22e0d52336f64a998085078b05a6e37b26f8120f43bf4db4c43a64ee", "0ac561fac838104e3f2e4ad107b4bee3e938bf15f2b15f009ccccd61a913f017", "3a9159f071e4dd1c8c4f968607c30942", "369771bb2cb9d2b04c1d54cca487e372d9f187f73f7ba3f65b95c8ee7798c527", "44f0061e69fa6fdfc290c494654a05dc0c053da7e5c52b84ef93a9d67d3fff88"
];
var hfuncs =  [
    "sha1", "sha224", "sha256", "sha384", "sha512", "md4", "md5",
    "sha3_224", "sha3_256", "sha3_384", "sha3_512",
    "blake2b512",        "blake2s256",
    "mdc2", "rmd160",
    "sha512_224", "sha512_256", "shake128", 'shake256',
    'sm3'
];
var i;

for (i=0;i<hfuncs.length;i++)
{
  testFeature( hfuncs[i]+" hash of 'hello world'", function(){
    var res=crypto[hfuncs[i]]("hello world");
    return res==hashes[i];
  });
}

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

var key;
testFeature("rsa create key", function(){
    key = crypto.rsa_gen_key(4096, "password");
    return true;
});

testFeature("rsa en/decrypt", function(){
    var txt="some text to encrypt";

    var out=crypto.rsa_pub_encrypt(txt, key.public);

    var dec = bufferToString(crypto.rsa_priv_decrypt(out, key.private, null, "password"));

    return (dec == txt);
});

testFeature("rsa sign/verify", function(){
    var txt="some text to encrypt";

    var sig = crypto.rsa_sign(txt, key.private, "password");
    var res = crypto.rsa_verify(txt, key.public, sig);

    return res;
});


hashes = [
  "89ddafb4017e0e33cb6c0f9e9ddd6539c5f1d3cc", "9e167b536cb7ae1fdb972f9ef5ad84eccb3b4171c5fd84235b5f364d", "f49cf057cd4de7c1a4cb0b051570372892674487333ac5ab3ea603f29aec9ffe", "262495541c88f3dd303e8a89e27ba0f0c5972afd088b424aed1998d795d6bb4e4fb78e5df1e0ef8b62e3d977d28bef58", "7f416275882395b49071c91caebc4d300b7aed08cb891680371154b61d867428271d3d00425ea7728b91344e442846db66b15b9043160c9a95d02aac6f514dd7", "480b381bb47c3e1dea3dad104b794619", "f91fd9d43fbe61a3134e8236d0fa1af8", "443c63decab0ce41e6f7887a10cb686371e213c7a7d5e1352b47c0fa", "091664f38388f580ef0a50d49b817a6821a80d5615ee3627b31c60befaf788a0", "26f8e3d1eaf49fd9ca4527dc701274b7dcc9e5eb3fb75eea26f637aeee5012fc48b6cf0352022a60552def4bcc2a89bf", "96273b35615d55b7ccadf3ce13757ca31ec239ebdae27d7bb671260eb6da26ffb229ddde669dd5d3914c9a1d1620a018d2586a1573034a489bf4fc3711137a2b", "60d0230e31c53e0b618e4fc4a3a5004bbfd8636ba1dbfbcb6eb092f7a75872abf41d613da998459e59fc68f9608838d75fb89976825a8dd9783f8e6d884c7fc2", "88e4a650a34aa66abbc32d6a14a2f130e98752f0429fb7bcb74fcc4510a4b57f", "664766b7d92b3c863bf0f9251e4fb7ba", "847d6acf7649d46b1b52fc801208f5ab3c2446fb", "b608f565216e5bb60a597b8b9b4304e3cfb977bdc3bd001fad0fef94", "459f266ae1a2dbc0f994fadea6acb24786653f6e56a86427d3d3a7c218042beb", "d89fe1c144685fd7f3004ae1b8d0cb8b0aa3ac489eb2878860e0ba70d0725733"
];
hfuncs =  [
    "sha1", "sha224", "sha256", "sha384", "sha512", "md4", "md5",
    "sha3-224", "sha3-256", "sha3-384", "sha3-512",
    "blake2b512",        "blake2s256",
    "mdc2", "rmd160",
    "sha512-224", "sha512-256",
    'sm3'
];

for (i=0;i<hfuncs.length;i++)
{
  testFeature("hmac "+ hfuncs[i], function(){
    var ret=crypto.hmac("mysecret","mydata", hfuncs[i]);
    return ret==hashes[i];
  });
}
/*
printf("\ncrypt of this file with aes-256-cbc (sha512 hashes before and after):\n%s\n%s\n",hash,comphash);
fprintf("crypto-copy.js.enc","%s",encBuffer);
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
