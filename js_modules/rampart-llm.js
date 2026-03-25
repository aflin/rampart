rampart.globalize(rampart.utils);

var curl = require("rampart-curl.so");

function err(msg) {
    throw new Error("rampart-llm: " + msg);
}

function checkIsRunning(url) {
    var res = curl.fetch(url, {maxTime: 5});
    return res.status == 200;
}

/* ---- SSE parsing helpers ---- */

/* Parse an SSE chunk (possibly containing multiple data: lines)
   into an array of event objects. */
function parseSSEChunk(btxt) {
    var events = [];
    var lines = btxt.split('\n');

    for(var i = 0; i < lines.length; i++) {
        var line = lines[i].trim();
        if(!line) continue;

        if(line.indexOf('data:') === 0)
            line = line.substring(5).trim();

        if(line === '[DONE]') {
            events.push({done: true});
            continue;
        }

        try {
            events.push(JSON.parse(line));
        } catch(e) {
            /* skip unparseable lines (blank, partial, etc.) */
        }
    }

    return events;
}

/* Pull the token string out of an OpenAI-format SSE event.
   Returns {token, thinking} to support --reasoning-format. */
function extractToken(parsed) {
    var choice = (getType(parsed.choices) == 'Array' && parsed.choices.length > 0)
        ? parsed.choices[0] : null;

    if(!choice || choice.finish_reason === "stop")
        return {token: "", thinking: false};

    /* reasoning_content is set by llama-server --reasoning-format deepseek */
    if(choice.delta && choice.delta.reasoning_content)
        return {token: choice.delta.reasoning_content, thinking: true};

    var tok = (choice.delta && choice.delta.content) ? choice.delta.content : "";
    return {token: tok, thinking: false};
}


/* ---- Shared query using OpenAI-compatible /v1/ endpoints ---- */

