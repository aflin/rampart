var crypto = require("rampart-crypto");
var curl = require("rampart-curl");
var api_test = "https://acme-staging-v02.api.letsencrypt.org"
var api_live = "https://acme-v02.api.letsencrypt.org";
var api;
var acct = {};
var auth;

var cb = {
    debug: function(msg) { rampart.utils.fprintf(stderr, '%s', msg); rampart.utils.fflush(rampart.utils.stderr);},
    info: function(msg) { rampart.utils.printf('%s', msg); rampart.utils.fflush(rampart.utils.stdout);},
    error: function(msg) { rampart.utils.fprintf(stderr, '%s', msg); },
    trace: function(msg) { rampart.utils.fprintf(stderr, '%s', msg); rampart.utils.fflush(rampart.utils.stderr);}
}    

function dprintf() {
    var i=0, ret;

    if( typeof cb.debug != 'function' ) return 0;

    ret=rampart.utils.sprintf.apply(null, arguments);
    cb.debug(ret);
}

function tprintf() {
    var i=0, ret;

    if( typeof cb.trace != 'function' ) return 0;

    ret=rampart.utils.sprintf.apply(null, arguments);
    cb.trace(ret);
}

function eprintf() {
    var i=0, ret;

    ret=rampart.utils.sprintf.apply(null, arguments);
    if (typeof cb.error == 'function' )
        cb.error(ret);
    throw("Certificate creation process failed: "+ret);
}

function iprintf() {
    var i=0, ret;

    if( typeof cb.info != 'function' ) return 0;

    ret=rampart.utils.sprintf.apply(null, arguments);
    cb.info(ret);
    rampart.utils.fflush(rampart.utils.stderr);
}


var i=0;
var home = process.env["HOME"];

var domains;
var maindom;
var email;
var files = {}
/*
var acct_dir;
var acct_pub_file;
var acct_priv_file;
var acct_url_file;
var server_key_file;
var server_cert_file;
*/
/* TODO: function to check if domains are local 
         and port 80 is open                    */
function localcheck() {
    return true;
}

function setopts(op, check){
    if(rampart.utils.getType(op.domains) == 'Array')
        domains = op.domains;
    else if (rampart.utils.getType(op.domains) == 'String')
        domains = [op.domains];
    else if (!check)
        throw("letsencrypt - no domain name(s) privided");

    if(domains && domains.length)
        maindom = domains[0];

    if (check && !maindom && !op.server_cert_file)
        throw("letsencrypt.check_cert - must provide option 'domains' or 'server_cert_file'"); 

    if (op.test === true)
        api=api_test;
    else
        api=api_live;

    iprintf("using api at %s\n", api);

    if (rampart.utils.getType(op.email) == 'String')
        email=op.email;
    else if (op.emain !== undefined)
        throw("letsencrypt - email must be a string");

    if (rampart.utils.getType(op.directory) == 'String')
    {
        files.acct_dir=op.directory.replace(/\/$/, '');
    }
    else if (op.directory !== undefined)
        throw("letsencrypt - directory must be a string");
    else
        files.acct_dir = home + '/.rampart/' + maindom;

    if (typeof op.save_challenges == 'function')
        cb.save_challenges = op.save_challenges;
    else if (op.save_challenges !== undefined)
        throw("letsencrypt - save_challenges must be a function"); 
    else
        localcheck();

    files.acct_pub_file = files.acct_dir + '/account_pub.key';
    files.acct_priv_file= files.acct_dir + '/account_priv.key';
    files.acct_url_file = files.acct_dir + '/account.url';
    files.server_key_file = files.acct_dir + '/server_priv.key';
    files.server_cert_file = files.acct_dir + '/server.cert';

    var k = ["acct_pub_file", "acct_priv_file", "acct_url_file", "server_key_file", "server_cert_file"]

    /* allow override of file names */
    for (var i=0; i<k.length; i++) {
        if(rampart.utils.getType(op[k[i]]) == 'String')
            files[k[i]] = op[k[i]];
    }

    if(typeof op.debug == 'function')
        cb.debug = op.debug;
    else if (op.debug !== true)
        cb.debug = false;

    if(typeof op.trace == 'function')
        cb.trace = op.trace;
    else if (op.trace !== true)
        cb.trace = false;

    if(typeof op.info == 'function')
        cb.info = op.info;
    else if (op.info !== true)
        cb.info = false;

    dprintf("Using files:\n%3J\n", files);
}

