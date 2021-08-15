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
    key: rampart.utils.stringToBuffer("01234567890123456789012345678901"),
    iv: rampart.utils.stringToBuffer("0123456789012345"),
    cipher: "aes-256-cbc",
    data: "encrypt and decrypt of a string with key,iv and aes-256-cbc"
  });

  var decBuffer = crypto.decrypt({
    key: rampart.utils.stringToBuffer("01234567890123456789012345678901"),
    iv: rampart.utils.stringToBuffer("0123456789012345"),
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

  var file=readFile(process.script);

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
    key = crypto.rsa_gen_key(2048, "password");
    return true;
});

testFeature("rsa en/decrypt", function(){
    var txt="some text to encrypt";

    var out=crypto.rsa_pub_encrypt(txt, key.public);
    var out2=crypto.rsa_pub_encrypt(txt, key.rsa_public); 
    var dec  = bufferToString(crypto.rsa_priv_decrypt(out, key.private, null, "password"));
    var dec2 = bufferToString(crypto.rsa_priv_decrypt(out, key.rsa_private, null, "password"));
    var dec3 = bufferToString(crypto.rsa_priv_decrypt(out2, key.private, null, "password"));
    var dec4 = bufferToString(crypto.rsa_priv_decrypt(out2, key.rsa_private, null, "password"));

    return (dec == txt && dec2 == txt && dec3 == txt && dec4 == txt);
});

