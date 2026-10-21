/*  llmchat-tools.js — the tools llmchat.js offers the model.

    Tools are declared in the OpenAI function-calling format
    ({type:"function", function:{name, description, parameters}}) that
    llama-server, vLLM, Ollama and the OpenAI API all accept.  The model
    answers with tool_calls; llmchat.js runs each one through run() below
    and returns the result as a {role:"tool", tool_call_id, content}
    message.  Results are JSON strings.

      web_search        DuckDuckGo (results-page scraping, as ddgs does),
                        then Exa's keyless MCP search, then Brave (scraping),
                        then Firecrawl's keyless search API
      web_fetch         a web page's or document's text, by character range;
                        read through Exa when our own download is refused

    Positions are character offsets into the page text, so a long page is
    read in parts by continuing from the end of the last part.
*/

var curl = require("rampart-curl");
var html = require("rampart-html");
var net  = require("rampart-net");
var totext = require("rampart-totext");     /* documents (PDF, Word, ...) for web_fetch */
var u    = rampart.utils;

var MAX_READ = 20000;      // most characters one read/fetch returns
var DEF_READ = 8000;

var UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 " +
         "(KHTML, like Gecko) Chrome/124.0 Safari/537.36";

function clampInt(v, def, min, max) {
    var n = parseInt(v, 10);
    if (isNaN(n)) return def;
    return n < min ? min : (n > max ? max : n);
}

function squash(s) {
    return String(s || "").replace(/\s+/g, " ").trim();
}

/* ---------------- tool declarations ---------------- */

/* the tools offered: the web pair when cfg.web is set */
function definitions(cfg) {
    var defs = [];
    if (cfg.web) defs.push(
    { type: "function", function: {
        name: "web_search",
        description:
            "Search the web for current or non-encyclopedic information. Returns " +
            "titles, URLs and short snippets; use web_fetch to read a page.",
        parameters: { type: "object", properties: {
            query:       { type: "string",  description: "Search query." },
            max_results: { type: "integer", description: "Results to return, 1-10 (default 5)." }
        }, required: ["query"] }
    }},
    { type: "function", function: {
        name: "web_fetch",
        description:
            "Fetch a web page or document (PDF, Word, PowerPoint, Excel, OpenDocument, " +
            "EPUB, RTF) and return its readable text; PDF pages are marked [page N]. " +
            "Returns up to `length` " +
            "characters from `start`, the range returned, and the page's total " +
            "length; continue from the returned end to read more.",
        parameters: { type: "object", properties: {
            url:    { type: "string",  description: "http or https URL." },
            start:  { type: "integer", description: "Character offset to start at (default 0)." },
            length: { type: "integer", description: "Characters to return (default " + DEF_READ + ", max " + MAX_READ + ")." }
        }, required: ["url"] }
    }});
    return defs;
}

/* ---------------- web search ---------------- */

/* real target of a results-page link (engines wrap them in redirects) */
function unwrapDDG(href) {
    var m = String(href || "").match(/[?&]uddg=([^&]+)/);
    href = m ? decodeURIComponent(m[1]) : href;
    return /^\/\//.test(href) ? "https:" + href : href;
}

/* ---------------- Exa (its hosted MCP server, keyless) ---------------- */
/* One JSON-RPC tools/call per request; the reply is a single server-sent
   event ("data: {...}").  Keyless use is limited per IP address (about 2 a
   second, 50-150 a day) and a 429 does not say which limit was hit, so a
   429 is retried once after a second (the per-second limit); if that also
   fails, this server thread leaves Exa alone for EXA_PAUSES[n] -- longer
   each time in a row, as the daily limit is the likelier cause by then. */
var EXA_URL = "https://mcp.exa.ai/mcp";
var EXA_PAUSES = [5 * 60 * 1000, 30 * 60 * 1000];
var exaPausedUntil = 0, exaStrikes = 0;

