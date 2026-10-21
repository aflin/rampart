/*  llmchat.js — General-purpose LLM chat websocket handler.

    OVERVIEW:
    This script handles websocket connections for the general chat demo.
    It streams LLM responses token-by-token to the browser, supports
    thinking/reasoning models (via <think> tags or --reasoning-format),
    and can give the model tools (defined in llmchat-tools.js: web search,
    page and document fetching, ...) through standard OpenAI-style tool
    calling.  Tool calls are shown in the chat as collapsed blocks, like
    thinking.

    Accessed at: ws(s)://host/wsapps/llmchat.js

    FLOW:
    1. Browser opens websocket (the chat page sets llmSettings)
    2. Handshake (req.count == 0): loads model config from llmchat-models.js,
       creates LLM client, queries server for context window size
    3. Messages (req.count > 0): handles chat, cancel, reset, retitle,
       and context-restore commands
    4. For chat messages: query the model with the tool list.  If it
       answers with tool_calls, run them, append the results as
       role:"tool" messages and query again; when it answers in text,
       that is the reply.

    KEY FILES:
    - llmchat-models.js: model config (system prompt, server, params, tools)
    - llmchat-tools.js:  the tools: declarations and what running them does
    - rampart-llm.js:    LLM streaming client (in /usr/local/rampart/modules/)
    - the chat page:     browser UI (chat.html, codechat.html)
    - llmchat-client.js: browser-side chat logic (js/ beside the page)
    - llmchat.css:       chat styles (css/ beside the page)
*/

/*  rampart.globalize() makes rampart.utils functions (sprintf, fprintf,
    getType, etc.) available as globals, so we don't need to prefix them.  */
rampart.globalize(rampart.utils);

/* rampart-llm.js provides llamaCpp and ollama constructors that stream
   completions from llama-server or ollama via their OpenAI-compatible APIs. */
var llm=require("rampart-llm.js");
/* rampart-curl provides fetch() and fetchAsync() for HTTP requests. */
var curl=require("rampart-curl.so");
var tools=require("llmchat-tools.js");

/* Tool-call log (tools.log in the server's log directory), written when
   the model's `tools' settings have log: true. */
var toollog = serverConf.logRoot + "/tools.log";
var logTools = false;       /* set per connection from the model's settings */

function toolLog(msg) {
    if (!logTools) return;
    fprintf(toollog, true, "[%s] %s\n", new Date().toISOString(), msg);
}


/* Ask the LLM to generate a short conversation title.
   Called automatically after the first response, or on demand via "retitle".
   messages: optional array of {role, content} — if omitted, uses req.prompt. */
function makeTitle(req, messages) {
    if(!messages) {
        if(req.titled)
            return;
        req.titled=true;
        messages = req.prompt;
    }

    if(!messages || getType(messages) !== 'Array' || messages.length < 2)
        return;

    /* Use last few exchanges so the title reflects topic drift */
    var recent = messages.slice(-6);
    var summary = '';
    recent.forEach(function(m) {
        if (m.role === 'user')
            summary += 'User: ' + String(m.content || '').slice(0, 200) + '\n';
        else if (m.role === 'assistant')
            summary += 'Assistant: ' + String(m.content || '').slice(0, 200) + '\n';
    });

    if (!summary) return;

    var prompt = [
        { role: 'system', content: "Generate a short title (4-7 words) for the conversation below. Output ONLY the title. No quotes, no explanation, no punctuation at the end."},
        { role: 'user', content: summary}
    ];

    /* Side queries use their own client (req.sideLlama): a client tracks
       one request at a time, and the title request can still be running
       when the next question arrives.  max_tokens must be generous because
       thinking/reasoning models consume tokens internally before producing
       visible output (which is all that ends up in fullText). */
    var side = req.sideLlama;
    side.params = {temperature: 0.1, max_tokens: 2048};

    /* llama.query(prompt, perTokenCallback, finalCallback)
       - perTokenCallback fires for each streamed token (or error/done)
       - finalCallback fires once when the full response is complete
       - res.fullText contains the concatenated non-thinking output         */
    side.query(
        prompt,
        function(res) { if (res.error || res.done) return; },
        function(res) {
            var title = (res.fullText || '').trim();
            if (!res.error && title.length)
                req.wsSend({title: title});
        }
    );
}