function check_cert(opts){
    setopts(opts, true);
    if(!stat(files.server_cert_file))
        throw("cert file "+files.server_cert_file+" not found");
    
    var ret = crypto.cert_info(readFile(files.server_cert_file));

    return {
        issued: ret.notBefore,
        expires: ret.notAfter,
        remainingDays: Math.ceil((ret.notAfter - ret.notBefore) / 86400000 )
    }
}

function iprintf() {
    var i=0, ret;

    if( typeof cb.info != 'function' ) return 0;

    ret=rampart.utils.sprintf.apply(null, arguments);
    cb.info(ret);
    rampart.utils.fflush(rampart.utils.stderr);
}

function tojson(str){
    var r;
    try{
        r = JSON.parse(str);
    } catch(e) {
        eprintf("error parsing json from text = '%s'\n", str);
    }
    return r;
}

function apireq(path, body, auth, privkey, return_with_error)
{
    var ret = curl.fetch(api + "/acme/new-nonce");
    var url, nonce;

    if(ret && ret.headers && ret.headers['Replay-Nonce'])
        nonce = ret.headers['Replay-Nonce'];
    else
        eprintf("error getting nonce from %s\nreply:\n%3J\n", api + "/acme/new-nonce", ret);

    if(/^https/.test(path))
        url = path;
    else
        url = api + path;

    dprintf("fetching %s\n%J\n", url, body?body:"");

    var authkey = "kid";

    if (auth["jwk"])
        authkey="jwk";
    else if (!auth[authkey])
        eprintf("%s:\n   malformed auth '%s'\n", url, auth);

    var hobj = {alg:"RS256"};
    hobj[authkey]=auth[authkey];
    hobj.nonce=nonce;
    hobj.url=url;
    var header = JSON.stringify(hobj);
    var jws_protected = rampart.utils.sprintf('%-0B',header);
    var jws_payload = rampart.utils.sprintf('%-0B',body);
    var jws_signature = rampart.utils.sprintf('%-0B', crypto.rsa_sign(jws_protected + '.' + jws_payload, privkey));
    var jws = JSON.stringify({protected: jws_protected, payload: jws_payload, signature: jws_signature});

    ret = curl.fetch(url, 
        {
            headers: ["Content-Type: application/jose+json"],
            post: jws
        }
    );

    delete ret.body; /* don't need the buffer */

    tprintf("%3J\n",ret);

    if(!return_with_error && (ret.status > 399 || ret.status < 200))
    {
        delete ret.body; /* don't print out the buffer */
        eprintf("error fetching %s\nreply:\n%3J\n", url, ret);
    }
    return ret;
}

var savefile = function(name, contents, private) {
    if(private) {
        try {
            rampart.utils.rmFile(name);
        } catch(e) {}
        rampart.utils.touch(name);
        rampart.utils.chmod(name,"600");
        rampart.utils.fprintf(name, true, '%s', contents);
        rampart.utils.chmod(name,"400");
    }
    else
        rampart.utils.fprintf(name, '%s', contents);
}

function create_key(privfile,pubfile){
    iprintf("creating rsa key, saving to %s%s\n", privfile, pubfile?(" and "+pubfile):'' );

    var key = crypto.rsa_gen_key(4096);

    savefile(privfile, key.private, true);

    if(pubfile)
        savefile(pubfile, key.public);

    return key;
}

function new_acct(email){
    var path = "/acme/new-acct";
    var body = {"termsOfServiceAgreed": true };
    var ret, acct_url;
    if(email) {
        body.contact=["mailto:"+email];
    }

    ret = apireq(path, JSON.stringify(body), auth, acct.private);
    if(ret && ret.headers && ret.headers.Location)
        acct_url=ret.headers.Location;
    else
        eprintf("error parsing Location from:\n%3J\n",ret);

    return acct_url;
}


