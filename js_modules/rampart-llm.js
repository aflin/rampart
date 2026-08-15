rampart.globalize(rampart.utils);

var curl = require("rampart-curl.so");

/* Optional debug logging: set RAMPART_LLM_DEBUG=1 to dump raw SSE chunks,
   request bodies, and tool-call accumulator state to a file. Defaults to
   /tmp/rampart-llm-debug.log; override with RAMPART_LLM_DEBUG_FILE. */
var RLD_ENABLED = false;
var RLD_FH      = null;
(function initRld() {
    var flag = process.env.RAMPART_LLM_DEBUG;
    if (!flag || flag === '0') return;
    var path = process.env.RAMPART_LLM_DEBUG_FILE || '/tmp/rampart-llm-debug.log';
    try {
        RLD_FH      = rampart.utils.fopen(path, 'a');
        RLD_ENABLED = true;
    } catch(e) {
        fprintf(stderr, "rampart-llm: debug-log init failed: %s\n", e.message);
    }
})();

function rld(tag, payload) {
    if (!RLD_ENABLED) return;
    var ts = (new Date()).toISOString();
    var body = (typeof payload === 'string')
        ? payload
        : sprintf('%J', payload);
    rampart.utils.fprintf(RLD_FH, "[%s] %s %s\n", ts, tag, body);
}

function err(msg) {
    throw new Error("rampart-llm: " + msg);
}

/* Cycle / phrase-repetition detector for streaming deltas.
   Fires when the recent stream is periodic with period p for at least
   max(10, ceil(100/p)) full cycles. O(maxPeriod) per token.
   Catches model degeneration inside tool-call args or free text where
   the model loops on a short token sequence and never emits EOS. */