function exaCall(tool, args, maxTime) {
    if (Date.now() < exaPausedUntil) return { error: "paused after reaching its free rate limit" };
    var r, tries = 0;
    while (true) {
        try {
            r = curl.fetch(EXA_URL, {
                postJSON: { jsonrpc: "2.0", id: 1, method: "tools/call",
                            params: { name: tool, arguments: args } },
                headers: ["Accept: application/json, text/event-stream", "User-Agent: " + UA],
                "max-time": maxTime || 30 });
        } catch (e) { return { error: e.message }; }
        if (r.status != 429 || tries++) break;
        u.sleep(1.2);
    }
    if (r.status == 429) {
        exaPausedUntil = Date.now() + EXA_PAUSES[Math.min(exaStrikes, EXA_PAUSES.length - 1)];
        exaStrikes++;
        return { error: "free rate limit reached" };
    }
    exaStrikes = 0;
    if (r.status != 200) return { error: "HTTP " + (r.status || 0) };
    var m = String(r.text).match(/^data: (.*)$/m), d;
    try { d = JSON.parse(m ? m[1] : r.text); } catch (e) { return { error: "unreadable reply" }; }
    if (d.error) return { error: String(d.error.message || "error") };
    var text = ((d.result && d.result.content) || []).map(function (c) { return c.text || ""; }).join("\n");
    if (d.result && d.result.isError) return { error: text.replace(/^Error fetching URL\(s\):\s*/, "").slice(0, 200) };
    return { text: text };
}