/* The system message: the model's prompt plus today's date, which the
   model needs to judge what counts as recent when it searches. */
function systemMessage(req) {
    var d = new Date();
    return { role: 'system', content: req.sysPrompt +
             sprintf("\n\nToday's date is %s.", d.toISOString().slice(0, 10)) };
}

/* ---- Answering one question ---- */
/* req.prompt is the conversation so far (system, user, assistant, ...,
   ending with the new user message).  Each round sends it plus this
   turn's tool exchanges (req.turnMsgs) and the tool list.  A reply with
   tool_calls is executed and appended to req.turnMsgs; a reply in text
   ends the turn.  The turn's tool exchanges and the reply then join
   req.prompt, so later questions can refer to what the tools returned
   (article ids, passages, pages).  The browser keeps only the text, so
   a conversation restored from it starts without them. */
function answer(req) {
    req.turnId = (req.turnId || 0) + 1;
    req.turnMsgs = [];
    req.turnText = '';
    req.errorSent = false;
    req.sources = [];
    req.round = 0;
    doRound(req);
}

/* list of distinct {title, link} sources gathered this turn */
function addSources(req, list) {
    list.forEach(function(s) {
        if (!s || !s.link) return;
        for (var i = 0; i < req.sources.length; i++)
            if (req.sources[i].link == s.link) {
                /* a search result that was then read is a read source */
                if (!s.kind) delete req.sources[i].kind;
                return;
            }
        req.sources.push(s);
    });
}