testFeature("rsa sign/verify", function(){
    var txt="some text to sign";

    var sig = crypto.rsa_sign(txt, key.private, "password");
    var sig2= crypto.rsa_sign(txt, key.rsa_private, "password");
    var res = crypto.rsa_verify(txt, key.public, sig);
    var res2= crypto.rsa_verify(txt, key.public, sig2); 
    var res3= crypto.rsa_verify(txt, key.rsa_public, sig);
    var res4= crypto.rsa_verify(txt, key.rsa_public, sig2); 
    return (res && res2 && res3 && res4);
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


var JSBI= crypto.JSBI;

var intn_data = [
  {expected: '-1', n: 3, x: '15'},
  {expected: '-2', n: 3, x: '14'},
  {expected: '-3', n: 3, x: '13'},
  {expected: '-4', n: 3, x: '12'},
  {expected: '3', n: 3, x: '11'},
  {expected: '2', n: 3, x: '10'},
  {expected: '1', n: 3, x: '9'},
  {expected: '0', n: 3, x: '8'},
  {expected: '-1', n: 3, x: '7'},
  {expected: '-2', n: 3, x: '6'},
  {expected: '-3', n: 3, x: '5'},
  {expected: '-4', n: 3, x: '4'},
  {expected: '3', n: 3, x: '3'},
  {expected: '2', n: 3, x: '2'},
  {expected: '1', n: 3, x: '1'},
  {expected: '0', n: 3, x: '0'},
  {expected: '-1', n: 3, x: '-1'},
  {expected: '-2', n: 3, x: '-2'},
  {expected: '-3', n: 3, x: '-3'},
  {expected: '-4', n: 3, x: '-4'},
  {expected: '3', n: 3, x: '-5'},
  {expected: '2', n: 3, x: '-6'},
  {expected: '1', n: 3, x: '-7'},
  {expected: '0', n: 3, x: '-8'},
  {expected: '-1', n: 3, x: '-9'},
  {expected: '-2', n: 3, x: '-10'},
  {expected: '-3', n: 3, x: '-11'},
  {expected: '-4', n: 3, x: '-12'},
  {expected: '3', n: 3, x: '-13'},
  {expected: '2', n: 3, x: '-14'},
  {expected: '1', n: 3, x: '-15'},

  {expected: '254', n: 10, x: '254'},
  {expected: '255', n: 10, x: '255'},
  {expected: '256', n: 10, x: '256'},
  {expected: '257', n: 10, x: '257'},
  {expected: '510', n: 10, x: '510'},
  {expected: '511', n: 10, x: '511'},
  {expected: '-512', n: 10, x: '512'},
  {expected: '-511', n: 10, x: '513'},
  {expected: '-2', n: 10, x: '1022'},
  {expected: '-1', n: 10, x: '1023'},
  {expected: '0', n: 10, x: '1024'},
  {expected: '1', n: 10, x: '1025'},

  {expected: '-254', n: 10, x: '-254'},
  {expected: '-255', n: 10, x: '-255'},
  {expected: '-256', n: 10, x: '-256'},
  {expected: '-257', n: 10, x: '-257'},
  {expected: '-510', n: 10, x: '-510'},
  {expected: '-511', n: 10, x: '-511'},
  {expected: '-512', n: 10, x: '-512'},
  {expected: '511', n: 10, x: '-513'},
  {expected: '2', n: 10, x: '-1022'},
  {expected: '1', n: 10, x: '-1023'},
  {expected: '0', n: 10, x: '-1024'},
  {expected: '-1', n: 10, x: '-1025'},

  {expected: '0', n: 0, x: '0'},
  {expected: '0', n: 1, x: '0'},
  {expected: '0', n: 16, x: '0'},
  {expected: '0', n: 31, x: '0'},
  {expected: '0', n: 32, x: '0'},
  {expected: '0', n: 33, x: '0'},
  {expected: '0', n: 63, x: '0'},
  {expected: '0', n: 64, x: '0'},
  {expected: '0', n: 65, x: '0'},
  {expected: '0', n: 127, x: '0'},
  {expected: '0', n: 128, x: '0'},
  {expected: '0', n: 129, x: '0'},

  {expected: '0', n: 0, x: '42'},
  {expected: '0', n: 1, x: '42'},
  {expected: '42', n: 16, x: '42'},
  {expected: '42', n: 31, x: '42'},
  {expected: '42', n: 32, x: '42'},
  {expected: '42', n: 33, x: '42'},
  {expected: '42', n: 63, x: '42'},
  {expected: '42', n: 64, x: '42'},
  {expected: '42', n: 65, x: '42'},
  {expected: '42', n: 127, x: '42'},
  {expected: '42', n: 128, x: '42'},
  {expected: '42', n: 129, x: '42'},

  {expected: '0', n: 0, x: '-42'},
  {expected: '0', n: 1, x: '-42'},
  {expected: '-42', n: 16, x: '-42'},
  {expected: '-42', n: 31, x: '-42'},
  {expected: '-42', n: 32, x: '-42'},
  {expected: '-42', n: 33, x: '-42'},
  {expected: '-42', n: 63, x: '-42'},
  {expected: '-42', n: 64, x: '-42'},
  {expected: '-42', n: 65, x: '-42'},
  {expected: '-42', n: 127, x: '-42'},
  {expected: '-42', n: 128, x: '-42'},
  {expected: '-42', n: 129, x: '-42'},

  {expected: '0', n: 0, x: '4294967295'},
  {expected: '-1', n: 1, x: '4294967295'},
  {expected: '-1', n: 16, x: '4294967295'},
  {expected: '-1', n: 31, x: '4294967295'},
  {expected: '-1', n: 32, x: '4294967295'},
  {expected: '4294967295', n: 33, x: '4294967295'},
  {expected: '4294967295', n: 63, x: '4294967295'},
  {expected: '4294967295', n: 64, x: '4294967295'},
  {expected: '4294967295', n: 65, x: '4294967295'},
  {expected: '4294967295', n: 127, x: '4294967295'},
  {expected: '4294967295', n: 128, x: '4294967295'},
  {expected: '4294967295', n: 129, x: '4294967295'},

  {expected: '0', n: 0, x: '-4294967295'},
  {expected: '-1', n: 1, x: '-4294967295'},
  {expected: '1', n: 16, x: '-4294967295'},
  {expected: '1', n: 31, x: '-4294967295'},
  {expected: '1', n: 32, x: '-4294967295'},
  {expected: '-4294967295', n: 33, x: '-4294967295'},
  {expected: '-4294967295', n: 63, x: '-4294967295'},
  {expected: '-4294967295', n: 64, x: '-4294967295'},
  {expected: '-4294967295', n: 65, x: '-4294967295'},
  {expected: '-4294967295', n: 127, x: '-4294967295'},
  {expected: '-4294967295', n: 128, x: '-4294967295'},
  {expected: '-4294967295', n: 129, x: '-4294967295'},

  {expected: '42', n: 2**32, x: '42'},
  {expected: '4294967295', n: 2**32, x: '4294967295'},
  {expected: '4294967296', n: 2**32, x: '4294967296'},
  {expected: '4294967297', n: 2**32, x: '4294967297'},

  {expected: '0', n: {}, x: '12'},
  {expected: '0', n: 2.9999, x: '12'},
  {expected: '-4', n: 3.1234, x: '12'},

  {expected: '-4', n: 3, x: '12'},
  {expected: '0x123456789abcdef', n: 64, x: '0xabcdef0123456789abcdef'},

  // Regression test for crbug.com/v8/8426.
  {expected: '-0x8000000000000000', n: 64, x: '-0x8000000000000000'},
];

var uintn_data = [
  {expected: '7', n: 3, x: '15'},
  {expected: '6', n: 3, x: '14'},
  {expected: '5', n: 3, x: '13'},
  {expected: '4', n: 3, x: '12'},
  {expected: '3', n: 3, x: '11'},
  {expected: '2', n: 3, x: '10'},
  {expected: '1', n: 3, x: '9'},
  {expected: '0', n: 3, x: '8'},
  {expected: '7', n: 3, x: '7'},
  {expected: '6', n: 3, x: '6'},
  {expected: '5', n: 3, x: '5'},
  {expected: '4', n: 3, x: '4'},
  {expected: '3', n: 3, x: '3'},
  {expected: '2', n: 3, x: '2'},
  {expected: '1', n: 3, x: '1'},
  {expected: '0', n: 3, x: '0'},
  {expected: '7', n: 3, x: '-1'},
  {expected: '6', n: 3, x: '-2'},
  {expected: '5', n: 3, x: '-3'},
  {expected: '4', n: 3, x: '-4'},
  {expected: '3', n: 3, x: '-5'},
  {expected: '2', n: 3, x: '-6'},
  {expected: '1', n: 3, x: '-7'},
  {expected: '0', n: 3, x: '-8'},
  {expected: '7', n: 3, x: '-9'},
  {expected: '6', n: 3, x: '-10'},
  {expected: '5', n: 3, x: '-11'},
  {expected: '4', n: 3, x: '-12'},
  {expected: '3', n: 3, x: '-13'},
  {expected: '2', n: 3, x: '-14'},
  {expected: '1', n: 3, x: '-15'},

  {expected: '254', n: 10, x: '254'},
  {expected: '255', n: 10, x: '255'},
  {expected: '256', n: 10, x: '256'},
  {expected: '257', n: 10, x: '257'},
  {expected: '510', n: 10, x: '510'},
  {expected: '511', n: 10, x: '511'},
  {expected: '512', n: 10, x: '512'},
  {expected: '513', n: 10, x: '513'},
  {expected: '1022', n: 10, x: '1022'},
  {expected: '1023', n: 10, x: '1023'},
  {expected: '0', n: 10, x: '1024'},
  {expected: '1', n: 10, x: '1025'},

  {expected: '770', n: 10, x: '-254'},
  {expected: '769', n: 10, x: '-255'},
  {expected: '768', n: 10, x: '-256'},
  {expected: '767', n: 10, x: '-257'},
  {expected: '514', n: 10, x: '-510'},
  {expected: '513', n: 10, x: '-511'},
  {expected: '512', n: 10, x: '-512'},
  {expected: '511', n: 10, x: '-513'},
  {expected: '2', n: 10, x: '-1022'},
  {expected: '1', n: 10, x: '-1023'},
  {expected: '0', n: 10, x: '-1024'},
  {expected: '1023', n: 10, x: '-1025'},

  {expected: '0', n: 0, x: '0'},
  {expected: '0', n: 1, x: '0'},
  {expected: '0', n: 16, x: '0'},
  {expected: '0', n: 31, x: '0'},
  {expected: '0', n: 32, x: '0'},
  {expected: '0', n: 33, x: '0'},
  {expected: '0', n: 63, x: '0'},
  {expected: '0', n: 64, x: '0'},
  {expected: '0', n: 65, x: '0'},
  {expected: '0', n: 127, x: '0'},
  {expected: '0', n: 128, x: '0'},
  {expected: '0', n: 129, x: '0'},

  {expected: '0', n: 0, x: '42'},
  {expected: '0', n: 1, x: '42'},
  {expected: '42', n: 16, x: '42'},
  {expected: '42', n: 31, x: '42'},
  {expected: '42', n: 32, x: '42'},
  {expected: '42', n: 33, x: '42'},
  {expected: '42', n: 63, x: '42'},
  {expected: '42', n: 64, x: '42'},
  {expected: '42', n: 65, x: '42'},
  {expected: '42', n: 127, x: '42'},
  {expected: '42', n: 128, x: '42'},
  {expected: '42', n: 129, x: '42'},

  {expected: '0', n: 0, x: '-42'},
  {expected: '0', n: 1, x: '-42'},
  {expected: '65494', n: 16, x: '-42'},
  {expected: '2147483606', n: 31, x: '-42'},
  {expected: '4294967254', n: 32, x: '-42'},
  {expected: '8589934550', n: 33, x: '-42'},
  {expected: '9223372036854775766', n: 63, x: '-42'},
  {expected: '18446744073709551574', n: 64, x: '-42'},
  {expected: '36893488147419103190', n: 65, x: '-42'},
  {expected: '170141183460469231731687303715884105686', n: 127, x: '-42'},
  {expected: '340282366920938463463374607431768211414', n: 128, x: '-42'},
  {expected: '680564733841876926926749214863536422870', n: 129, x: '-42'},

  {expected: '0', n: 0, x: '4294967295'},
  {expected: '1', n: 1, x: '4294967295'},
  {expected: '65535', n: 16, x: '4294967295'},
  {expected: '2147483647', n: 31, x: '4294967295'},
  {expected: '4294967295', n: 32, x: '4294967295'},
  {expected: '4294967295', n: 33, x: '4294967295'},
  {expected: '4294967295', n: 63, x: '4294967295'},
  {expected: '4294967295', n: 64, x: '4294967295'},
  {expected: '4294967295', n: 65, x: '4294967295'},
  {expected: '4294967295', n: 127, x: '4294967295'},
  {expected: '4294967295', n: 128, x: '4294967295'},
  {expected: '4294967295', n: 129, x: '4294967295'},

  {expected: '0', n: 0, x: '-4294967295'},
  {expected: '1', n: 1, x: '-4294967295'},
  {expected: '1', n: 16, x: '-4294967295'},
  {expected: '1', n: 31, x: '-4294967295'},
  {expected: '1', n: 32, x: '-4294967295'},
  {expected: '4294967297', n: 33, x: '-4294967295'},
  {expected: '9223372032559808513', n: 63, x: '-4294967295'},
  {expected: '18446744069414584321', n: 64, x: '-4294967295'},
  {expected: '36893488143124135937', n: 65, x: '-4294967295'},
  {expected: '170141183460469231731687303711589138433', n: 127, x: '-4294967295'},
  {expected: '340282366920938463463374607427473244161', n: 128, x: '-4294967295'},
  {expected: '680564733841876926926749214859241455617', n: 129, x: '-4294967295'},

  {expected: '42', n: 2**32, x: '42'},
  {expected: '4294967295', n: 2**32, x: '4294967295'},
  {expected: '4294967296', n: 2**32, x: '4294967296'},
  {expected: '4294967297', n: 2**32, x: '4294967297'},

  {expected: '0x7234567812345678', n: 63, x: '0xf234567812345678'},

  {expected: '0', n: {}, x: '12'},
  {expected: '0', n: 2.9999, x: '12'},
  {expected: '4', n: 3.1234, x: '12'},

  {expected: '4', n: 3, x: '12'},

  // crbug.com/936506
  {expected: '1', n: 15, x: '0x100000001'},
  {expected: '1', n: 15, x: '0x10000000000000001'},
];

for (var i = 0; i<intn_data.length; i++) {
  var test = intn_data[i]

  var x = JSBI.BigInt(test.x);

  var expected = JSBI.BigInt(test.expected);
  var result = JSBI.asIntN(test.n, x);

  testFeature(`BigInt asIntN ${i+1}`,JSBI.equal(expected, result));

}
for (var i = 0; i<uintn_data.length; i++) {
  var test = uintn_data[i];

  var x = JSBI.BigInt(test.x);
  var expected = JSBI.BigInt(test.expected);
  var result = JSBI.asUintN(test.n, x);

  testFeature(`BigInt asUintN ${i+1}`,JSBI.equal(expected, result));
}


// tests.mjs
{
  // Test the example from the README.
  var max = JSBI.BigInt(Number.MAX_SAFE_INTEGER);
  // → 9007199254740991
  var other = JSBI.BigInt(2);
  var result = JSBI.add(max, other);
  // → 9007199254740993
  testFeature("BigInt MAX_SAFE_INTEGER string", '9007199254740993' === result.toString());
  // Test `JSBI.toNumber` as well.
  testFeature("BigInt MAX_SAFE_INTEGER number", 9007199254740993 === JSBI.toNumber(result));

  // Corner cases near the single digit threshold.
  testFeature("BigInt Corner Case", JSBI.LT(JSBI.BigInt('0x100000000'), 0x100000001));
  testFeature("BigInt Corner Case", JSBI.EQ(JSBI.BigInt('0xFFFFFFFF'), 0xFFFFFFFF));
  testFeature("BigInt Corner Case", JSBI.EQ(JSBI.BigInt('0x7FFFFFFF'), 0x7FFFFFFF));
  testFeature("BigInt Corner Case", JSBI.EQ(JSBI.BigInt(0x7FFFFFFF), 0x7FFFFFFF));
  testFeature("BigInt Corner Case", JSBI.EQ(JSBI.BigInt(-0x7FFFFFFF), -0x7FFFFFFF));
  testFeature("BigInt Corner Case", JSBI.LT(JSBI.BigInt(0x7FFFFFF0), 0x7FFFFFFF));
  testFeature("BigInt Corner Case", JSBI.GT(JSBI.BigInt(-0x7FFFFFF0), -0x7FFFFFFF));

  // Regression test for issue #63.
  testFeature("BigInt exponential to bignum",
      JSBI.BigInt(4.4384296245614243e+42).toString() ===
      '4438429624561424320047307980392507864252416');
  var str = '3361387880631608742970259577528807057005903';
  console.assert(JSBI.toNumber(JSBI.BigInt(str)) === 3.361387880631609e+42);
}

var TESTS = [
  {
    operation: 'add',
    a: '-0xF72AAE64D54951CAE560D9B4531CE6CF02426F8CD601B77',
    b: '-0xF3CF5EDD759DBCC7449962CDB52AE0295BE7306D51555C70',
    expected: '-0x1034209C3C2F251E3F2EF7068FA5CAE964C0B57661EB577E7',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/pull/14
    operation: 'remainder',
    a: '0x62A49213A5CD1793CB4518A12CA4FB5F3AB6DBD8B465D0D86975CEBDA6B6093',
    b: '0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF',
    expected: '0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/pull/14#issuecomment-439484605
    operation: 'remainder',
    a: '0x10000000000000000',
    b: '0x100000001',
    expected: '0x1',
  },
  { 
    operation: 'remainder',
    a: '-123791497293754987123',
    b: '-123',
    expected: '-40',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseAnd',
    a: '0b10000010001000100010001000100010001000100010001000100010001000100',
    b: '-0b10000000000000000000000000000000000000000000000000000000000000001',
    expected: '0b10001000100010001000100010001000100010001000100010001000100',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseAnd',
    a: '-0b10000010001000100010001000100010001000100010001000100010001000100',
    b: '-0b10000000000000000000000000000000000000000000000000000000000000001',
    expected: '-18754189808271377476'
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseAnd',
    b: '-0b10000010001000100010001000100010001000100010001000100010001000100',
    a: '-0b10000000000000000000000000000000000000000000000000000000000000001',
    expected: '-18754189808271377476'
  },

  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseAnd',
    b: '-0b10000010001000100010001000100010001000100010001000100010001000100',
    a: '-0b10000000000000000000000000001',
    expected: '-18754189808539812932'
  },

  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '0',
    b: '-0b1111111111111111111111111111111111111111111111111111111111111111',
    expected: '-0b1111111111111111111111111111111111111111111111111111111111111111',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    b: '0',
    a: '-0b1111111111111111111111111111111111111111111111111111111111111111',
    expected: '-0b1111111111111111111111111111111111111111111111111111111111111111',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '0',
    b: '0b1111111111111111111111111111111111111111111111111111111111111111',
    expected: '0b1111111111111111111111111111111111111111111111111111111111111111',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '-1',
    b: '0b1111111111111111111111111111111111111111111111111111111111111111',
    expected: '-0b10000000000000000000000000000000000000000000000000000000000000000',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    b: '-1',
    a: '0b1111111111111111111111111111111111111111111111111111111111111111',
    expected: '-0b10000000000000000000000000000000000000000000000000000000000000000',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '-1',
    b: '-0b1111111111111111111111111111111111111111111111111111111111111111',
    expected: '0b1111111111111111111111111111111111111111111111111111111111111110',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    b: '-1',
    a: '-0b1111111111111111111111111111111111111111111111111111111111111111',
    expected: '0b1111111111111111111111111111111111111111111111111111111111111110',
  },



  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '1',
    b: '0b11111111',
    expected: '0b11111110',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '-1',
    b: '-0b11111111',
    expected: '0b11111110',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '-1',
    b: '0b11111111',
    expected: '-0b100000000',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '1',
    b: '-0b11111111',
    expected: '-0b100000000',
  },
  { // https://github.com/GoogleChromeLabs/jsbi/issues/44#issue-630518844
    operation: 'bitwiseXor',
    a: '165',
    b: '-128',
    expected: '-219',
  },



  {  // https://github.com/GoogleChromeLabs/jsbi/issues/57
    operation: 'signedRightShift',
    a: '-0xFFFFFFFFFFFFFFFF',
    b: '32',
    expected: '-0x100000000',
  },

  {
    operation: 'bitwiseOr',
    a: '1',
    b: '0b11111111',
    expected: '0b11111111',
  },
  {
    operation: 'bitwiseOr',
    a: '-1',
    b: '0b11111111',
    expected: '-0b1',
  },
  {
    operation: 'bitwiseOr',
    a: '1',
    b: '-0b11111111',
    expected: '-0255',
  },
  {
    operation: 'bitwiseOr',
    a: '0',
    b: '-0b11111111',
    expected: '-255',
  },
  {
    operation: 'bitwiseOr',
    a: '61234',
    b: '0b1010101010',
    expected: '61370',
  },
  {
    operation: 'bitwiseOr',
    a: '-61234',
    b: '-0b1010101010',
    expected: '-546',
  },
  {
    operation: 'bitwiseOr',
    a: '61234',
    b: '-0b1010101010',
    expected: '-138',
  },
  {
    operation: 'bitwiseOr',
    a: '-61234',
    b: '0b1010101010',
    expected: '-60690',
  },


];