function query(prompt, callback, finalCallback, ep) {
    var self = this;
    var thinkBuf    = "";
    var thinkingText = "";
    var answerText  = "";
    var hadThinking = false;

    self.thinking = false;

    /* allow (query, callback, endpointString) shorthand */
    if(!ep && getType(finalCallback) == 'String') {
        ep = finalCallback;
        finalCallback = undefined;
    }

    self.cancel = false;

    if(!this.model) {
        if(this._type === 'llamaCpp')
            this.model = "mymod"; /* llama.cpp serves one model; name is irrelevant */
        else
            err("model not set (use instance.model='mymodel' to set)");
    }

    if(callback && typeof callback != 'function')
        err("query - callback must be a function");
    if(finalCallback && typeof finalCallback != 'function')
        err("query - finalCallback must be a function");
    if(!callback && !finalCallback)
        err("query - at least one callback must be provided");

    this.fetchError = false;

    var postObj = {
        model:  this.model,
        stream: true
    };

    var endPoint = ep || "/v1/chat/completions";

    if(typeof prompt == 'string' && prompt.length) {
        postObj.prompt = prompt;
        if(!ep)
            endPoint = '/v1/completions';
    } else if(getType(prompt) == 'Array') {
        postObj.messages = prompt;
    }

    if(this.params)
        Object.assign(postObj, this.params);

    function chunkcb(content) {
        if(callback)
            return callback(content);
        if(content.error)
            fprintf(stderr, 'error and no callback in rampart-llm.js: "%J"\n', content);
    }

    /*  Emit a token through the <think>-tag filter.
        Buffers partial tags so a split like "<thi" + "nk>" is handled correctly.  */
    function emitToken(token, serverResponse) {
        thinkBuf += token;

        while(thinkBuf.length > 0) {
            var tag    = self.thinking ? "</think>" : "<think>";
            var tagLen = tag.length;
            var idx    = thinkBuf.indexOf(tag);

            if(idx !== -1) {
                /* complete tag found */
                var before = thinkBuf.substring(0, idx);
                thinkBuf = thinkBuf.substring(idx + tagLen);

                if(before.length) {
                    if(self.thinking)
                        thinkingText += before;
                    else if(hadThinking)
                        answerText += before;

                    var ret = chunkcb({
                        thinking:       self.thinking,
                        token:          before,
                        serverResponse: serverResponse
                    });
                    if(ret === false) return false;
                }

                self.thinking = !self.thinking;
                if(self.thinking) hadThinking = true;
                continue;
            }

            /* check whether the tail of the buffer could be the start of the tag */
            var partialAt = -1;
            var minCheck  = Math.min(thinkBuf.length, tagLen - 1);
            for(var n = minCheck; n > 0; n--) {
                if(tag.substring(0, n) === thinkBuf.substring(thinkBuf.length - n)) {
                    partialAt = thinkBuf.length - n;
                    break;
                }
            }

            if(partialAt > 0) {
                /* emit the safe portion before the potential partial tag */
                var safe = thinkBuf.substring(0, partialAt);
                thinkBuf = thinkBuf.substring(partialAt);

                if(self.thinking)
                    thinkingText += safe;
                else if(hadThinking)
                    answerText += safe;

                var ret = chunkcb({
                    thinking:       self.thinking,
                    token:          safe,
                    serverResponse: serverResponse
                });
                if(ret === false) return false;
            } else if(partialAt === 0) {
                /* entire buffer is a potential partial tag – wait for more data */
                break;
            } else {
                /* no partial match – emit everything */
                if(self.thinking)
                    thinkingText += thinkBuf;
                else if(hadThinking)
                    answerText += thinkBuf;

                var ret = chunkcb({
                    thinking:       self.thinking,
                    token:          thinkBuf,
                    serverResponse: serverResponse
                });
                thinkBuf = "";
                if(ret === false) return false;
            }

            break;
        }
    }

    self.tokens = [];

    curl.fetchAsync(this.urlbase + endPoint,
        {
            connectTimeout: 10,
            postJSON: postObj,

            chunkCallback: function(r) {
                if(self.cancel === true) {
                    self.cancel = false;
                    return curl.cancel;
                }

                var btxt   = sprintf('%s', r.body);
                var events = parseSSEChunk(btxt);

                for(var i = 0; i < events.length; i++) {
                    var ev = events[i];

                    if(ev.done) return;  /* [DONE] – final callback handles the rest */

                    if(ev.error) {
                        self.fetchError = ev.error;
                        chunkcb({error: ev.error, serverResponse: r, token: ""});
                        return false;
                    }

                    var result = extractToken(ev);
                    if(!result.token.length) continue;

                    if(result.thinking) {
                        /* reasoning_content from API — already classified,
                           bypass <think> tag parser and emit directly */
                        thinkingText += result.token;
                        hadThinking = true;
                        self.thinking = true;
                        var ret = chunkcb({
                            thinking: true,
                            token: result.token,
                            serverResponse: r
                        });
                        if(ret === false) {
                            self.cancel = false;
                            return curl.cancel;
                        }
                    } else {
                        /* reset thinking state so emitToken doesn't
                           treat answer tokens as thinking content */
                        if(self.thinking && hadThinking)
                            self.thinking = false;

                        if(finalCallback)
                            self.tokens.push(result.token);

                        var ret = emitToken(result.token, r);
                        if(ret === false) {
                            self.cancel = false;
                            return curl.cancel;
                        }
                    }
                }
            }
        },

        /* final callback – runs after the stream ends */
        function(r) {
            /* mid-stream SSE error already handled in chunkCallback */
            if(self.fetchError) {
                chunkcb({done: true, token: '', serverResponse: r});
                if(finalCallback)
                    finalCallback({serverResponse: r, fullText: '', error: self.fetchError});
                return;
            }

            /* check for HTTP error (e.g. 400 context exceeded) */
            if(!r.status || r.status != 200) {
                var errInfo = {error: (r.status || 0) + " error", serverResponse: r};
                try {
                    var parsed = JSON.parse(sprintf('%s', r.body));
                    if(parsed.error) errInfo.error = parsed.error;
                } catch(e) {}
                chunkcb({done: true, token: '', serverResponse: r, error: errInfo.error});
                if(finalCallback)
                    finalCallback({serverResponse: r, fullText: '', error: errInfo.error});
                return;
            }

            /* flush any remaining buffered text */
            if(thinkBuf.length) {
                if(self.thinking)
                    thinkingText += thinkBuf;
                else {
                    if(hadThinking) answerText += thinkBuf;
                    chunkcb({thinking: self.thinking, token: thinkBuf, serverResponse: r});
                }
                thinkBuf = "";
            }

            /* signal done to per-token callback */
            chunkcb({done: true, token: '', serverResponse: ''});

            if(finalCallback) {
                var resp = {serverResponse: r, fullText: self.tokens.join('')};
                self.tokens = undefined;

                if(thinkingText) {
                    resp.thinkingText = thinkingText;
                    resp.answer       = answerText;
                }
                finalCallback(resp);
            }
        }
    );
}


/* ---- Constructors ---- */

function initBackend(self, defaults, opts) {
    Object.assign(self, defaults, opts);

    if(typeof self.server != 'string')
        err("option 'server' must be a string (host name or ip)");
    if(typeof self.port != 'number')
        err("option 'port' must be a number");

    self.urlbase = sprintf("http://%s:%d", self.server, self.port);
}

function ollama(opts) {
    initBackend(this, {server: '127.0.0.1', port: 11434, query: query}, opts || {});
    this._type = 'ollama';

    if(!checkIsRunning(this.urlbase + '/'))
        err(sprintf("ollama server at %s doesn't appear to be running", this.urlbase));
}

function llamaCpp(opts) {
    initBackend(this, {server: '127.0.0.1', port: 8080, query: query}, opts || {});
    this._type = 'llamaCpp';

    if(!checkIsRunning(this.urlbase + '/'))
        err(sprintf("llama.cpp server at %s doesn't appear to be running", this.urlbase));
}

module.exports = {
    ollama:   ollama,
    llamaCpp: llamaCpp
};