function makeCycleDetector(opts) {
    opts = opts || {};
    var maxPeriod = opts.maxPeriod || 20;
    var buf       = [];
    var match     = new Array(maxPeriod + 1);
    for (var _i = 0; _i <= maxPeriod; _i++) match[_i] = 0;

    function threshold(p) {
        var t = Math.ceil(100 / p);
        return t < 10 ? 10 : t;
    }

    return function push(tok) {
        buf.push(tok);
        if (buf.length > maxPeriod * 2) buf.shift();
        var i = buf.length - 1;
        for (var p = 1; p <= maxPeriod && i - p >= 0; p++) {
            if (buf[i] === buf[i - p]) {
                match[p]++;
                if (match[p] >= p * threshold(p))
                    return { period: p,
                             cycles: threshold(p),
                             sample: buf.slice(i - p + 1, i + 1).join('|') };
            } else {
                match[p] = 0;
            }
        }
        return null;
    };
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
   Returns {token, thinking, toolCallsDelta, finishReason}.
   - token / thinking: as before
   - toolCallsDelta: array of partial tool_call objects from this chunk
     (OpenAI streams tool_calls.function.arguments as JSON-string fragments
     across multiple chunks; caller must accumulate by `index` field).
   - finishReason: passed through so caller knows when stream ended for
     tool-call vs content vs stop.
*/
function extractToken(parsed) {
    var choice = (getType(parsed.choices) == 'Array' && parsed.choices.length > 0)
        ? parsed.choices[0] : null;

    if(!choice)
        return {token: "", thinking: false};

    var fr = choice.finish_reason || null;

    if(fr === "stop")
        return {token: "", thinking: false, finishReason: fr};

    /* tool_calls deltas — OpenAI streams these in chunks */
    if(choice.delta && getType(choice.delta.tool_calls) == 'Array')
        return {token: "", thinking: false,
                toolCallsDelta: choice.delta.tool_calls,
                finishReason: fr};

    /* reasoning_content is set by llama-server --reasoning-format deepseek */
    if(choice.delta && choice.delta.reasoning_content)
        return {token: choice.delta.reasoning_content, thinking: true,
                finishReason: fr};

    var tok = (choice.delta && choice.delta.content) ? choice.delta.content : "";
    return {token: tok, thinking: false, finishReason: fr};
}

/* Merge a streamed tool_call delta into an accumulator array.
   OpenAI streams partial fields keyed by `index`; this builds up the
   final {id, type, function: {name, arguments}} objects. */
function mergeToolCallDelta(acc, deltas) {
    for(var i = 0; i < deltas.length; i++) {
        var d = deltas[i];
        var idx = (d.index != null) ? d.index : 0;
        if(!acc[idx]) acc[idx] = {id: '', type: 'function',
                                   function: {name: '', arguments: ''}};
        if(d.id)   acc[idx].id   = d.id;
        if(d.type) acc[idx].type = d.type;
        if(d.function) {
            if(d.function.name)
                acc[idx].function.name = (acc[idx].function.name || '') + d.function.name;
            if(typeof d.function.arguments === 'string')
                acc[idx].function.arguments = (acc[idx].function.arguments || '') + d.function.arguments;
        }
    }
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
        stream: true,
        /* Ask the server to include token-usage in the final SSE chunk.
         * Standard OpenAI flag; honored by llama-server, vllm, recent
         * ollama. Servers that don't recognize it ignore it silently.
         * Required for state.llm.usage to populate; without it, the
         * final chunk has no usage field. */
        stream_options: { include_usage: true }
    };

    /* apiBase already includes the /v1 (or equivalent) prefix; endpoint
     * here is just the leaf. Legacy llamaCpp/ollama still work because
     * initBackend sets apiBase = urlbase + "/v1". */
    var endPoint = ep || "/chat/completions";

    if(typeof prompt == 'string' && prompt.length) {
        postObj.prompt = prompt;
        if(!ep)
            endPoint = '/completions';
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
    self.toolCallsAcc = [];        /* accumulator for streamed tool_calls */
    /* Track finish_reason from the last chunk so caller can detect
     * truncation. OpenAI/llamacpp SSE: only the last delta before [DONE]
     * has finish_reason set. We capture the most recent one. */
    var lastFinishReason = null;
    /* Per-call usage. Reset at the start of every query so a stale value
     * from a prior call never gets re-reported. Populated from the final
     * SSE chunk's `usage` block when the server honors include_usage. */
    self.usage = null;

    rld("REQUEST", {endpoint: endPoint, model: postObj.model,
        nmsg: (postObj.messages || []).length,
        params: Object.keys(postObj).filter(function(k){
            return k !== 'messages' && k !== 'tools';
        }).reduce(function(o,k){o[k]=postObj[k];return o;},{}),
        ntools: (postObj.tools || []).length});

    /* Fresh cycle detector per request. */
    var cycleDetect = makeCycleDetector({});

    /* Build request headers — apiKey becomes Authorization: Bearer, plus
     * any literal headers the caller supplied (e.g. anthropic-version,
     * x-api-key, OpenAI org id, etc.). */
    var reqHeaders = {};
    if(this.apiKey) reqHeaders["Authorization"] = "Bearer " + this.apiKey;
    if(this.headers) Object.assign(reqHeaders, this.headers);

    curl.fetchAsync(this.apiBase + endPoint,
        {
            connectTimeout: 10,
            postJSON: postObj,
            headers:  reqHeaders,

            chunkCallback: function(r) {
                if(self.cancel === true) {
                    self.cancel = false;
                    return curl.cancel;
                }

                var btxt   = sprintf('%s', r.body);
                rld("CHUNK", btxt);
                var events = parseSSEChunk(btxt);

                for(var i = 0; i < events.length; i++) {
                    var ev = events[i];

                    if(ev.done) return;  /* [DONE] – final callback handles the rest */

                    if(ev.error) {
                        self.fetchError = ev.error;
                        chunkcb({error: ev.error, serverResponse: r, token: ""});
                        return false;
                    }

                    /* Capture token-usage when the server emits a usage chunk
                     * (final chunk before [DONE] when stream_options.include_usage
                     * is honored). The usage chunk typically has choices=[].
                     * Be defensive — different servers shape this slightly
                     * differently. */
                    if(ev.usage) {
                        self.usage = {
                            prompt:     ev.usage.prompt_tokens     || 0,
                            completion: ev.usage.completion_tokens || 0,
                            total:      ev.usage.total_tokens      ||
                                        ((ev.usage.prompt_tokens || 0) +
                                         (ev.usage.completion_tokens || 0))
                        };
                    }

                    var result = extractToken(ev);

                    /* Capture finish_reason as it arrives — only the last
                     * delta has it set, so we just keep overwriting. */
                    if (result.finishReason) lastFinishReason = result.finishReason;

                    /* tool_calls deltas: accumulate, do not emit through
                       the token callback — caller reads them from the
                       finalCallback's resp.toolCalls. */
                    if(result.toolCallsDelta) {
                        mergeToolCallDelta(self.toolCallsAcc, result.toolCallsDelta);
                        /* Feed each arg-delta string through cycle detector
                           to catch model degeneration inside JSON args. */
                        for (var di = 0; di < result.toolCallsDelta.length; di++) {
                            var dargs = result.toolCallsDelta[di].function &&
                                        result.toolCallsDelta[di].function.arguments;
                            if (typeof dargs !== 'string' || !dargs.length) continue;
                            var hit = cycleDetect(dargs);
                            if (hit) {
                                rld("CYCLE_DETECTED",
                                    {where:"tool_args", period:hit.period,
                                     cycles:hit.cycles, sample:hit.sample});
                                self.fetchError =
                                    sprintf("model output cycle detected (period=%d, sample=\"%s\")",
                                            hit.period, hit.sample);
                                chunkcb({error: self.fetchError,
                                         serverResponse: r, token: ""});
                                return curl.cancel;
                            }
                        }
                        continue;
                    }

                    if(!result.token.length) continue;

                    /* Feed text tokens through cycle detector too. */
                    {
                        var hit = cycleDetect(result.token);
                        if (hit) {
                            rld("CYCLE_DETECTED",
                                {where:"text", period:hit.period,
                                 cycles:hit.cycles, sample:hit.sample});
                            self.fetchError =
                                sprintf("model output cycle detected (period=%d, sample=\"%s\")",
                                        hit.period, hit.sample);
                            chunkcb({error: self.fetchError,
                                     serverResponse: r, token: ""});
                            return curl.cancel;
                        }
                    }

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
                if(self.toolCallsAcc && self.toolCallsAcc.length) {
                    resp.toolCalls = self.toolCallsAcc;
                }
                self.toolCallsAcc = undefined;
                /* Surface finish reason + a normalized truncated bool.
                 * 'length' = OpenAI's signal for "hit max_tokens". */
                if (lastFinishReason) {
                    resp.finishReason = lastFinishReason;
                    resp.truncated    = (lastFinishReason === 'length');
                }
                /* Token usage from the server, when reported. May be
                 * absent on servers that ignored stream_options.include_usage. */
                if (self.usage) resp.usage = self.usage;
                finalCallback(resp);
            }
        }
    );
}


/* ---- Server info / capacity discovery ---- *
 * Two backends, two strategies:
 *   - llama.cpp / Ollama / openai-compat: hit GET /props on the urlbase.
 *     llama-server returns context settings. Other servers may 404 —
 *     we return null then.
 *   - Anthropic: no equivalent endpoint. Use a model→capacity table.
 *     Claude 4 family is uniformly 200K; if it ever isn't, update here.
 *
 * Result shape: {n_ctx, n_ctx_train, model, fallback?}, or null if
 * discovery failed. Cached on the client by initBackend so callers
 * read this.serverInfo / this.capacity directly without re-fetching. */

function getServerInfoOpenAI() {
    /* /props is at the root, not under /v1, so use urlbase, not apiBase. */
    var base = this.urlbase || (this.apiBase ? this.apiBase.replace(/\/v1\/?$/, '') : null);
    if (!base) return null;
    try {
        var r = curl.fetch(base + '/props', {maxTime: 5});
        if (!r.status || r.status !== 200) return null;
        var p = JSON.parse(sprintf('%s', r.body));
        /* llama-server shape:
         *   { default_generation_settings: { n_ctx, ... },
         *     n_ctx, n_ctx_train, ...  (top-level on some builds) } */
        var s   = p.default_generation_settings || {};
        var ctx = s.n_ctx || p.n_ctx || null;
        var trn = s.n_ctx_train || p.n_ctx_train || null;
        if (!ctx) return null;
        return {n_ctx: ctx, n_ctx_train: trn || ctx, model: p.model_alias || p.model || null};
    } catch(_) {
        return null;
    }
}

/* Anthropic Claude 4 family is uniformly 200K. If a new model arrives
 * with a different cap, add an entry. Lookup is exact-match first, then
 * date-stripped (-YYYYMMDD), then a generic claude-* fallback. */
var ANTHROPIC_CAPACITY = {
    'claude-haiku-4-5':  200000,
    'claude-sonnet-4-5': 200000,
    'claude-sonnet-4-6': 200000,
    'claude-opus-4-1':   200000,
    'claude-opus-4-5':   200000,
    'claude-opus-4-7':   200000,
    /* Legacy 3.x family — 200K everywhere except claude-3-haiku (200K). */
    'claude-3-5-sonnet': 200000,
    'claude-3-7-sonnet': 200000,
    'claude-3-opus':     200000,
    'claude-3-haiku':    200000
};

function getServerInfoAnthropic() {
    var model = this.model || '';
    var ctx;
    if (ANTHROPIC_CAPACITY[model] !== undefined) {
        ctx = ANTHROPIC_CAPACITY[model];
        return {n_ctx: ctx, n_ctx_train: ctx, model: model};
    }
    /* Strip trailing -YYYYMMDD stamp and retry. */
    var stripped = model.replace(/-\d{8}$/, '');
    if (ANTHROPIC_CAPACITY[stripped] !== undefined) {
        ctx = ANTHROPIC_CAPACITY[stripped];
        return {n_ctx: ctx, n_ctx_train: ctx, model: model};
    }
    /* Unknown Claude — assume 200K (current family default). */
    if (/^claude-/.test(model)) {
        return {n_ctx: 200000, n_ctx_train: 200000, model: model, fallback: true};
    }
    return null;
}

/* ---- Constructors ---- */

/* Resolve `${VAR}` against process.env in any string in the opts object. */
function resolveEnv(s) {
    if (typeof s !== 'string') return s;
    return s.replace(/\$\{([A-Z_][A-Z0-9_]*)\}/gi, function(_, n){
        return process.env[n] || '';
    });
}

function initBackend(self, defaults, opts) {
    opts = opts || {};
    Object.assign(self, defaults, opts);

    /* Resolve env-var interpolation in known string fields */
    ['apiKey', 'baseURL', 'apiBase', 'model'].forEach(function(k){
        if (typeof self[k] === 'string') self[k] = resolveEnv(self[k]);
    });
    if (self.headers) {
        Object.keys(self.headers).forEach(function(k){
            self.headers[k] = resolveEnv(self.headers[k]);
        });
    }

    /* Configure apiBase. Two paths:
     *   1. opts.baseURL set — caller knows the full /v1 prefix
     *   2. opts.server + opts.port set — legacy llamaCpp/ollama style;
     *      compose http://host:port and append /v1
     * If neither, error. */
    if (!self.apiBase) {
        if (self.baseURL) {
            self.apiBase = self.baseURL.replace(/\/$/, '');
            if (!self.urlbase) self.urlbase = self.apiBase;
        } else if (typeof self.server === 'string' && typeof self.port === 'number') {
            self.urlbase = sprintf("http://%s:%d", self.server, self.port);
            self.apiBase = self.urlbase + "/v1";
        } else {
            err("LLM client: must specify baseURL OR (server + port)");
        }
    }
}

/* Set up capacity-related fields on a freshly-initialized client. Called
 * by each backend constructor after initBackend (so urlbase/apiBase are
 * resolved). serverInfo / capacity are cached on the instance; callers
 * read this.capacity directly. discoverServerInfo() re-runs discovery
 * (useful if the server restarts with a different -c). */
function attachCapacity(self) {
    var info = null;
    try { info = self.getServerInfo ? self.getServerInfo() : null; } catch(_) { info = null; }
    self.serverInfo = info || null;
    self.capacity   = (info && info.n_ctx) || null;
}

/* Generic OpenAI-compatible client. Works for: llama.cpp, Ollama,
 * OpenAI, OpenRouter, Together, Groq, DeepSeek, Gemini OpenAI mode,
 * any provider that speaks /v1/chat/completions with SSE streaming. */
function openaiCompat(opts) {
    initBackend(this, {query: query, getServerInfo: getServerInfoOpenAI}, opts || {});
    this._type = 'openaiCompat';
    attachCapacity(this);
}

/* llama.cpp convenience wrapper — connectivity-checked. */
function llamaCpp(opts) {
    initBackend(this, {server: '127.0.0.1', port: 8080, query: query,
                       getServerInfo: getServerInfoOpenAI}, opts || {});
    this._type = 'llamaCpp';
    if(this.urlbase && !checkIsRunning(this.urlbase + '/'))
        err(sprintf("llama.cpp server at %s doesn't appear to be running", this.urlbase));
    attachCapacity(this);
}

/* Ollama convenience wrapper. */
function ollama(opts) {
    initBackend(this, {server: '127.0.0.1', port: 11434, query: query,
                       getServerInfo: getServerInfoOpenAI}, opts || {});
    this._type = 'ollama';
    if(this.urlbase && !checkIsRunning(this.urlbase + '/'))
        err(sprintf("ollama server at %s doesn't appear to be running", this.urlbase));
    attachCapacity(this);
}

/* ---- Anthropic (Claude) client ---- *
 * Different request and response shape than OpenAI:
 *   - system prompt is a top-level field, not a role:'system' message
 *   - tool messages are user-role with tool_result content blocks
 *   - assistant tool calls are content blocks of type tool_use
 *   - tools schema lacks the {type:"function", function:{...}} wrapper
 *   - max_tokens is required
 *   - SSE event format uses event: lines with content_block_delta etc.
 */

function oaiToAnthropicMessages(oaiMessages) {
    var system = '';
    var msgs = [];
    oaiMessages.forEach(function(m){
        if (m.role === 'system') {
            system += (system ? '\n\n' : '') + (m.content || '');
            return;
        }
        if (m.role === 'tool') {
            msgs.push({
                role: 'user',
                content: [{
                    type: 'tool_result',
                    tool_use_id: m.tool_call_id || m.tool_name || 'unknown',
                    content: typeof m.content === 'string' ? m.content : JSON.stringify(m.content)
                }]
            });
            return;
        }
        if (m.role === 'assistant' && m.tool_calls) {
            var blocks = [];
            if (m.content) blocks.push({type: 'text', text: m.content});
            m.tool_calls.forEach(function(tc){
                var input = {};
                try { input = JSON.parse(tc.function.arguments || '{}'); } catch(_) {}
                blocks.push({
                    type: 'tool_use',
                    id:   tc.id,
                    name: tc.function.name,
                    input: input
                });
            });
            msgs.push({role: 'assistant', content: blocks});
            return;
        }
        /* plain user/assistant text */
        msgs.push({role: m.role, content: m.content});
    });
    return {system: system, messages: msgs};
}

function oaiToAnthropicTools(oaiTools) {
    if (!oaiTools) return undefined;
    return oaiTools.map(function(t){
        var fn = t.function || t;
        return {
            name:        fn.name,
            description: fn.description,
            input_schema: fn.parameters || {type:'object', properties:{}}
        };
    });
}

function anthropicQuery(prompt, callback, finalCallback) {
    var self = this;
    self.cancel = false;
    self.fetchError = false;

    if (callback && typeof callback != 'function') err("callback must be a function");
    if (finalCallback && typeof finalCallback != 'function') err("finalCallback must be a function");
    if (!callback && !finalCallback) err("at least one callback required");

    if (!self.model) err("anthropic: model not set (e.g. claude-sonnet-4-5)");
    if (!self.apiKey) err("anthropic: apiKey not set");

    var translated = oaiToAnthropicMessages(getType(prompt) === 'Array' ? prompt : [{role:'user', content: String(prompt)}]);
    var postObj = {
        model:      self.model,
        max_tokens: (self.params && self.params.max_tokens) || 4096,
        stream:     true,
        messages:   translated.messages
    };
    if (translated.system) postObj.system = translated.system;
    /* Tools can be set on the backend instance (self.tools) OR threaded
     * through per-call as self.params.tools (which is what agent.js does
     * at runToolLoop / planMode time). The OpenAI-compat path copies the
     * whole params object into postObj so it picks up tools naturally;
     * Anthropic needs to translate them, so we look in both places.
     * Without this, Sonnet receives no tool schemas, can't emit proper
     * tool_use blocks, and falls back to text-format <invoke> tags that
     * the dispatcher never executes — breaking every Anthropic-backed
     * agent run. */
    var tools = (self.params && self.params.tools) || self.tools;
    if (tools) postObj.tools = oaiToAnthropicTools(tools);
    /* Anthropic uses tool_choice with shape {"type":"auto"|"any"|"tool",
     * "name":"..."} not the plain string OpenAI expects. Translate. */
    if (self.params && self.params.tool_choice) {
        var tc = self.params.tool_choice;
        if (tc === 'auto')      postObj.tool_choice = {type: 'auto'};
        else if (tc === 'any')  postObj.tool_choice = {type: 'any'};
        else if (tc === 'none') { /* anthropic default — omit tool_choice */ }
        else if (typeof tc === 'object') postObj.tool_choice = tc;
    }
    if (self.params) {
        ['temperature', 'top_p', 'top_k', 'stop_sequences'].forEach(function(k){
            if (self.params[k] !== undefined) postObj[k] = self.params[k];
        });
    }

    var headers = {
        "x-api-key":         self.apiKey,
        "anthropic-version": "2023-06-01",
        "content-type":      "application/json"
    };
    if (self.headers) Object.assign(headers, self.headers);

    /* Anthropic SSE state — content blocks streamed by index */
    var blocks = {};       /* idx → {type, text|input_json, id, name} */
    var fullText  = '';
    var toolCalls = [];
    /* Anthropic returns stop_reason in the message_delta event near the
     * end of the stream. Capture it so the caller can detect truncation
     * (stop_reason === 'max_tokens') or other end states. */
    var stopReason = null;
    /* Token usage — Anthropic streams it in two phases:
     *   - message_start carries final input_tokens + initial output_tokens=1
     *   - message_delta carries cumulative output_tokens as the stream grows
     * Reset on self so client.usage is fresh per call. */
    self.usage = null;
    var inputTokens          = null;
    var outputTokens         = null;
    var cacheReadTokens      = null;
    var cacheCreationTokens  = null;

    function chunkcb(c) {
        if (callback) return callback(c);
    }

    function handleEvent(ev) {
        if (!ev || !ev.type) return;
        switch (ev.type) {
            case 'message_start':
                /* Initial usage block: input_tokens is the authoritative
                 * server count of the prompt; output_tokens starts at 1
                 * and grows in subsequent message_delta events. Cache
                 * fields appear only when prompt caching applies. */
                if (ev.message && ev.message.usage) {
                    var u0 = ev.message.usage;
                    inputTokens         = u0.input_tokens         || null;
                    outputTokens        = u0.output_tokens        || 0;
                    cacheReadTokens     = u0.cache_read_input_tokens     || null;
                    cacheCreationTokens = u0.cache_creation_input_tokens || null;
                }
                break;
            case 'content_block_start':
                blocks[ev.index] = Object.assign({}, ev.content_block);
                if (blocks[ev.index].type === 'tool_use')
                    blocks[ev.index].input_json = '';
                break;
            case 'content_block_delta':
                var b = blocks[ev.index];
                if (!b) return;
                if (ev.delta.type === 'text_delta') {
                    var t = ev.delta.text || '';
                    fullText += t;
                    chunkcb({thinking: false, token: t});
                } else if (ev.delta.type === 'input_json_delta') {
                    b.input_json = (b.input_json || '') + (ev.delta.partial_json || '');
                }
                break;
            case 'content_block_stop':
                var bb = blocks[ev.index];
                if (bb && bb.type === 'tool_use') {
                    var args = {};
                    try { args = JSON.parse(bb.input_json || '{}'); } catch(_) {}
                    toolCalls.push({
                        id: bb.id,
                        type: 'function',
                        function: { name: bb.name, arguments: JSON.stringify(args) }
                    });
                }
                break;
            case 'message_delta':
                /* Anthropic puts stop_reason here, alongside output usage.
                 * `usage.output_tokens` is cumulative — keep overwriting. */
                if (ev.delta && ev.delta.stop_reason) {
                    stopReason = ev.delta.stop_reason;
                }
                if (ev.usage && typeof ev.usage.output_tokens === 'number') {
                    outputTokens = ev.usage.output_tokens;
                }
                break;
            case 'message_stop':
                /* stream end signaled by stream end too */
                break;
        }
    }

    curl.fetchAsync(self.apiBase + "/messages", {
        connectTimeout: 10,
        postJSON: postObj,
        headers:  headers,
        chunkCallback: function(r) {
            if (self.cancel === true) { self.cancel = false; return curl.cancel; }
            var btxt = sprintf('%s', r.body);
            /* Anthropic SSE: pairs of "event: X" + "data: {...}" lines */
            btxt.split('\n').forEach(function(line){
                line = line.trim();
                if (line.indexOf('data:') !== 0) return;
                var d = line.substring(5).trim();
                if (!d) return;
                try { handleEvent(JSON.parse(d)); }
                catch(_) {}
            });
        }
    }, function(r) {
        if (!r.status || r.status !== 200) {
            var msg = "anthropic " + (r.status || 0);
            try {
                var p = JSON.parse(sprintf('%s', r.body));
                if (p.error && p.error.message) msg += ": " + p.error.message;
            } catch(_) {}
            chunkcb({done: true, token: '', error: msg});
            if (finalCallback) finalCallback({serverResponse: r, fullText: '', error: msg});
            return;
        }
        chunkcb({done: true, token: ''});
        if (finalCallback) {
            var resp = {serverResponse: r, fullText: fullText};
            if (toolCalls.length) resp.toolCalls = toolCalls;
            /* Pass through stop_reason; mark truncated when the model hit
             * its output cap. Anthropic uses 'max_tokens' for that. */
            if (stopReason) {
                resp.finishReason = stopReason;
                resp.truncated    = (stopReason === 'max_tokens');
            }
            /* Token usage. Anthropic always reports input_tokens; output
             * is cumulative from message_delta events. Cache fields appear
             * only when prompt caching applies. Build self.usage in the
             * same shape as the OpenAI-compat path so callers don't have
             * to branch on backend type. */
            if (inputTokens !== null || outputTokens !== null) {
                self.usage = {
                    prompt:     inputTokens  || 0,
                    completion: outputTokens || 0,
                    total:      (inputTokens || 0) + (outputTokens || 0)
                };
                if (cacheReadTokens !== null)
                    self.usage.cache_read = cacheReadTokens;
                if (cacheCreationTokens !== null)
                    self.usage.cache_creation = cacheCreationTokens;
                resp.usage = self.usage;
            }
            finalCallback(resp);
        }
    });
}

function anthropic(opts) {
    opts = opts || {};
    /* baseURL default for Anthropic */
    if (!opts.baseURL && !opts.apiBase) opts.baseURL = 'https://api.anthropic.com/v1';
    initBackend(this, {query: anthropicQuery, getServerInfo: getServerInfoAnthropic}, opts);
    this._type = 'anthropic';
    if (!this.model) err("anthropic: model required (e.g. 'claude-sonnet-4-5')");
    attachCapacity(this);
}

/* ---- Claude Code (claude CLI) as a provider ---- *
 * Shells out to `claude --print --bare` for the entire turn. No streaming
 * (claude buffers its final answer); no tool-call exposure (claude does
 * its own tool dispatch internally, the agent's local tool registry is
 * unused for this turn). Useful as a "fall back to Claude for this whole
 * conversation" mode. For per-task delegation while keeping local tools
 * active, use the `delegate_to_claude` TOOL instead — it's strictly more
 * composable. */

function claudeCodeQuery(prompt, callback, finalCallback) {
    var self = this;
    self.cancel = false;

    /* Split system messages off; pass them via --append-system-prompt so
     * Claude treats them as system instructions rather than as inlined
     * user content. The rest (user/assistant/tool turns) is concatenated
     * and passed via -p. */
    var systemText = '';
    var userText   = '';
    if (typeof prompt === 'string') {
        userText = prompt;
    } else if (getType(prompt) === 'Array') {
        prompt.forEach(function(m){
            var content = m.content;
            if (typeof content !== 'string') content = JSON.stringify(content);
            if (m.role === 'system') {
                systemText += (systemText ? '\n\n' : '') + content;
            } else if (m.role === 'tool') {
                userText += '\n[tool result]\n' + content + '\n';
            } else {
                userText += '\n[' + (m.role || 'user') + ']\n' + content + '\n';
            }
        });
    } else {
        userText = String(prompt);
    }

    var argv = [self.binary || 'claude', '--print'];
    if (self.bare) argv.push('--bare');     /* opt-in; requires ANTHROPIC_API_KEY */
    if (self.allowedTools) argv.push('--allowed-tools', String(self.allowedTools));
    if (self.maxBudget !== undefined && self.maxBudget !== null)
        argv.push('--max-budget-usd', String(self.maxBudget));
    if (systemText) argv.push('--append-system-prompt', systemText);
    if (self.extraArgs && getType(self.extraArgs) === 'Array')
        self.extraArgs.forEach(function(a){ argv.push(String(a)); });
    argv.push(userText);
    argv.push({
        timeout:       (self.timeout || 600) * 1000,
        captureStderr: true
    });

    var r;
    try { r = exec.apply(null, argv); }
    catch (e) {
        if (callback)      callback({error: 'claude-code exec failed: ' + e.message, token: '', done: true});
        if (finalCallback) finalCallback({fullText: '', error: e.message});
        return;
    }
    var fullText = sprintf('%s', r.stdout || '');
    var errMsg = (r.exitStatus !== 0 || r.timedOut)
        ? ('claude-code exit=' + r.exitStatus +
           (r.timedOut ? ' (timed out)' : '') +
           (r.stderr ? ': ' + sprintf('%s', r.stderr).substring(0, 512) : ''))
        : null;

    if (callback) {
        if (fullText.length) callback({thinking: false, token: fullText});
        callback({done: true, token: ''});
    }
    if (finalCallback) {
        var resp = {fullText: fullText};
        if (errMsg) resp.error = errMsg;
        finalCallback(resp);
    }
}

function claudeCode(opts) {
    opts = opts || {};
    Object.assign(this, {
        query:        claudeCodeQuery,
        binary:       'claude',
        timeout:      600,
        allowedTools: 'Read,Grep,Glob,LS,Edit,Bash'
    }, opts);
    this._type   = 'claudeCode';
    this.urlbase = 'shell://' + this.binary;
    this.apiBase = '';
    /* model is informational (claude picks based on its own config / login) */
    if (!this.model) this.model = 'claude-code';
    /* No introspection path for the CLI provider. Callers should
     * handle this.capacity === null gracefully (e.g., fall back to the
     * legacy compaction budget). */
    this.serverInfo = null;
    this.capacity   = null;
    this.getServerInfo = function(){ return null; };
}

/* ---- Provider factory ---- *
 * Given a config like:
 *   {type: "anthropic", model: "claude-sonnet-4-5", apiKey: "..."}
 *   {type: "openai-compat", baseURL: "https://api.openai.com/v1", apiKey: "..."}
 *   {type: "llamacpp", server: "en", port: 8080}
 * return an instantiated client. */
function providerFromConfig(cfg) {
    cfg = cfg || {};
    var t = (cfg.type || 'openai-compat').toLowerCase();
    if (t === 'anthropic')                            return new anthropic(cfg);
    if (t === 'llamacpp' || t === 'llama')            return new llamaCpp(cfg);
    if (t === 'ollama')                               return new ollama(cfg);
    if (t === 'claude-code' || t === 'claudecode')    return new claudeCode(cfg);
    /* default: generic OpenAI-compatible */
    return new openaiCompat(cfg);
}

module.exports = {
    ollama:             ollama,
    llamaCpp:           llamaCpp,
    openaiCompat:       openaiCompat,
    anthropic:          anthropic,
    resolveEnv:         resolveEnv,
    claudeCode:         claudeCode,
    providerFromConfig: providerFromConfig,
    /* Exported for testing.  The detector is a pure function of the
     * token stream, so a captured degenerate generation can be replayed
     * through it offline -- which is the only practical way to test it:
     * the real failure appeared once in three runs, by luck. */
    makeCycleDetector:  makeCycleDetector
};
