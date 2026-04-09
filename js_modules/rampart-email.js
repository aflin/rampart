/*
    rampart-email.js - Send email via SMTP using rampart-curl

    Requires: rampart-curl, rampart-net

    Usage:

        var email = require("rampart-email");

        var result = email.send({
            from:    "sender@example.com",        // required
            to:      "recipient@example.com",     // required, string or array
            subject: "Hello",                     // optional, defaults to ""
            message: "plain text body",           // string for plain text, or object:
            //  message: {
            //      html: "<p>HTML body</p>",
            //      text: "plain text fallback",
            //      attach: [
            //          { data: "@/path/to/file.pdf", name: "file.pdf", type: "application/pdf" },
            //          { data: bufferVar, name: "pic.jpg", type: "image/jpeg", cid: "mypic" }
            //      ]
            //  },
            cc:         "cc@example.com",         // optional, string or array
            "reply-to": "reply@example.com",      // optional
            date:       new Date(),               // optional, defaults to now

            method: "direct",                     // delivery method (see below)

            // tuning:
            timeout:        30,                   // max total time in seconds (default 30)
            connectTimeout: 10                    // connection timeout in seconds (default 10)
        });

        // result:
        //   { ok: true, sent: 1, failed: 0, results: [ ... ] }
        //
        // Each entry in results:
        //   { domain, rcpt, ok, status, mx, errMsg, sslMode (direct only) }

    Delivery Methods:

        method: "direct"   (default)
            Looks up MX records for each recipient's domain and connects
            directly to the destination mail server on port 25.  Tries
            verified TLS first, then falls back to plain SMTP.
            Recipients at the same domain are batched into one connection.

            Additional options:
                insecure:   true    // also try TLS without cert verification
                                    //   (ssl -> ssl+insecure -> plain)
                requireSsl: true    // do not fall back to plain SMTP
                                    //   (ssl only, or ssl -> ssl+insecure
                                    //    if insecure is also set)

        method: "relay"
            Sends through a local or specified SMTP relay server, which
            handles onward delivery.  Postfix or similar must be running.

            Additional options:
                relay:     "localhost"   // relay hostname (default "localhost")
                relayPort: 25           // relay port (default 25)
                insecure:  true         // skip TLS cert verification

        method: "smtp"
            Sends through any authenticated SMTP server.  You provide the
            full URL including protocol and port.

            Additional options (required):
                smtpUrl: "smtps://smtp.example.com:465"
                user:    "username"
                pass:    "password"
                insecure: true          // skip TLS cert verification

        method: "gmailApp"
            Sends through Gmail's SMTP server using a Google App Password.
            The SMTP URL is handled automatically.

            Additional options (required):
                user: "you@gmail.com"
                pass: "xxxx xxxx xxxx xxxx"   // 16-character app password
                insecure: true                // skip TLS cert verification

            Creating a Gmail App Password:
                1. Go to myaccount.google.com
                2. Click "Security" in the left sidebar
                3. Ensure 2-Step Verification is enabled under
                   "How you sign in to Google"
                4. Search for "App passwords" in the search bar at the top,
                   or go directly to myaccount.google.com/apppasswords
                5. Enter a name (e.g. "rampart-email") and click "Create"
                6. Copy the 16-character password shown

                Note: "App passwords" will not appear if 2-Step Verification
                is not enabled, or if a workspace admin has disabled it.

    Examples:

        // Direct delivery (no account needed, may be rejected by strict servers)
        email.send({
            from: "me@myserver.com", to: "them@example.com",
            subject: "Hello", message: "Hi there"
        });

        // Via local postfix relay
        email.send({
            from: "me@myserver.com", to: "them@example.com",
            subject: "Hello", message: "Hi there",
            method: "relay"
        });

        // Via Gmail with app password
        email.send({
            from: "me@gmail.com", to: "them@example.com",
            subject: "Hello", message: "Hi there",
            method: "gmailApp",
            user: "me@gmail.com", pass: "xxxx xxxx xxxx xxxx"
        });

        // Via any authenticated SMTP server
        email.send({
            from: "me@example.com", to: "them@example.com",
            subject: "Hello", message: "Hi there",
            method: "smtp",
            smtpUrl: "smtps://smtp.example.com:465",
            user: "me@example.com", pass: "mypassword"
        });

        // HTML email with attachment via Gmail
        email.send({
            from: "me@gmail.com", to: ["a@example.com", "b@example.com"],
            subject: "Report", cc: "boss@example.com",
            message: {
                html: "<p>See attached.</p>",
                text: "See attached.",
                attach: [{ data: "@/tmp/report.pdf", name: "report.pdf", type: "application/pdf" }]
            },
            method: "gmailApp",
            user: "me@gmail.com", pass: "xxxx xxxx xxxx xxxx"
        });
*/

