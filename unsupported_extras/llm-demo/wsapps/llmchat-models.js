
var qcPrompt = `You are a concise helpful assistant.`;

var server = "127.0.0.1";

var qcCoderPrompt = `You are a highly capable AI coding assistant. 
- Write clear, correct, and efficient code. 
- Always explain your reasoning and provide helpful context. 
- When showing code, use proper syntax highlighting and complete, working examples. 
- Follow best practices for readability, maintainability, and performance. 
- If the user’s request is ambiguous, ask clarifying questions before answering. 
- Be concise, but include enough detail for the user to understand and apply the solution.
`;

var models = {
    qwen: {
        sysPrompt:   qcPrompt,
        /* llama-server only uses the model it was loaded with, so this is a no-op.  
           If you want several models, run several llama-server's on different ports */
        model:       'qwen2.5-32b-instruct-q5_k_m.gguf',
        server:      server,                             // default is 127.0.0.1 if omitted
        params:      {temperature: 0.2},
        engine:      'llamaCpp',
        port:        8080                                // default if omitted.
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
