rampart.globalize(rampart.utils);

var ollamaRunningMessage = "Ollama is running";

var defaultOpts = {
    server:      '127.0.0.1',
    port:        11434,
    serverLog:   '/dev/null',
    errorLog:    '/dev/null',
    startServer: false,
    query:       llmQuery
}

function err(msg) {
    throw new Error("rampart-ollama: " + msg);
}

var ollamaBin = shell("which ollama");
if(ollamaBin.stderr != '') {
    err("Could not find ollama executable");
}
ollamaBin=ollamaBin.stdout.trim();

var curl = require("rampart-curl.so");

function checkIsRunning(url) {
    var res = curl.fetch(url, {maxTime:5});

    if(res.status != 200)
        return false;
    if(res.text != ollamaRunningMessage)
        return false
    return true;
}


function llmQuery(query, callback, finalCallback, ep) {
    var deepseek = this.deepseek;
    var self=this;
    var thinkingtext="";
    var answer="";

    if(deepseek)
        self.thinking=false;

    // allow three params as (query, callback, ep)
    if(!ep && getType(finalCallback)=='String'){
        ep=finalCallback;
        finalCallback=undefined;
    }

    self.cancel=false;

    if(!this.model)
        err("model not set (use llm.model='mymodel' to set)");
    if(/*typeof query != 'string' || */ typeof callback != 'function')
        err("llm.query - invalid parameters: call must be llm.query(queryString, callbackFunction [,finalCallback])");

    if (finalCallback && typeof finalCallback != "function")
        err("llm.query - invalid parameters: finalCallback must be a function: call must be llm.query(queryString, callbackFunction [, finalCallback] [, endPoint])");

    this.fetchError=false;

    postObj={
        model:this.model,
    }

    var endPoint = "generate";
    if(ep) {
        if(ep == "chat")
            endPoint = "chat";
        else if(ep != 'generate')
            err(`llm.query - unknown endPoint "${endPoint}.  Must be "generate" or "chat"`);
    }

    if(typeof query == 'string' && query.length) {
        postObj.prompt=query;
        if(endPoint != 'generate')
            err("llm.query - endPoint 'chat' requires query to be an Array");
    }
    else if(getType(query) == 'Array') {
        postObj.messages=query;
        if(endPoint != 'chat')
            err("llm.query - endPoint 'generate' requires query to be a String");
    } 
    else if(endPoint == "chat")
        err(`llm.query - query is ${getType(query)}, endPoint 'chat' requires query to be an Array`);
    else if (endPoint == 'generate')
        err(`llm.query - query is ${getType(query)}, endPoint 'generate' requires query to be a String`);
        
    
    if(this.params)
        Object.assign(postObj, this.params);

    postObj.stream=true;

    curl.fetchAsync(this.urlbase+'/api/'+endPoint,
        {
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
                    callback(parsed);
                    return false; //don't run any more callbacks
                }
                // empty string is ok.  check for undefined.
                if(parsed.response===undefined && !(parsed.message && parsed.message.content!==undefined))
                {
                    var parsed = {error:"no response or message.content 1", data:rampart.utils.bufferToString(r.body)}
                    callback(parsed);
                    return false;
                }
                var token = parsed.response ? parsed.response :parsed.message.content 

                if(deepseek)
                {
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
                   else
                       answer += token;
                }

                parsed.serverResponse=r;
                parsed.token = token;
                
                if( finalCallback )
                {
                    if(!self.tokens)
                        self.tokens=[];
                    self.tokens.push(token);
                }

                var ret=callback(parsed);
                if(ret===false)
                {
                    self.cancel=false;
                    return curl.cancel;
                }
            }
        },
        // run the final callback when we have all the data
        function(r) {
            if (self.fetchError) {
                self.fetchError.serverResponse=r;
                if(finalCallback)
                    finalCallback(self.fetchError);
                else
                    callback(self.fetchError);
                return;
            }

            if(finalCallback) {
                if(self.tokens) {
                    var resp = {serverResponse:r, fullText: self.tokens.join('')};
                    self.tokens=undefined;
                } else {
                    
                    var jsons = r.text.trim().split('\n');
                    var tokens=[];
                    for (var i=0;i<jsons.length;i++){
                        var j = JSON.parse(jsons[i]);
                        if(j.error) {
                            return {
                                error: "no response or message.content",
                                body: j.error
                            }
                        }
                        tokens.push(j.message.content);
                    }
                    var resp = {serverResponse:r, fullText: tokens.join('')};
                }
                if(deepseek){
                    resp.thinkingText=thinkingtext;
                    resp.answer=answer;
                }
                finalCallback(resp);
            }
        }
    );

}


function llm(opts) {
    if(!opts) opts={};

    Object.assign(this, defaultOpts, opts);

    if(typeof this.server != 'string')
        err("option 'server' must be a string (host name or ip)");

    if(typeof this.port != 'number')
        err("option 'port' must be a number");

    this.urlbase = `http://${this.server}:${this.port}`;
    
    if( !checkIsRunning(this.urlbase + '/') ) {
        //start ollama
        if(this.startServer) {
            
            if(typeof this.serverLog != 'string')
                err("option 'serverLog' must be a string (path to ollama server output log file)");

            var shcmd = `nohup ${ollamaBin} serve >> ${this.serverLog} 2>> ${this.errorLog}`;
            shell(shcmd,
                    {
                        background:true,
                        env: {'OLLAMA_HOST':`${this.server}:${this.port}`},
                        appendEnv:true
                    }
            );

            sleep(1);

            if( !checkIsRunning(this.urlbase + '/') )
                err(`ollama server at ${this.urlbase} failed to start`);

        } else {
            err(`ollama server at ${this.urlbase} doesn't appear to be running`);
        }
    }

}

module.exports = {llm:llm};