/* a page as Exa reads it: "# Title" / "URL: ..." lines, then the text */
function exaPage(url) {
    var x = exaCall("web_fetch_exa", { urls: [url], maxCharacters: 200000 }, 60);
    if (x.error) return x;
    var t = x.text, title = "", m = t.match(/^# (.*)\n/);
    if (m) { title = m[1]; t = t.slice(m[0].length); }
    t = t.replace(/^URL: .*\n/, "").replace(/\n{3,}/g, "\n\n").trim();
    if (!t) return { error: "no text" };
    return { url: url, title: squash(title), text: t, via: "exa" };
}

/* each engine, in the order tried: request + parse of its results
   (a no-javascript results page, or JSON for `json' engines) */
var ENGINES = [
    { name: "duckduckgo",
      fetch: function (q) {
          return curl.fetch("https://html.duckduckgo.com/html/", {
              post: "q=" + encodeURIComponent(q) + "&b=&kl=us-en",
              headers: ["User-Agent: " + UA, "Referer: https://html.duckduckgo.com/"],
              "max-time": 12 });
      },
      blocked: function (r) { return r.status == 202 || /anomaly-modal/.test(r.text); },
      parse: function (doc) {
          var out = [], items = doc.findClass("result");
          for (var i = 0; i < items.length; i++) {
              var el = items.eq(i);
              if (el.hasClass("result--ad")[0]) continue;
              var a = el.findClass("result__a");
              if (!a.length) continue;
              out.push({ title: a.toText()[0], url: unwrapDDG(a.getAttr("href")[0]),
                         snippet: el.findClass("result__snippet").toText()[0] });
          }
          return out;
      } },
    { name: "exa",
      search: function (q, n) {
          var x = exaCall("web_search_exa", { query: q, numResults: n }, 30);
          if (x.error) return x;
          return { results: x.text.split(/\n---\n/).map(function (b) {
              var t = b.match(/^Title: (.*)$/m), l = b.match(/^URL: (.*)$/m);
              var h = b.split(/^Highlights:\s*$/m)[1] || "";
              return { title: t ? t[1] : "", url: l ? l[1].trim() : "", snippet: squash(h).slice(0, 600) };
          }) };
      } },
    { name: "brave",
      fetch: function (q) {
          return curl.fetch("https://search.brave.com/search?source=web&q=" + encodeURIComponent(q), {
              headers: ["User-Agent: " + UA, "Accept-Language: en-US,en;q=0.9"],
              "max-time": 12 });
      },
      blocked: function (r) { return r.status != 200; },
      parse: function (doc) {
          var out = [], items = doc.findClass("snippet");
          for (var i = 0; i < items.length; i++) {
              var el = items.eq(i);
              var a = el.findTag("a"), t = el.findClass("title");
              var href = a.length ? a.getAttr("href")[0] : "";
              if (!t.length || !/^https?:/.test(href || "")) continue;
              var s = el.findClass("content");
              if (!s.length) s = el.findClass("snippet-description");
              out.push({ title: t.toText()[0], url: href, snippet: s.toText()[0] });
          }
          return out;
      } },
    /* Firecrawl's search API: keyless use is allowed, limited per IP
       address (about 1,000 credits a month; a search costs 2) */
    { name: "firecrawl", json: true,
      fetch: function (q) {
          return curl.fetch("https://api.firecrawl.dev/v2/search", {
              postJSON: { query: q, limit: 10 },
              headers: ["User-Agent: " + UA],
              "max-time": 20 });
      },
      blocked: function (r) { return r.status != 200; },
      parse: function (d) {
          return ((d && d.success && d.data && d.data.web) || []).map(function (x) {
              return { title: x.title, url: x.url, snippet: x.description };
          });
      } }
];

/* one search request per engine at most every MIN_GAP ms (per server thread) */
var MIN_GAP = 2000;
var lastHit = {};

function webSearch(cfg, args, out) {
    var q = squash(args.query);
    if (!q) return { error: "query is required" };
    var n = clampInt(args.max_results, 5, 1, 10);
    var tried = [];
    for (var i = 0; i < ENGINES.length; i++) {
        var eng = ENGINES[i];
        var wait = (lastHit[eng.name] || 0) + MIN_GAP - Date.now();
        if (wait > 0) u.sleep(wait / 1000);
        lastHit[eng.name] = Date.now();
        var r, found = [];
        if (eng.search) {           /* an API engine that does its own request and parsing */
            var sr = eng.search(q, n);
            if (sr.error) { tried.push(eng.name + ": " + sr.error); continue; }
            found = sr.results;
        } else {
            try { r = eng.fetch(q); } catch (e) { tried.push(eng.name + ": " + e.message); continue; }
            if (!r || eng.blocked(r)) { tried.push(eng.name + ": refused (HTTP " + (r ? r.status : 0) + ")"); continue; }
            try {
                if (eng.json)
                    found = eng.parse(JSON.parse(r.text));
                else {
                    var doc = html.newDocument(r.text);
                    found = eng.parse(doc);
                    doc.destroy();
                }
            } catch (e) { tried.push(eng.name + ": unreadable results"); continue; }
        }
        found = found.filter(function (x) { return x.url && /^https?:\/\//.test(x.url); })
                     .slice(0, n)
                     .map(function (x) { return { title: squash(x.title), url: x.url, snippet: squash(x.snippet) }; });
        if (!found.length) { tried.push(eng.name + ": no results"); continue; }
        /* search results are candidate sources: cited only if the answer names them */
        found.forEach(function (x) { out.sources.push({ title: x.title, link: x.url, kind: "search" }); });
        var res = { query: q, engine: eng.name, results: found };
        if (tried.length) res.skipped = tried;      /* engines that did not answer, and why */
        return res;
    }
    return { query: q, error: "web search failed", engines: tried };
}

/* ---------------- web fetch ---------------- */

/* addresses a fetched URL may not reach: loopback, private, link-local,
   CGNAT, multicast/reserved, and their IPv6 counterparts */
function privateAddr(ip) {
    ip = String(ip).toLowerCase();
    var m4 = ip.match(/^(?:::ffff:)?(\d+)\.(\d+)\.(\d+)\.(\d+)$/);
    if (m4) {
        var a = +m4[1], b = +m4[2];
        return a == 0 || a == 10 || a == 127 || a >= 224 ||
               (a == 100 && b >= 64 && b < 128) || (a == 169 && b == 254) ||
               (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168);
    }
    return ip == "::" || ip == "::1" || /^f[cd]/.test(ip) || /^fe[89ab]/.test(ip) || /^ff/.test(ip);
}

/* vet the URL's host and return curl options pinning the connection to the
   vetted address (so a second DNS answer can't point it elsewhere) */
function vetUrl(url) {
    var m = String(url).match(/^(https?):\/\/(?:[^\/@]*@)?(\[[^\]]+\]|[^\/:?#]+)(?::(\d+))?/i);
    if (!m) return { error: "only http and https URLs can be fetched" };
    var host = m[2].replace(/^\[|\]$/g, ""), port = m[3] || (m[1].toLowerCase() == "https" ? "443" : "80");
    if (/^localhost$|\.local$|\.internal$|\.lan$/i.test(host)) return { error: "that address is not reachable from here" };
    var ip = host;
    if (!/^[\d.]+$/.test(host) && host.indexOf(":") < 0) {
        var res;
        try { res = net.resolve(host); } catch (e) { res = null; }
        if (!res || !res.ipaddrs || !res.ipaddrs.length) return { error: "could not resolve " + host };
        for (var i = 0; i < res.ipaddrs.length; i++)
            if (privateAddr(res.ipaddrs[i])) return { error: "that address is not reachable from here" };
        ip = res.ipv4 || res.ip;
    } else if (privateAddr(host))
        return { error: "that address is not reachable from here" };
    var to = ip.indexOf(":") >= 0 ? "[" + ip + "]" : ip;
    return { opts: { "connect-to": host + ":" + port + ":" + to + ":" + port } };
}

/* readable text of an HTML page: <main>/<article> if present, minus scripts and chrome */
function pageText(body) {
    var doc = html.newDocument(body);
    var title = doc.findTag("title").toText()[0] || "";
    ["script", "style", "noscript", "svg", "nav", "header", "footer", "aside", "form", "iframe"]
        .forEach(function (t) { doc.findTag(t).delete(); });
    var root = doc.findTag("main");
    if (!root.length) root = doc.findTag("article");
    if (!root.length) root = doc.findTag("body");
    var text = root.toText({ concatenate: true, enumerateLists: true }) || "";
    if (typeof text != "string") text = text.join("\n");
    doc.destroy();
    text = text.replace(/[ \t]+\n/g, "\n").replace(/\n{3,}/g, "\n\n").trim();
    return { title: squash(title), text: text };
}

/* web_fetch: our own download first; if that fails (a site that blocks us,
   an error, a timeout) the page is read through Exa, which fetches from its
   own servers.  Addresses vetUrl refuses never go to Exa. */
function getPage(url) {
    var p = getPageDirect(url);
    if (!p.error || p.noFallback) return p;
    var x = exaPage(p.url || url);
    if (x.error) return { error: p.error + " (also tried Exa: " + x.error + ")" };
    return x;
}

function getPageDirect(url) {
    var opts, hop = 0;
    while (true) {
        var v = vetUrl(url);
        if (v.error) { v.noFallback = true; return v; }
        opts = v.opts;
        opts["max-time"] = 20;
        opts["max-filesize"] = 20000000;
        opts.headers = ["User-Agent: " + UA, "Accept-Language: en-US,en;q=0.9"];
        var r;
        try { r = curl.fetch(url, opts); }
        catch (e) { return { url: url, error: e.message }; }
        if (r.status >= 300 && r.status < 400 && r.headers && (r.headers.Location || r.headers.location)) {
            if (++hop > 5) return { url: url, error: "too many redirects" };
            var loc = r.headers.Location || r.headers.location;
            url = /^https?:/i.test(loc) ? loc :
                  (/^\//.test(loc) ? url.match(/^https?:\/\/[^\/]+/i)[0] + loc : url.replace(/[^\/]*$/, "") + loc);
            continue;
        }
        if (r.status != 200) {
            var h = r.headers || {};
            if ((r.status == 403 || r.status == 429 || r.status == 503) &&
                (h["cf-mitigated"] || h["Cf-Mitigated"] || /challenge|captcha/i.test(String(r.text).slice(0, 4000))))
                return { url: url, error: "HTTP " + r.status + ": the site blocks automated access" };
            return { url: url, error: "HTTP " + (r.status || 0) + (r.errMsg ? " " + r.errMsg : "") };
        }
        var ctype = String((r.headers && (r.headers["Content-Type"] || r.headers["content-type"])) || "");
        var mime = ctype.split(";")[0].trim().toLowerCase();
        /* what the bytes are decides first (a server's type can be wrong, and
           "xml" appears in the Office types), then the declared type */
        var kind = totext.identify(r.body);
        if (!DOC_KINDS.test(kind)) {
            if (kind == "html" || /^text\/html$|xhtml|^text\/xml$|^application\/xml$|\+xml$/.test(mime) ||
                (!mime && /^\s*</.test(r.text.slice(0, 200)))) {
                var p = pageText(r.text);
                return { url: url, title: p.title, text: p.text };
            }
            if (/^text\/|json/.test(mime)) return { url: url, title: "", text: r.text };
        }
        /* PDF, Word, PowerPoint, Excel, OpenDocument, EPUB, RTF, ... */
        var d = docText(r.body, ctype);
        if (d.error) { d.url = url; return d; }
        d.url = url;
        if (!d.title)       /* no title in the document: its file name */
            d.title = decodeURIComponent(url.replace(/[?#].*$/, "").replace(/\/+$/, "").replace(/^.*\//, "")) || url;
        return d;
    }
}

/* rampart-totext identify() names of the formats handed to it rather than
   read as a web page or plain text */
var DOC_KINDS = /^(pdf|doc|docx|pptx|xlsx|odt|odp|ods|epub|rtf|latex|man|email|mbox|mhtml|png|jpeg|tiff|gif|bmp|pnm|psd|hdr)$/;

/* text of a downloaded document via rampart-totext; PDF pages are marked
   [page N] so passages can be cited by page */
function docText(body, ctype) {
    var d;
    try { d = totext.convert(body, { details: true }); }
    catch (e) {
        if (/OCR reader/.test(e.message))       /* images / scanned-only PDFs */
            return { noFallback: true,
                     error: (/^image\//i.test(ctype) ? "this is an image" :
                             "this " + (ctype || "file") + " holds only scanned images") +
                            ", and reading text from images is not enabled here" };
        return { error: "could not extract text from this " + (ctype || "file") + ": " + e.message };
    }
    var text = String(d.text || "");
    if (text.indexOf("\f") >= 0)
        text = text.split("\f").map(function (p, i) { return { n: i + 1, t: p.trim() }; })
                   .filter(function (p) { return p.t.length; })
                   .map(function (p) { return "[page " + p.n + "]\n" + p.t; }).join("\n\n");
    text = text.replace(/[ \t]+\n/g, "\n").replace(/\n{3,}/g, "\n\n").trim();
    if (!text)
        return { error: "no text found in this " + (d.mimeType || ctype || "file") +
                        " (it may contain only scanned images)" };
    return { title: squash(d.title || ""), text: text, type: d.mimeType };
}

function webFetch(cfg, args, out, cache) {
    var url = String(args.url || "").trim();
    if (!url) return { error: "url is required" };
    if (/^[a-z][a-z0-9+.\-]*:/i.test(url) && !/^https?:\/\//i.test(url))
        return { url: url, error: "only http and https URLs can be fetched" };
    if (!/^https?:\/\//i.test(url)) url = "https://" + url;
    var start = clampInt(args.start, 0, 0, 2000000000);
    var len   = clampInt(args.length, DEF_READ, 1, MAX_READ);
    // pages stay cached for the connection, so reading on from `end' is free
    var page = cache[url];
    if (!page) {
        page = getPage(url);
        if (page.error) return { url: url, error: page.error };
        cache[url] = page;
        if (page.url != url) cache[page.url] = page;
    }
    if (start > page.text.length) start = page.text.length;
    var text = page.text.substr(start, len);
    out.sources.push({ title: page.title || page.url, link: page.url });
    return { url: page.url, title: page.title, type: page.type, via: page.via, length: page.text.length,
             start: start, end: start + text.length,
             more: start + text.length < page.text.length, text: text };
}

/* ---------------- dispatch ---------------- */

var RUN = { web_search: webSearch, web_fetch: webFetch };

/* a one-line status for the chat window while a tool runs */
function describe(name, args) {
    switch (name) {
        case "web_search":       return "searching the web: " + squash(args.query);
        case "web_fetch":        return "reading " + String(args.url || "");
    }
    return "running " + name;
}

/* a few readable lines describing a tool result, for the chat window */
function summarize(name, res) {
    if (!res) return "";
    if (res.error) return "Error: " + res.error +
        (res.engines ? "\n" + res.engines.join("\n") : "");
    function n(x) { return u.sprintf("%'d", x || 0); }
    switch (name) {
        case "web_fetch":
            return (res.title || res.url) + ": characters " + n(res.start) + "–" + n(res.end) +
                   " of " + n(res.length) + (res.more ? " (more follows)" : "") +
                   "\n" + res.url + (res.via ? " (read through " + res.via + ")" : "");
        case "web_search":
            return res.results.length + " result" + (res.results.length == 1 ? "" : "s") +
                   " from " + res.engine +
                   (res.skipped ? " (" + res.skipped.join("; ") + ")" : "") + ":\n" +
                   res.results.map(function (r) { return "• " + r.title + "\n  " + r.url; }).join("\n");
    }
    return "";
}

/* run one tool call.  Returns {content: <JSON string for the tool message>,
   sources: [{title, link}]}.  Never throws: errors go back to the model. */
function run(cfg, call, cache) {
    var name = call.function && call.function.name;
    var out = { sources: [] }, res, args;
    try { args = JSON.parse(call.function.arguments || "{}"); }
    catch (e) { args = null; }
    if (!args || typeof args != "object")
        res = { error: "arguments were not a JSON object" };
    else if (!RUN[name] || (!cfg.web && /^web_/.test(name)))
        res = { error: "unknown tool " + name };
    else {
        try { res = RUN[name](cfg, args, out, cache || {}); }
        catch (e) { res = { error: name + " failed: " + e.message }; }
    }
    return { content: JSON.stringify(res), sources: out.sources, args: args || {},
             summary: summarize(name, res) };
}

module.exports = { definitions: definitions, run: run, describe: describe,
                   _engines: ENGINES,       /* for tests */
                   _exa: function (url, keepStrikes) { EXA_URL = url; exaPausedUntil = 0;
                                                     if (!keepStrikes) exaStrikes = 0; },
                   _exaState: function () { return { pausedFor: exaPausedUntil - Date.now(), strikes: exaStrikes }; } };