rampart.globalize(rampart.utils);

var curl = require("rampart-curl");
var net  = require("rampart-net");

/* ---- helpers ---- */

function extractDomain(addr) {
    var at = addr.indexOf("@");
    if (at < 0) return "";
    return addr.substring(at + 1).toLowerCase();
}

function normalizeList(val) {
    if (!val) return [];
    if (typeof val === "string") return [val];
    if (Array.isArray(val)) return val.slice();
    return [];
}

function resolveMX(domain) {
    var mx;
    try {
        mx = net.resolve(domain, "MX");
    } catch(e) {
        return null;
    }
    if (!mx || mx.errMsg || !mx.records || mx.records.length === 0) {
        // RFC 5321 sec 5.1: no MX records, try domain itself
        return [{priority: 0, exchange: domain}];
    }
    mx.records.sort(function(a, b) { return a.priority - b.priority; });
    return mx.records;
}

function groupByDomain(addrs) {
    var groups = {};
    for (var i = 0; i < addrs.length; i++) {
        var domain = extractDomain(addrs[i]);
        if (!groups[domain]) groups[domain] = [];
        groups[domain].push(addrs[i]);
    }
    return groups;
}

function buildMailMsg(opts, allTo, allCc) {
    var msg = {
        from:    opts.from,
        to:      allTo.join(", "),
        subject: opts.subject || "",
        date:    opts.date || new Date()
    };
    if (allCc.length > 0) msg.cc = allCc.join(", ");
    if (opts["reply-to"]) msg["reply-to"] = opts["reply-to"];
    msg.message = opts.message || "";
    return msg;
}

/* ---- delivery methods ---- */

function trySmtpSend(url, from, rcpt, mailMsg, curlOpts) {
    // Build SSL attempt sequence based on options:
    //   default:              ssl -> plain
    //   insecure:             ssl -> ssl+insecure -> plain
    //   requireSsl:           ssl only
    //   requireSsl+insecure:  ssl -> ssl+insecure
    var attempts = [
        {ssl: true,  insecure: false, label: "ssl"}
    ];
    if (curlOpts.insecure) {
        attempts.push({ssl: true, insecure: true, label: "ssl+insecure"});
    }
    if (!curlOpts.requireSsl) {
        attempts.push({ssl: false, insecure: false, label: "no-ssl"});
    }

    var lastRes;
    for (var i = 0; i < attempts.length; i++) {
        var a = attempts[i];
        var fetchOpts = {
            "mail-from": from,
            "mail-rcpt": rcpt,
            "mail-msg":  mailMsg,
            "connect-timeout": curlOpts.connectTimeout,
            "max-time": curlOpts.timeout
        };
        if (a.ssl) {
            fetchOpts.ssl = true;
            if (a.insecure) fetchOpts.insecure = true;
        }
        lastRes = curl.fetch(url, fetchOpts);
        if (lastRes.status === 250) {
            lastRes._sslMode = a.label;
            return lastRes;
        }
    }
    return lastRes;
}

function sendDirect(from, domainGroups, mailMsg, curlOpts) {
    var results = [];
    for (var domain in domainGroups) {
        var rcpt = domainGroups[domain];
        var mxList = resolveMX(domain);
        if (!mxList) {
            results.push({
                domain: domain, rcpt: rcpt, ok: false,
                status: 0, mx: "", errMsg: "MX lookup failed for " + domain
            });
            continue;
        }
        var sent = false;
        var errors = [];
        for (var i = 0; i < mxList.length; i++) {
            var mxHost = mxList[i].exchange;
            var url = "smtp://" + mxHost + ":25";
            var res = trySmtpSend(url, from, rcpt, mailMsg, curlOpts);
            if (res.status === 250) {
                results.push({
                    domain: domain, rcpt: rcpt, ok: true,
                    status: 250, mx: mxHost, errMsg: "",
                    sslMode: res._sslMode
                });
                sent = true;
                break;
            }
            errors.push(mxHost + " (" + (res.errMsg || "status " + res.status) + ")");
        }
        if (!sent) {
            results.push({
                domain: domain, rcpt: rcpt, ok: false,
                status: 0, mx: "",
                errMsg: "All MX hosts failed: " + errors.join(", ")
            });
        }
    }
    return results;
}