/* a URL reduced to host + path, for matching what the answer wrote */
function urlKey(u) {
    return String(u || '').toLowerCase().replace(/^https?:\/\/(www\.)?/, '')
                          .replace(/[?#].*$/, '').replace(/\/+$/, '');
}

/* The sources the answer names, in the answer's order: by URL, or by title
   for pages and articles the tools read (the answer may write a different
   URL form).  Search results count only by URL -- titles are not unique,
   and a look-alike site can carry the same title.  If the answer names
   none: the pages the tools read (not mere search results). */
function citedSources(sources, text) {
    var t = String(text || '').toLowerCase();
    var keys = (t.match(/https?:\/\/[^\s<>"'`)\]]+/g) || []).map(urlKey);
    var named = [];
    sources.forEach(function(s) {
        var title = String(s.title || '').toLowerCase().replace(/\s+[|–—-]\s.*$/, '').trim();
        var at = keys.indexOf(urlKey(s.link));
        if (at < 0 && s.kind != 'search' && title.length > 2) {
            var k = t.indexOf(title);
            if (k >= 0) at = keys.length + k;      /* named by title: after the URL matches */
        }
        if (at >= 0) named.push({ s: s, at: at });
    });
    if (named.length)
        return named.sort(function(a, b) { return a.at - b.at; })
                    .map(function(n) { return { title: n.s.title, link: n.s.link }; });
    return sources.filter(function(s) { return s.kind != 'search'; })
                  .map(function(s) { return { title: s.title, link: s.link }; });
}

/* drop the oldest turn (its user message through to the next user message,
   tool exchanges included) from a history; false when none can go */
function trimOldestTurn(prompt) {
    if (prompt.length < 3) return false;
    var j = 2;
    while (j < prompt.length && prompt[j].role != 'user') j++;
    if (j >= prompt.length) return false;      /* only the current question is left */
    prompt.splice(1, j - 1);
    return true;
}

function endTurn(req) {
    req.wsSend({end:"<br><hr><br>"});
    req.processing=false;
}

/* Characters of a round's text held back before streaming starts.  A round
   that ends in tool calls usually opens with one short sentence; that goes
   to the thinking panel instead of the answer. */
var HOLD_CHARS = 300;

/* Tool calls written as text: the server parses these into tool_calls, but
   a call it cannot parse (or any call when tool_choice is "none") arrives
   as plain text.  Nothing from such markup on is shown as the answer. */
var CALL_TEXT = /<tool_call>|<function=/;
var TAIL_HOLD = 12;     /* chars kept back while streaming: a marker split across tokens */

/* the answer-worthy part of this round's text: everything before tool-call markup */
function visibleText(req) {
    var k = req.roundText.search(CALL_TEXT);
    return k < 0 ? req.roundText : req.roundText.slice(0, k);
}

/* send what can be shown of this round's text so far; `all' at the end */
function pumpAnswer(req, all) {
    var t = req.roundText, marked = CALL_TEXT.test(t);
    var vis = visibleText(req);
    if (!req.answering) {
        if (!all && (marked || vis.length < HOLD_CHARS)) return;
        req.wsSend({srvmsg: "answering..."});
        req.answering = true;
    }
    var upto = (all || marked) ? vis.length : Math.max(req.sentLen, vis.length - TAIL_HOLD);
    if (upto > req.sentLen) {
        req.wsSend(vis.slice(req.sentLen, upto));
        req.sentLen = upto;
    }
}

/* tool calls the server left in the text, in the two common forms:
     Qwen/XML   <tool_call><function=NAME><parameter=KEY>value</parameter>...</function></tool_call>
     Hermes     <tool_call>{"name": NAME, "arguments": {...}}</tool_call>          */
function textToolCalls(text, round) {
    var calls = [], m, re = /<tool_call>([\s\S]*?)(?:<\/tool_call>|$)/g;
    while ((m = re.exec(text)) !== null) {
        var body = m[1], name = null, args = {};
        var fm = body.match(/<function=([^>\s]+)>([\s\S]*?)(?:<\/function>|$)/);
        if (fm) {
            name = fm[1];
            var pm, pre = /<parameter=([^>\s]+)>([\s\S]*?)<\/parameter>/g;
            while ((pm = pre.exec(fm[2])) !== null) {
                var v = pm[2].trim();
                if (v.length) args[pm[1]] = v;
            }
        } else {
            try {
                var j = JSON.parse(body.trim());
                name = j.name;
                args = typeof j.arguments == 'string' ? JSON.parse(j.arguments) : (j.arguments || {});
            } catch (e) { name = null; }
        }
        if (name)
            calls.push({ id: sprintf("call_%d_t%d", round, calls.length), type: 'function',
                         function: { name: name, arguments: JSON.stringify(args) } });
        if (!m[0].length) break;
    }
    return calls;
}

/* Run a round's tool calls one at a time, each on a later turn of the event
   loop: the websocket only sends what is queued when the handler returns,
   so this lets "searching..." and the open tool block reach the browser
   before the tool runs, and lets a stop request in between be seen.
   `turn' guards against a stopped turn resuming during a newer one. */
function runCalls(req, calls, i, turn) {
    if (turn != req.turnId) return;
    if (req.cancelled) { req.cancelled = false; return; }
    if (i >= calls.length) {
        req.round++;
        doRound(req);
        return;
    }
    var c = calls[i], shown;
    try { shown = tools.describe(c.function.name, JSON.parse(c.function.arguments || "{}")); }
    catch(e) { shown = "running " + c.function.name; }
    req.wsSend({srvmsg: sprintf("%H\u2026", shown)});
    /* the chat window shows each call as a collapsed block:
       {tool} opens it, {toolResult} fills it in */
    req.wsSend({tool: {id: c.id, name: c.function.name, label: shown,
                       args: c.function.arguments || "{}"}});
    setTimeout(function() {
        if (turn != req.turnId) return;
        if (req.cancelled) { req.cancelled = false; return; }
        var t0 = performance.now();
        var r = tools.run(req.toolCfg, c, req.webCache);
        var ms = performance.now() - t0;
        req.wsSend({toolResult: {id: c.id, summary: r.summary, ms: Math.round(ms),
                                 chars: r.content.length, raw: r.content.slice(0, 4000),
                                 link: r.link}});
        toolLog(sprintf("round %d %s %s -> %d chars in %.0fms: %s", req.round,
            c.function.name, c.function.arguments, r.content.length, ms, r.content.slice(0, 200)));
        addSources(req, r.sources);
        req.turnMsgs.push({ role: 'tool', tool_call_id: c.id, content: r.content });
        runCalls(req, calls, i + 1, turn);
    }, 0);
}

function doRound(req) {
    var llama = req.llama;
    var turn = req.turnId;
    var useTools = req.toolDefs && req.round < req.maxRounds;

    req.wsSend({srvmsg: req.round ? "processing results…" : "processing..."});
    req.thinking=true;
    req.answering=false;
    req.contextError=false;
    req.roundText='';
    req.sentLen=0;

    var params = Object.assign({}, req.baseParams);
    if (req.toolDefs) {
        /* after maxRounds the tools stay declared (the transcript refers to
           them) but the model is told to answer now */
        params.tools = req.toolDefs;
        params.tool_choice = useTools ? "auto" : "none";
    }
    llama.params = params;

    var queryPrompt = req.prompt.concat(req.turnMsgs);
    if (req.toolDefs && !useTools)
        queryPrompt.push({ role: 'user', content:
            "(The tool-call limit for this question has been reached. Do not call any " +
            "more tools. Answer now from the information above, and say what you " +
            "could not find.)" });

    llama.query(
        queryPrompt,

        /* Per-token callback — fires for every streamed token.
           res.token is the text, res.thinking indicates reasoning
           content (from models using <think> tags or the
           --reasoning-format deepseek flag in llama-server).
           res.error/res.done signal error or stream end.  Tool calls
           are not streamed here; they arrive in the final callback. */
        function(res) {
            if(res.error) {
                var errMsg = res.error;
                var isContextError = false;

                if(typeof errMsg == 'object') {
                    if(errMsg.type === 'exceed_context_size_error') {
                        isContextError = true;
                        errMsg = sprintf("Context exceeded: %d tokens sent, max is %d", errMsg.n_prompt_tokens, errMsg.n_ctx);
                    } else {
                        /* check message text for context-related errors */
                        var msgText = errMsg.message || '';
                        if(msgText.indexOf('context') !== -1)
                            isContextError = true;
                        errMsg = msgText || JSON.stringify(errMsg);
                    }
                } else if(typeof errMsg == 'string' && errMsg.indexOf('context') !== -1) {
                    isContextError = true;
                }

                /* if we can trim older messages, suppress the error
                   and let the final callback retry                  */
                if(isContextError && req.prompt.length > 3) {
                    req.contextError = errMsg;
                    return;
                }
                req.errorSent = errMsg;
                return;
            }
            if(res.done)
                return;     /* the final callback decides what comes next */

            if(! req.llama.cancel && res.token) {
                if(res.thinking) {
                    if(req.thinking) {
                        req.wsSend({srvmsg: "thinking..."});
                        req.thinking=false;
                    }
                    req.wsSend({think: res.token});
                } else {
                    /* hold the start of the text: a short preamble before
                       tool calls ("Let me search...") is not the answer */
                    req.roundText += res.token;
                    pumpAnswer(req, false);
                }
            }
        },

        /* Final callback — fires once after the stream ends.
           res.fullText has the complete non-thinking response and
           res.toolCalls any tool calls the model made.               */
        function(res) {
            if (req.cancelled || turn != req.turnId) {   /* stopped by the user; already ended */
                if (turn == req.turnId) req.cancelled = false;
                return;
            }

            /* Context overflow — remove the oldest turn and retry.  This
               lets long conversations shed old context instead of failing. */
            if(req.contextError) {
                if(trimOldestTurn(req.prompt)) {
                    req.wsSend({srvmsg: "Context exceeded – trimming earlier messages…"});
                    doRound(req);
                } else {
                    req.wsSend({error: req.contextError});
                    endTurn(req);
                }
                return;
            }

            if(res.error || req.errorSent) {
                req.wsSend({error: req.errorSent || res.error});
                req.errorSent = false;
                endTurn(req);
                return;
            }

            var vis = visibleText(req);
            var calls = (res.toolCalls || []).filter(function(c) { return c && c.function && c.function.name; });
            if (!calls.length && useTools)
                calls = textToolCalls(req.roundText, req.round);

            /* The model asked for tools: run them, then ask again */
            if (calls.length && useTools) {
                calls.forEach(function(c, i) {
                    if (!c.id) c.id = sprintf("call_%d_%d", req.round, i);
                });
                req.turnMsgs.push({ role: 'assistant', content: vis, tool_calls: calls });
                /* a held-back preamble belongs with the reasoning, not the answer */
                if (req.answering) {
                    pumpAnswer(req, true);
                    req.turnText += vis;
                } else if (vis.trim())
                    req.wsSend({think: vis.trim() + "\n"});
                runCalls(req, calls, 0, turn);
                return;
            }

            /* A text answer: the end of this turn */
            if (res.serverResponse && res.serverResponse.status && res.serverResponse.status != 200) {
                req.wsSend({srvmsg: sprintf("%d: %s", res.serverResponse.status, res.serverResponse.text)});
                endTurn(req);
                return;
            }

            /* show whatever of the reply was still held back */
            if (!vis.trim()) {
                /* nothing but tool-call markup, even after being told to stop */
                req.roundText = vis = "I wasn't able to finish looking this up within the " +
                                      "tool-call limit for one question.";
            }
            pumpAnswer(req, true);
            req.turnText += vis;
            if (CALL_TEXT.test(req.roundText))
                toolLog(sprintf("round %d: tool-call text dropped from the answer", req.round));

            /* Append this turn's tool exchanges and the reply to the
               conversation history so the LLM sees them next turn.  */
            req.prompt = req.prompt.concat(req.turnMsgs);
            req.prompt.push( { role: "assistant", content: vis } );
            req.turnMsgs = [];
            endTurn(req);
            /* Send the pages the answer drew on, for attribution */
            var cited = citedSources(req.sources, req.turnText);
            if (cited.length)
                req.wsSend({ragSources: cited});
            if(!req.titled)
                makeTitle(req);
            req.wsSend({srvmsg: ""});
        }
    );
}

/* ---- Main websocket handler ---- */
/* Rampart maps this file to ws(s)://host/wsapps/llmchat.js

   This function is called once per websocket event:
     req.count == 0 : initial HTTP upgrade / handshake (no req.body)
     req.count  > 0 : each subsequent message from the client

   The req object persists for the lifetime of the connection, so we
   attach state (req.llama, req.prompt, etc.) during the handshake
   and reuse it in later messages.                                    */
function chat(req) {

    /* ---- Handshake (count == 0): set up the LLM connection ---- */
    if( req.count==0) {
        /* Model definitions live in a separate file for easy editing. */
        var models = require("llmchat-models.js");
        var modelObj;

        /* Client can pass settings (e.g. model name) as the query-string
           parameter "s", parsed automatically by Rampart into req.params. */
        var settings = req.params.s;

        if(settings && settings.model) {
            modelObj = models[settings.model]
        } else {
            modelObj = models[models.defaultModel];
        }

        if(!modelObj) {
            req.wsSend({error: "model not found"});
            req.wsEnd();
            return;
        }
        var opts = {};
        if(modelObj.server)
            opts.server=modelObj.server;
        if(modelObj.port)
            opts.port=modelObj.port;

        /* Create an LLM client instance and store it on req so it
           persists across messages for this websocket connection.
           llm.llamaCpp and llm.ollama both expose the same .query() API. */
        try {
            if( modelObj.engine == "llamaCpp" ) {
                req.llama = new llm.llamaCpp(opts);
                req.sideLlama = new llm.llamaCpp(opts);
            } else if ( modelObj.engine == "ollama" ) {
                req.llama = new llm.ollama(opts);
                req.sideLlama = new llm.ollama(opts);
            } else {
                req.wsSend( { error: "Unsupported engine " + modelObj.engine} );
                req.wsEnd();
                return;
            }
        } catch(e) {
            req.wsSend( { error: e.message });
            req.wsEnd();
            return;
        }

        req.llama.model = modelObj.model;
        req.sideLlama.model = modelObj.model;
        req.sysPrompt = modelObj.sysPrompt;

        /* params (temperature, max_tokens, etc.) are merged directly into
           the /v1/chat/completions POST body by rampart-llm.js; each round
           adds the tool list to a copy of them */
        req.baseParams = modelObj.params || {};
        req.llama.params = req.baseParams;

        /* the tools this model may call (none if the model entry has no `tools') */
        if (modelObj.tools) {
            req.toolCfg   = modelObj.tools;
            req.toolDefs  = tools.definitions(modelObj.tools);
            req.maxRounds = modelObj.tools.maxRounds || 6;
            logTools      = !!modelObj.tools.log;
            req.webCache  = {};      /* pages web_fetch has read on this connection */
        }

        /* Query llama-server's /props and /slots endpoints to discover
           the per-slot context size, then send it to the client for the
           token-usage bar.  /slots is preferred (accurate with --parallel). */
        try {
            var propsUrl = sprintf("http://%s:%d/props", modelObj.server || '127.0.0.1', modelObj.port || 8080);
            var pres = curl.fetch(propsUrl, {maxTime: 5});
            if(pres.status == 200) {
                var props = JSON.parse(pres.text);
                var nCtx = props.default_generation_settings && props.default_generation_settings.n_ctx;
                /* n_ctx is per-slot when using --parallel, so report as-is */
                if(nCtx)
                    req.wsSend({maxTokens: nCtx});
            }
            var slotsUrl = sprintf("http://%s:%d/slots", modelObj.server || '127.0.0.1', modelObj.port || 8080);
            var sres = curl.fetch(slotsUrl, {maxTime: 5});
            if(sres.status == 200) {
                var slots = JSON.parse(sres.text);
                if(getType(slots) == 'Array' && slots.length && slots[0].n_ctx) {
                    req.wsSend({maxTokens: slots[0].n_ctx});
                }
            }
        } catch(e) { /* props/slots not available, skip */ }

        return;
    }

    /* ---- Subsequent messages (count > 0) ---- */

    if(req.body.length) {

        var cmdobj;
        var intxt=sprintf("%s", req.body);  /* req.body is a Buffer */
        var restored = false;

        /* Messages from the client are either plain text (a chat message)
           or JSON commands: cancel, reset, retitle, or context-restore.   */
        try {
            cmdobj=JSON.parse(intxt);
        } catch(e) { cmdobj = null; }

        if (cmdobj && typeof cmdobj == 'object') {
            if(cmdobj.cmd == "cancel") {
                if (req.processing) {
                    /* Setting cancel=true causes rampart-llm.js to abort the
                       in-flight HTTP stream; the final callback then sees
                       req.cancelled and stops the turn.                   */
                    req.llama.cancel=true;
                    req.cancelled=true;
                    req.processing=false;
                    /* keep the stopped turn in the history, as far as the
                       user saw it, so the question is not left unanswered
                       (the model would otherwise go back to it) */
                    var seen = (req.turnText || '') +
                               (req.answering ? visibleText(req).slice(0, req.sentLen) : '');
                    req.prompt.push({ role: 'assistant', content: seen.trim() ?
                        seen + "\n\n[Stopped by the user.]" : "[Stopped by the user before answering.]" });
                    req.turnMsgs = [];
                    req.wsSend("<br><hr><br>");
                }
                return;

            } else if (cmdobj.cmd == "retitle") {
                makeTitle(req, cmdobj.messages || null);
                return;

            } else if (cmdobj.cmd == "reset") {

                /* got a reset request */
                if(req.processing)
                {
                    req.llama.cancel=true;
                    req.cancelled=true;
                    req.processing=false;
                }
                /* erase the history of this conversation */
                req.prompt=false;
                req.titled=false;
                return;

            } else if (cmdobj.context && cmdobj.q) {

                /* continuing an existing conversation stored in browser */
                req.prompt=cmdobj.context.filter(function(m) {
                    return m && (m.role == 'user' || m.role == 'assistant');
                }).map(function(m) {
                    return { role: m.role, content: String(m.content || '') };
                });
                req.prompt.unshift(systemMessage(req));
                intxt=String(cmdobj.q);
                req.titled = req.prompt.length >= 3;
                /* after a reconnect the browser's history already ends
                   with this question */
                var last = req.prompt[req.prompt.length - 1];
                restored = last.role == 'user' && last.content.trim() == intxt.trim();
            }
        }

        if (!req.processing) {
            req.processing=true;
            req.cancelled=false;

            /* req.prompt holds the full conversation as an array of
               {role:'system'|'user'|'assistant', content:''} objects.
               The entire array is sent to the LLM on every query so
               it has full context (llama-server is stateless per-request). */
            if(!req.prompt) {
                req.prompt = [
                    systemMessage(req),
                    { role: 'user', content: sprintf("%s\n", intxt) }
                ];
                req.titled=false;
            } else if (!restored) {
                req.prompt.push({ role: 'user', content: sprintf("%s\n", intxt) } );
            }

            answer(req);
        }
    }
}

/* Rampart expects module.exports to be the websocket handler function. */
module.exports = chat;
chat._test = { textToolCalls: textToolCalls, pumpAnswer: pumpAnswer, citedSources: citedSources };   /* for tests */
