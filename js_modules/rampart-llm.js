rampart.globalize(rampart.utils);


function err(msg) {
    throw new Error("rampart-llm: " + msg);
}

var curl = require("rampart-curl.so");

function checkIsRunning(url) {
    var res = curl.fetch(url, {maxTime:5});

    if(res.status != 200)
        return false;

    return true;
}

/* *********************  ollama ************************** */

var ollamaDefaultOpts = {
    server:      '127.0.0.1',
    port:        11434,
    query:       ollamaQuery
}

function ollamaQuery(query, callback, finalCallback, ep) {
    var self=this;
    var thinkingtext="";
    var answer="";

    self.thinking=false;

    // allow three params as (query, callback, ep)
    if(!ep && getType(finalCallback)=='String'){
        ep=finalCallback;
        finalCallback=undefined;
    }

    self.cancel=false;

    if(!this.model)
        err("model not set (use ollama.model='mymodel' to set)");
    if( callback && typeof callback != 'function')
        err("ollama.query - invalid parameters: call must be ollama.query(queryString, [callbackFunction||null] [,finalCallback])");

    if (finalCallback && typeof finalCallback != "function")
        err("ollama.query - invalid parameters: finalCallback must be a function: call must be ollama.query(queryString, [callbackFunction||null] [, finalCallback] [, endPoint])");

    if(!callback && !finalCallback)
        err("ollama.query - invalid setup: at least one callback must be provided ollama.query(queryString, [callbackFunction||null] [, finalCallback] [, endPoint])");

    this.fetchError=false;

    postObj={
        model: this.model,
    }

    var endPoint = "/api/chat";
    if(ep) {
        endPoint = ep;
    }

    if(typeof query == 'string' && query.length) {
        postObj.prompt=query;
        if(!ep)
            endPoint = '/api/generate';
    }
    else if(getType(query) == 'Array') {
        postObj.messages=query;
    } 

    function chunkcb(content) {
        if(callback)
            return callback(content);
        if(content.error)
            fprintf(stderr, 'error and no callback in rampart-llm.js: "%J"\n', content) 
    }

    if(this.params)
        Object.assign(postObj, this.params);

    postObj.stream=true;

    self.tokens=[];
    curl.fetchAsync(this.urlbase + endPoint,
        {
            connectTimeout: 10,
            postJSON: postObj,
            chunkCallback: function(r) {

                if(self.cancel===true)
                {
                    self.cancel=false;
                    return curl.cancel;
                }

                try {
                    var parsed = JSON.parse(r.body);
                } catch(e) {
                    var parsed = {error:e.message, data:rampart.utils.bufferToString(r.body)}
                    chunkcb(parsed);
                    return false; //don't run any more callbacks
                }
                // empty string is ok.  check for undefined.
                if(parsed.response===undefined && !(parsed.message && parsed.message.content!==undefined))
                {
                    var parsed = {
                        error:"no response or message.content", 
                        data:rampart.utils.bufferToString(r.body),
                        serverResponse: r,
                        token: ""
                    }
                    chunkcb(parsed);
                    return false;
                }
                var token = parsed.response ? parsed.response :parsed.message.content 

                 if( finalCallback && token.length )
                     self.tokens.push(token);

                if(token && token.indexOf("<think>") != -1) {
                    self.thinking=true;
                    return;
                }
                else if (token && token.indexOf("</think>") != -1) {
                    self.thinking=false;
                    return;
                }    
                parsed.thinking=self.thinking;

                if(self.thinking)
                    thinkingtext += token;
                else if (thinkingtext)
                    answer += token;

                parsed.serverResponse=r;
                parsed.token = token;
                
                var ret=chunkcb(parsed);
                if(ret===false)
                {
                    self.cancel=false;
                    return curl.cancel;
                }
            }
        },
        // run the final callback when we have all the data
        function(r) {
            callback({done:true, token:'', serverResponse:''});
            if(finalCallback) {
                var resp = {serverResponse:r, fullText: self.tokens.join('')};
                self.tokens=undefined;

                if(thinkingtext) {
                    resp.thinkingText=thinkingtext;
                    resp.answer=answer;
                }
                finalCallback(resp);
            }
        }
    );

}

