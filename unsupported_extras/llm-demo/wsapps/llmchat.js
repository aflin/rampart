rampart.globalize(rampart.utils);

var llm=require("rampart-llm.js");


// have llm get us an appropriate short title for this conversation
function makeTitle(req) {
    if(req.titled)
        return;

    req.titled=true;

    var mainPrompt = req.prompt;

    // system, user, assistant should be in the first 3
    if(!mainPrompt || getType(mainPrompt) !== 'Array' || mainPrompt.length < 3) 
        return;

    var userInst = "Below is the beginning of a conversation.  Please make a four to seven word title that best summarizes this conversation.\n";
    var userQ = sprintf("%s\n%s\n%s\n", userInst, mainPrompt[1].content, mainPrompt[2].content);
    
    var prompt = [
        { role: 'system', content: "You are an AI agent.  Your task is to create a title for a conversation."},
        { role: 'user', content:  userQ}
    ];

    req.llama.query(
        prompt,
        null,  /* no need for per token callback. */
        function(res) {    
            req.wsSend({title: res.fullText});
        }
    );
    
}

// available at ws(s)://example.com/wsapps/llmchat.txt
/* Called each time new data is sent.
   for every call, req is the same object,
   only req.body changes (text/binary message from client) */
function chat(req) {

    /* first connection.  This is the handshake and upgrade to ws.
       There's no req.body. Here we set up the llama connection        */
    if( req.count==0) {
        /* models are defined here  */
        var models = require("llmchat-models.js");
        var modelObj;

        var settings;
        if(req.params.s) {
            try {
                settings = req.params.s;
            } catch(e) {
                req.wsSend( { error: `invalid JSON: ${req.params.s}`} );
                req.wsEnd();
            }
        }

        if(settings && settings.model) {
            modelObj = models[settings.model]
        } else {
            modelObj = models[models.defaultModel];
        }

        if(!modelObj) {
            req.wsSend({error: "model not found"});
            req.wsEnd();
        }
        var opts = {};
        if(modelObj.server)
            opts.server=modelObj.server;
        if(modelObj.port)
            opts.port=modelObj.port;

        /* get new connection and save it in the req object
           so we have access to it in subsequent rounds     */
        try {
            if( modelObj.engine == "llamaCpp" ) {
                req.llama = new llm.llamaCpp(opts);
            } else if ( modelObj.engine == "ollama" )
                req.llama = new llm.ollama(opts);
            else {
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

        req.sysPrompt = modelObj.sysPrompt

        if(typeof modelObj.params)
            req.llama.params=modelObj.params;

        return;
    }

    /* any subsequent message after initial connection above */
    var llama=req.llama;

    if(req.body.length) {

        var cmdobj;
        var intxt=req.body;

        /* did we get plain text or some json? */
        try {
            cmdobj=JSON.parse(intxt);
            if(cmdobj.cmd == "cancel" && req.processing) {

                /* llama.cancel=true will signal rampart-llama to interrupt the llama server */
                req.llama.cancel=true;
                req.processing=false;
                req.wsSend("<br><hr><br>");
                return;

            } else if (cmdobj.cmd == "reset") {

                /* got a reset request */
                if(req.processing)
                {
                    req.llama.cancel=true;
                    req.processing=false;
                }
                /* erase the history of this conversation */
                req.prompt=false;
                req.titled=false;
                return;

            } else if (cmdobj.context && cmdobj.q) {

                /* continuing an existing conversation stored in browser */
                req.prompt=cmdobj.context;
                req.prompt.unshift({ role: 'system', content: req.sysPrompt});
                intxt=cmdobj.q;
                if(req.prompt.length < 3)
                    req.titled=false;
                else
                    req.titled=true;
                /* no return, continue below */
            }
        } catch(e) {}

        if (!req.processing) {
            req.processing=true;

            if(!req.prompt) {
                /* initial prompt */
                req.prompt = [
                    { role: 'system', content: req.sysPrompt},
                    { role: 'user', content: sprintf("%s\n", intxt) }
                ];
                req.titled=false;
            } else {
                /* follow up prompts, add and send it all back to llama */
                req.prompt.push({ role: 'user', content: sprintf("%s\n", intxt) } );
            }

            req.wsSend({srvmsg: "thinking..."});
            req.thinking=true;
            /* send the llm the prompt with llama.query(prompt, perTokenCallback, finalCallback)
               call perTokenCallback function for every token
               call finalCallback function at end of response
            */
            llama.query(    
                req.prompt,

                /* per token callback */
                function(res) {
                    if(req.thinking) {
                        req.wsSend({srvmsg: "answering..."});
                        req.thinking=false;
                    }
                    if(res.done) {
                        req.wsSend({end:"<br><hr><br>"});
                        req.processing=false;
                    }
                    else if(! req.llama.cancel )
                    {
                        if(res.token) {
                            req.wsSend(res.token); 
                        }
                    }

                },

                /* end of chat response */
                function(res) {
                    /* check for errors in res.serverResponse. */
                    if (res.serverResponse.status != 200) {
                        req.wsSend({srvmsg: `${res.serverResponse.status}: ${res.serverResponse.text}`});
                        return;
                    }
                    /* save our last full response from the llm */
                    req.prompt.push( { role: "assistant", content: res.fullText} );
                    if(!req.titled)
                        makeTitle(req);
                    req.wsSend({srvmsg: ""});
                }
            ); // llama.query
        }      // req.processing
    }          // body.length
}

module.exports = chat;