function le_main(opts)
{

    setopts(opts);

    if(!rampart.utils.stat(files.acct_dir))
    {
        rampart.utils.mkdir(files.acct_dir,"700");
    }

    acct={};

    if(!rampart.utils.stat(files.acct_priv_file)) {
        iprintf("generating the account key...\n");
        acct = create_key(files.acct_priv_file, files.acct_pub_file);
    } else {
        acct.private = rampart.utils.readFile(files.acct_priv_file);
        if(rampart.utils.stat(files.acct_url_file))
            acct.url = rampart.utils.bufferToString( rampart.utils.readFile(files.acct_url_file) );

        if(!rampart.utils.stat(files.acct_pub_file)) {
            acct = crypto.rsa_import_priv_key(acct.private);
            savefile(files.acct_pub_file, acct.public);
        }
        else acct.public = rampart.utils.readFile(files.acct_pub_file);
    }

    var info = crypto.rsa_components(acct.public);

    //dprintf("rsa public key components:\n%3J\n\n", info);
    var e = rampart.utils.sprintf('%-0B',rampart.utils.dehexify(info.exponent));
    var n = rampart.utils.sprintf('%-0B',rampart.utils.dehexify(info.modulus));

    auth = {jwk: {e:e, kty:"RSA",n:n}};
    var tmb = rampart.utils.sprintf('%-0B', crypto.sha256(JSON.stringify(auth.jwk),true));

    //dprintf("e: %s\nn: %s\nauth: %s\nthumbprint: %s\n",e,n,auth,tmb);

    if (!acct.url) {
        acct.url=new_acct(email);
        savefile(files.acct_url_file, acct.url);
    }

    dprintf("Account Location: %s\n", acct.url);

    /* create domain key: */
    var serverkey = create_key(files.server_key_file);

    auth = {kid:acct.url};

    var identifiers = [];

    for (i=0; i<domains.length;i++) {
        identifiers[i]= { "type": "dns", "value": domains[i] };
    }

    body = JSON.stringify({ "identifiers": identifiers });

    var ret = apireq("/acme/new-order", body, auth, acct.private, true);

    /* if we were just staging - need to make a new account */
    if (ret.status == 400 && ret.text.indexOf('KeyID header contained an invalid account') > -1 ) {
        auth = {jwk: {e:e, kty:"RSA",n:n}};
        acct.url=new_acct(email);
        savefile(files.acct_url_file, acct.url);
        auth = {kid:acct.url};
        ret = apireq("/acme/new-order", body, auth, acct.private);
    }
    else if(ret.status > 399 || ret.status < 200)
        eprintf("error fetching %s\nreply:\n%3J\n", url, ret);

    var bret = tojson(ret.text);

    if(!bret.finalize)
        eprintf("error, expecting 'finalize' property in return text:\n%3J\n",bret);

    var finalize = bret.finalize;

    if(!bret.authorizations)
        eprintf("error, expecting 'authorizations' property in return text:\n%3J\n",bret);

    var authorizations = bret.authorizations;
    var order_url = ret.headers.Location;

    var challenges = {};

    /*"challenges": [
          {
             "type": "http-01",
             "status": "pending",
             "url": "https://acme-staging-v02.api.letsencrypt.org/acme/chall-v3/197435019/zj1dHA",
             "token": "_kyJ71YAlr6DztDRyuqIF5dW-3PT4fm8zcdVU5ebVXw"
          },
    */

    iprintf("requesting challenge information from letsencrypt\n");

    for(i=0;i<authorizations.length;i++) {
        var j=0;
        ret = apireq(authorizations[i], "", auth, acct.private);
        bret = tojson(ret.text);

        if(bret.challenges && bret.challenges.length){

            dprintf("challenges:\n%3J\n", bret.challenges);
            for(j=0;j<bret.challenges.length;j++)
            {
                if(bret.challenges[j] && bret.challenges[j].type == "http-01"){
                    iprintf("Got challenge details for %s\n", bret.identifier.value);
                    bret.challenges[j].keyauth = bret.challenges[j].token + '.' + tmb;// + '\n';
                    challenges[bret.identifier.value] = bret.challenges[j];
                    break; 
                }
            }
        } else {
            eprintf("No challenges found in object:\n%3J\n",bret);
        }
    }



    /*
    challenges = 
    {
       "example.com": {
          "type": "http-01",
          "status": "pending",
          "url": "https://acme-staging-v02.api.letsencrypt.org/acme/chall-v3/197435019/zj1dHA",
          "token": "_kyJ71YAlr6DztDRyuqIF5dW-3PT4fm8zcdVU5ebVXw",
          "keyauth": "_kyJ71YAlr6DztDRyuqIF5dW-3PT4fm8zcdVU5ebVXw.EM1KMqbN0rjW0BSfgiBl1QOThbZO8m9kq_evP3zRFrg"
       },
       "www.example.com": {
          "type": "http-01",
          "status": "pending",
          "url": "https://acme-staging-v02.api.letsencrypt.org/acme/chall-v3/197439996/k63Ayg",
          "token": "vGG4FzLz8eOL242yNoONsM6ClncYcwNFgbzfikaGemQ",
          "keyauth": "vGG4FzLz8eOL242yNoONsM6ClncYcwNFgbzfikaGemQ.EM1KMqbN0rjW0BSfgiBl1QOThbZO8m9kq_evP3zRFrg"
       }
    }
    */
    var pid=false;
    if(typeof cb.save_challenges == 'function')
    {
        var k = Object.keys(challenges);
        var j=0;
        for (;j<k.length;j++) {
            challenges[k[j]].path = '/.well-known/acme-challenge/' + challenges[k[j]].token; 
        }
        cb.save_challenges(challenges);
    }
    else {
        function serve_challenges(req) {
            var keys = Object.keys(challenges);
            var i=0;

            for (;i<keys.length;i++) {
                //rampart.utils.printf("%s\n  vs\n%s\n", req.path.path, '/.well-known/acme-challenge/' + challenges[keys[i]].token);
                if (req.path.path == '/.well-known/acme-challenge/' + challenges[keys[i]].token)
                {
                    rampart.utils.printf("url:%s\nreturning %s\n",  
                        '/.well-known/acme-challenge/' + challenges[keys[i]].token,
                        challenges[keys[i]].keyauth
                    );
                    return{bin:challenges[keys[i]].keyauth};
                }
            }
            return {status:404};
        }

        iprintf("starting webserver on port 80\n");

        /* local vars are not copied to web server */
        global.__le__challenges = challenges;
        pid = require("rampart-server").start({
            daemon:true,
            bind: ["[::]:80", "0.0.0.0:80"],
            user: "nobody",
            log:true,
            accessLog: "le-access.log",
            errorLog: "le-error.log",
            map: {
                /* function for webserver to return challenges */
                '/': function(req) 
                    {
                        var challenges = __le__challenges;
                        var keys = Object.keys(challenges);
                        var i=0;

                        for (;i<keys.length;i++) {
                            //rampart.utils.printf("%s\n  vs\n%s\n", req.path.path, '/.well-known/acme-challenge/' + challenges[keys[i]].token);
                            if (req.path.path == '/.well-known/acme-challenge/' + challenges[keys[i]].token)
                            {
                                rampart.utils.printf("url:%s\nreturning %s\n",  
                                    '/.well-known/acme-challenge/' + challenges[keys[i]].token,
                                    challenges[keys[i]].keyauth
                                );
                                return{bin:challenges[keys[i]].keyauth};
                            }
                        }
                        return {status:404};
                    }
            }
        });
    }

    rampart.utils.sleep(1);

    iprintf("signaling for letsencrypt to issue challenges\n");
    var k = Object.keys(challenges);
    for (i=0; i<k.length;i++) {
        var c= challenges[k[i]];
        var ret = apireq(c.url, '{}', auth, acct.private); 
    }

    iprintf("waiting for status update\n");

    var status;

    for (i=0; i<6; i++) {
        var bret;
        rampart.utils.sleep(i * 4);
        iprintf("checking status ...");

        ret = apireq(order_url, "", auth, acct.private);

        bret = tojson(ret.text);

        if(bret.status != "pending"){
            status=bret.status;
            dprintf("%s\n", status);
            break;
        }
        iprintf("%s\n", bret.status);
    }

    if(pid)
        rampart.utils.kill(pid);

    delete global.__le__challenges;

    if(status != 'ready') {
        eprintf("Authentication failed\n");
    }

    iprintf("creating csr\n");

    var csrobj = crypto.gen_csr(serverkey.private, {
        name: maindom,
        subjectAltName: domains
    });

    var csr = rampart.utils.sprintf("%-0B", csrobj.der);
    iprintf("downloading cert\n");

    ret = apireq(finalize, '{ "csr": "' + csr + '" }', auth, acct.private);

    delete ret.body;
    dprintf("%3J\n", ret);

    bret = tojson(ret.text);

    if(!bret.certificate)
        eprintf("error retrieving certificate location");

    ret = apireq(bret.certificate, "", auth, acct.private);

    iprintf("Saving certificate to %s\n", files.server_cert_file);
    rampart.utils.fprintf(files.server_cert_file, '%s', ret.text);


    return {
        success: true,
        serverkey: files.server_key_file,
        servercert: files.server_cert_file,
    }

}

module.exports= { new_cert: le_main, check_cert: check_cert};