// https://github.com/GoogleChromeLabs/jsbi/issues/36
(function() {
  var VALID = ['123', ' 123 ', '   123   '];
  var INVALID = ['x123', 'x 123', ' 123x', '123 x', '123  xx', '123 ?a',
                   '-0o0', /* wont fix: '-0x0',  '-0b0', '-0x1'*/];
  for (var i=0; i<VALID.length; i++) {
    v=VALID[i]
    var result = JSBI.BigInt(v);
    testFeature(`BigInt valid string ${i+1}`,
      JSBI.equal(result, JSBI.BigInt(123)));
  }

  for (var i=0; i<INVALID.length; i++) {
    var inv=INVALID[i];
    var isInvalid=0;
    try {
      var result = JSBI.BigInt(inv);
    } catch (exception) {
      isInvalid=1;
    }
    testFeature(`BigInt invalid string`, isInvalid);
  }
})();

var zero = JSBI.BigInt(0);

function hex(jsb) {
  if (JSBI.lessThan(jsb, zero)) {
    return `-0x${ jsb.toString(16).slice(1).toUpperCase() }`;
  }
  return `0x${ jsb.toString(16).toUpperCase() }`;
}

for (var i = 0; i<TESTS.length; i++){
  var test = TESTS[i];
  var operation = test.operation;
  var a = JSBI.BigInt(test.a);
  var b = JSBI.BigInt(test.b);
  var expected = JSBI.BigInt(test.expected);
  var result = JSBI[operation](a, b);

  testFeature(`BigInt op test ${i+1} - ${operation}`,
    JSBI.equal(result, expected) );

}


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