function ollama(opts) {
    if(!opts) opts={};

    Object.assign(this, ollamaDefaultOpts, opts);

    if(typeof this.server != 'string')
        err("option 'server' must be a string (host name or ip)");

    if(typeof this.port != 'number')
        err("option 'port' must be a number");

    this.urlbase = `http://${this.server}:${this.port}`;
    
    if( !checkIsRunning(this.urlbase + '/') ) {
        err(`ollama server at ${this.urlbase} doesn't appear to be running`);
    }

}

/* *********************  llama.cpp ************************** */

var llamaCppDefaultOpts = {
    server:      '127.0.0.1',
    port:        8080,
    query:       llamaCppQuery
}

function llamaCppQuery(query, callback, finalCallback, ep) {
    var self=this;
    var thinkingtext="";
    var answer="";

    self.thinking=false;

    // allow three params as (query, callback, ep)
    if(!ep && getType(finalCallback)=='String'){
        ep=finalCallback;
        finalCallback=undefined;
    }

    self.cancel=false;

    if(!this.model)
        err("model not set (use llamaCpp.model='mymodel' to set)");
    if( callback && typeof callback != 'function')
        err("llamaCpp.query - invalid parameters: call must be llamaCpp.query(queryString, [callbackFunction||null] [,finalCallback])");

    if (finalCallback && typeof finalCallback != "function")
        err("llamaCpp.query - invalid parameters: finalCallback must be a function: call must be llamaCpp.query(queryString, [callbackFunction||null] [, finalCallback] [, endPoint])");

    if(!callback && !finalCallback)
        err("llamaCpp.query - invalid setup: at least one callback must be provided llamaCpp.query(queryString, [callbackFunction||null] [, finalCallback] [, endPoint])");

    this.fetchError=false;

    postObj={
        model:this.model,
    }

    var endPoint = "/v1/chat/completions";
    if(ep) {
        endPoint = ep;
    }

    if(typeof query == 'string' && query.length) {
        postObj.prompt=query;
        if(!ep)
            endPoint = '/v1/completions';
    }
    else if(getType(query) == 'Array') {
        postObj.messages=query;
    } 


    if(this.params)
        Object.assign(postObj, this.params);

    function chunkcb(content) {
        if(callback)
            return callback(content);
        if(content.error)
            fprintf(stderr, 'error and no callback in rampart-llm.js: "%J"\n', content) 
    }

    postObj.stream=true;
    /*
        data: {"choices":[{"finish_reason":null,"index":0,"delta":{"content":"."}}],"created":1756597116,"id":"chatcmpl-abvobwHo7AhZvUHU36sQ5ZV6Anjok43F","model":"qwen2.5-32b-instruct-q5_k_m.gguf","system_fingerprint":"b6290-a6a58d64","object":"chat.completion.chunk"}

        data: {"choices":[{"finish_reason":"stop","index":0,"delta":{}}],"created":1756597116,"id":"chatcmpl-abvobwHo7AhZvUHU36sQ5ZV6Anjok43F","model":"qwen2.5-32b-instruct-q5_k_m.gguf","system_fingerprint":"b6290-a6a58d64","object":"chat.completion.chunk"}

        data: {"choices":[],"created":1756597116,"id":"chatcmpl-abvobwHo7AhZvUHU36sQ5ZV6Anjok43F","model":"qwen2.5-32b-instruct-q5_k_m.gguf","system_fingerprint":"b6290-a6a58d64","object":"chat.completion.chunk","usage":{"completion_tokens":22,"prompt_tokens":28,"total_tokens":50},"timings":{"prompt_n":28,"prompt_ms":447.926,"prompt_per_token_ms":15.997357142857142,"prompt_per_second":62.510325366243535,"predicted_n":22,"predicted_ms":1317.711,"predicted_per_token_ms":59.89595454545454,"predicted_per_second":16.69561838673275}}

        data: [DONE]
    */
    self.tokens=[];
    var DONE=true; //make parsing a little easier
    curl.fetchAsync(this.urlbase + endPoint,
        {
            connectTimeout: 10,
            postJSON: postObj,
            chunkCallback: function(r) {
                var parsed;

                if(self.cancel===true)
                {
                    self.cancel=false;
                    return curl.cancel;
                }

                // first, get body as a string
                var btxt = sprintf('%s', r.body);

                // check for data: [DONE]
                if( /\[DONE\]/.test(btxt) )
                {
                    parsed = {
                        serverResponse: r,
                        token: "",
                        done: true
                    }
                    chunkcb(parsed)
                    return;
                }

                try {
                    btxt=btxt.replace('data:', '');
                    //printf("parsing '%s'\n",btxt);
                    parsed = JSON.parse(btxt);
                } catch(e) {
                    parsed = {
                        error:e.message,
                        data:rampart.utils.sprintf("%s", r.body),
                        serverResponse: r,
                        token: ""
                    }
                    chunkcb(parsed);
                    return false; //don't run any more callbacks
                }

                if(typeof parsed != 'object')
                {
                    var parsed = {
                        error:"no data in object",
                        data:rampart.utils.bufferToString(r.body),
                        serverResponse: r,
                        token: ""
                    }
                    //printf("doing err 2: %J\n", parsed);
                    chunkcb(parsed);
                    return false;
                }


                var choice = (getType(parsed.choices)=='Array') ? parsed.choices[0] : null;
                //printf("getType=%s, choice = '%s'\n", getType(parsed.choices), choice);
                // we get choices:[] right after "finish_reason":"stop"
                if(!choice || choice["finish_reason"] == "stop")
                {
                    var parsed = {
                        serverResponse: r,
                        token: ""
                    }
                    chunkcb(parsed);
                }
                var token = ( choice.delta && choice.delta.content )
                    ? choice.delta.content
                    : "";

                //printf("token='%s'\n", token);

                if( finalCallback && token.length )
                    self.tokens.push(token);

                if(token && token.indexOf("<think>") != -1) {
                    self.thinking=true;
                    return;
                }
                else if (token && token.indexOf("</think>") != -1) {
                    self.thinking=false;
                    return;
                }    

                parsed.thinking=self.thinking;

                if(self.thinking)
                    thinkingtext += token;
                else if (thinkingtext)
                    answer += token;

                parsed.serverResponse=r;
                parsed.token = token;
                
                var ret=chunkcb(parsed);
                if(ret===false)
                {
                    self.cancel=false;
                    return curl.cancel;
                }
            }
        },
        // run the final callback when we have all the data
        function(r) {
            if(finalCallback) {
                var resp = {serverResponse:r, fullText: self.tokens.join('')};
                self.tokens=undefined;

                if(thinkingtext) {
                    resp.thinkingText=thinkingtext;
                    resp.answer=answer;
                }
                finalCallback(resp);
            }
        }
    );

}

function llamaCpp(opts) {
    if(!opts) opts={};

    Object.assign(this, llamaCppDefaultOpts, opts);

    if(typeof this.server != 'string')
        err("option 'server' must be a string (host name or ip)");

    if(typeof this.port != 'number')
        err("option 'port' must be a number");

    this.urlbase = `http://${this.server}:${this.port}`;
    
    if( !checkIsRunning(this.urlbase + '/') ) {
        err(`llama.cpp server at ${this.urlbase} doesn't appear to be running`);
    }

}

module.exports = {
    ollama: ollama,
    llamaCpp: llamaCpp
};
