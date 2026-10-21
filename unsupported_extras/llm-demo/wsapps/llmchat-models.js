/* Model configuration for llmchat.js — general-purpose chat assistant.

   This file defines the LLM backend(s) available to the chat demo.
   The chat page (chat.html or codechat.html) sets llmSettings.model to
   select which model to use. The client passes this as a query-string parameter
   on the websocket URL (?s={"model":"qwen"}), and llmchat.js reads
   it from req.params.s to look up the matching entry here.

   Fields per model entry:
     sysPrompt  – system message prepended to every conversation.
                  Defines the assistant's personality and formatting rules.
                  llmchat.js appends today's date to it.
     model      – model name sent in the request.  A server that loads
                  one model (llama-server) ignores it.
     server     – hostname/IP of the LLM server (default 127.0.0.1)
     port       – port of the LLM server (default 8080 for llamaCpp, 11434 for ollama)
     engine     – "llamaCpp" or "ollama" — both use the same .query() API
                  via rampart-llm.js, just different default ports and endpoints.
     params     – sampling parameters merged into the /v1/chat/completions POST body:
                  temperature, max_tokens, top_p, top_k, etc.
     tools      – (optional) settings for the tools the model may call
                  (see llmchat-tools.js).  Omit to chat without tools.
                    web         true to offer web_search and web_fetch
                    maxRounds   most tool-calling rounds per question
                    log         true to log each tool call to logs/tools.log
                  The model needs a server and chat template that support
                  OpenAI-style tool calls (llama-server --jinja, vLLM, ollama).
*/

/* System prompt for the general chat assistant. */
var qcPrompt = `You are a helpful assistant. Be clear and well-organized.
Use markdown formatting: headings for sections, **bold** for emphasis,
numbered or bulleted lists where appropriate, and fenced code blocks
with the language name (e.g. \`\`\`python) for any code.

When you have search tools, use them for facts about people, places,
history, science and current events, read pages or articles when the
search results are not enough, and base factual answers on what the
tools return, citing the sources you used with their URLs.  Answer
directly, without tools, for coding, math, writing and conversation.`;

/* System prompt for the coding assistant (codechat.html). */
var qcCoderPrompt = `You are a highly capable AI coding assistant.
- Write clear, correct, and efficient code.
- Always explain your reasoning and provide helpful context.
- When showing code, use proper syntax highlighting and complete, working examples.
- Follow best practices for readability, maintainability, and performance.
- If the user's request is ambiguous, ask clarifying questions before answering.
- Be concise, but include enough detail for the user to understand and apply the solution.`;

/* where the LLM server runs */
var server = "127.0.0.1";

/* Tools for the chat model: web search and page/document fetching, which
   need only internet access. */
var tools = {
    web:        true,
    maxRounds:  6,
    log:        false
};

var models = {
    qwen: {
        sysPrompt:   qcPrompt,
        /* llama-server only uses the model it was loaded with, so this is a no-op.
           If you want several models, run several llama-server's on different ports */
        model:       'qwen2.5-32b-instruct-q5_k_m.gguf',
        server:      server,                             // default is 127.0.0.1 if omitted
        params:      {temperature: 0.3, max_tokens: 8192},
        engine:      'llamaCpp',
        port:        8080,                               // default if omitted.
        tools:       tools
    },

    qwenCoder: {
        sysPrompt:   qcCoderPrompt,
        model:       'hf.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF:latest',
        server:      server,                             // default is 127.0.0.1 if omitted
        params:      {temperature: 0.2},
        engine:      'ollama',
        port:        11434                               // default if omitted.
    },

    defaultModel:    "qwen"
}

module.exports = models;