function sendRelay(from, allRcpt, mailMsg, curlOpts) {
    var host = curlOpts.relay || "localhost";
    var port = curlOpts.relayPort || 25;
    var url = "smtp://" + host + ":" + port;
    var fetchOpts = {
        "mail-from": from,
        "mail-rcpt": allRcpt,
        "mail-msg":  mailMsg,
        "connect-timeout": curlOpts.connectTimeout,
        "max-time": curlOpts.timeout
    };
    if (curlOpts.insecure) fetchOpts.insecure = true;
    var res = curl.fetch(url, fetchOpts);
    var ok = (res.status === 250);
    return [{
        domain: "*relay*", rcpt: allRcpt, ok: ok,
        status: res.status || 0, mx: host, errMsg: res.errMsg || ""
    }];
}

function sendSmtp(from, allRcpt, mailMsg, curlOpts) {
    var url = curlOpts.smtpUrl;
    var fetchOpts = {
        "mail-from": from,
        "mail-rcpt": allRcpt,
        "mail-msg":  mailMsg,
        user: curlOpts.user,
        pass: curlOpts.pass,
        "connect-timeout": curlOpts.connectTimeout,
        "max-time": curlOpts.timeout
    };
    if (curlOpts.insecure) fetchOpts.insecure = true;
    var res = curl.fetch(url, fetchOpts);
    var ok = (res.status === 250);
    return [{
        domain: "*smtp*", rcpt: allRcpt, ok: ok,
        status: res.status || 0, mx: url, errMsg: res.errMsg || ""
    }];
}

function sendGmailApp(from, allRcpt, mailMsg, curlOpts) {
    var url = "smtps://smtp.gmail.com:465";
    var fetchOpts = {
        "mail-from": from,
        "mail-rcpt": allRcpt,
        "mail-msg":  mailMsg,
        user: curlOpts.user,
        pass: curlOpts.pass,
        "connect-timeout": curlOpts.connectTimeout,
        "max-time": curlOpts.timeout
    };
    if (curlOpts.insecure) fetchOpts.insecure = true;
    var res = curl.fetch(url, fetchOpts);
    var ok = (res.status === 250);
    return [{
        domain: "*gmailApp*", rcpt: allRcpt, ok: ok,
        status: res.status || 0, mx: url, errMsg: res.errMsg || ""
    }];
}

/* ---- public API ---- */

function send(opts) {
    if (!opts || typeof opts !== "object")
        throw new Error("rampart-email.send: options object required");
    if (!opts.from)
        throw new Error("rampart-email.send: 'from' is required");
    if (!opts.to)
        throw new Error("rampart-email.send: 'to' is required");

    var allTo  = normalizeList(opts.to);
    var allCc  = normalizeList(opts.cc);
    if (allTo.length === 0)
        throw new Error("rampart-email.send: 'to' must not be empty");

    var allRcpt = allTo.concat(allCc);
    var method  = opts.method || "direct";
    var mailMsg = buildMailMsg(opts, allTo, allCc);

    var curlOpts = {
        timeout:        opts.timeout || 30,
        connectTimeout: opts.connectTimeout || 10,
        insecure:       opts.insecure || false,
        requireSsl:     opts.requireSsl || false,
        relay:          opts.relay,
        relayPort:      opts.relayPort,
        smtpUrl:        opts.smtpUrl,
        user:           opts.user,
        pass:           opts.pass
    };

    var results;
    if (method === "direct") {
        var groups = groupByDomain(allRcpt);
        results = sendDirect(opts.from, groups, mailMsg, curlOpts);
    } else if (method === "relay") {
        results = sendRelay(opts.from, allRcpt, mailMsg, curlOpts);
    } else if (method === "gmailApp") {
        if (!curlOpts.user)
            throw new Error("rampart-email.send: 'user' required for method 'gmailApp'");
        if (!curlOpts.pass)
            throw new Error("rampart-email.send: 'pass' required for method 'gmailApp'");
        results = sendGmailApp(opts.from, allRcpt, mailMsg, curlOpts);
    } else if (method === "smtp") {
        if (!curlOpts.smtpUrl)
            throw new Error("rampart-email.send: 'smtpUrl' required for method 'smtp'");
        results = sendSmtp(opts.from, allRcpt, mailMsg, curlOpts);
    } else {
        throw new Error("rampart-email.send: unknown method '" + method + "'");
    }

    var sent = 0, failed = 0;
    for (var i = 0; i < results.length; i++) {
        if (results[i].ok) {
            sent += results[i].rcpt.length;
        } else {
            failed += results[i].rcpt.length;
        }
    }

    return {
        ok:      (failed === 0),
        sent:    sent,
        failed:  failed,
        results: results
    };
}

module.exports = { send: send };
