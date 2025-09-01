// Settings:
console.log(window.llmsettings);

var wsapp='/wsapps/llmchat.txt';
var server /* = "example.com" */ ;

var prismCss      = "https://cdnjs.cloudflare.com/ajax/libs/prism/1.29.0/themes/prism-okaidia.min.css";
var prismJs       = "https://cdnjs.cloudflare.com/ajax/libs/prism/1.29.0/prism.min.js";
var prismAutoJs   = "https://cdnjs.cloudflare.com/ajax/libs/prism/1.29.0/plugins/autoloader/prism-autoloader.min.js";
var prismAutoPath = "https://cdnjs.cloudflare.com/ajax/libs/prism/1.29.0/components/";

var katexCss      = "https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css";
var katexJs       = "https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js";
var katexAutoJs  = "https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/contrib/auto-render.min.js";

var prismScripts = [
    {type: "css", loc: prismCss},
    {type: "js",  loc: prismJs},
    {type: "js",  loc: prismAutoJs}
];

var katexScripts = [
    {type: "css", loc: katexCss},
    {type: "js",  loc: katexJs},
    {type: "js",  loc: katexAutoJs}
];

function loadScript(url) {
  return new Promise((resolve) => {
    const script = document.createElement("script");
    script.src = url;
    script.type = "text/javascript";

    script.onload = () => resolve(true);
    script.onerror = () => resolve(false);

    document.head.appendChild(script);
  });
}

function loadCss(url) {
  return new Promise((resolve) => {
    const link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = url;

    link.onload = () => resolve(true);
    link.onerror = () => resolve(false);

    document.head.appendChild(link);
  });
}

/* external js helpers */
async function loadScripts(startfunc) {
  // If you actually needed to defer one tick, do:
  // await Promise.resolve();

  if (!window.Prism) {
    for (const s of prismScripts) {
      const ok = s.type === "css" ? await loadCss(s.loc) : await loadScript(s.loc);
      if (!ok) console.warn(`Failed to load ${s.loc}`);
    }
    if (window.Prism && Prism.plugins?.autoloader) {
      Prism.plugins.autoloader.languages_path = prismAutoPath;
    }
  }

  if (!window.renderMathInElement) {
    for (const s of katexScripts) {
      const ok = s.type === "css" ? await loadCss(s.loc) : await loadScript(s.loc);
      if (!ok) console.warn(`Failed to load ${s.loc}`);
    }
  }

  if (typeof startfunc === "function")
      startfunc();

  return true;
}

// endpoint helper
function makeEndpoint(endpoint, server) {

    if(/wss?:/.test(endpoint))
        return endpoint;

    // protocol
    var ret = 'ws://';

    if (/^https:/.test(window.location.href))
        ret = 'wss://';

    if(endpoint.charAt(0) != '/')
        endpoint = '/' + endpoint;

    if(!server) {
        ret += window.location.host + endpoint;
    } else {
        ret += server + endpoint;
    }
    return ret;
}

/* ================= LLM Renderer ================= */
function createLLMRenderer(container, opts = {}) {
    const root = typeof container === 'string' ?
        document.querySelector(container) :
        container;
    if (!root) throw new Error('Container not found');

    const cfg = {
        inlineCodeClass: opts.inlineCodeClass || 'inline-code',
        codeLangClassPrefix: opts.codeLangClassPrefix || 'language-',
        scrollThreshold: opts.scrollThreshold ?? 24,
        linkTarget: opts.linkTarget ?? '_blank',
        linkRel: opts.linkRel ?? 'noopener noreferrer nofollow',
    };

    // -------- Smart autoscroll --------
    let autoScroll = true;

    const isAtBottom = () =>
        (root.scrollHeight - (root.scrollTop + root.clientHeight)) <=
        cfg.scrollThreshold;

    const maybeScroll = () => {
        if (autoScroll)
            requestAnimationFrame(() => {
                root.scrollTop = root.scrollHeight;
            });
    };

    root.addEventListener('scroll', () => {
        autoScroll = isAtBottom();
    }, {passive: true});

    async function copyNodeTextToClipboard(node) {
        const text = node?.textContent ?? '';
        if (!text) return false;
        try {
            await navigator.clipboard.writeText(text);
            return true;
        } catch {
            const ta = document.createElement('textarea');
            ta.value = text;
            ta.setAttribute('readonly', '');
            ta.style.position = 'fixed';
            ta.style.opacity = '0';
            document.body.appendChild(ta);
            ta.select();
            let ok = false;
            try {
                ok = document.execCommand('copy');
            } finally {
                document.body.removeChild(ta);
            }
            return ok;
        }
    }

    // -------- Rendering state --------
    let block = null, inline = null;
    let inBold = false, inInlineCode = false;
    let inFence = false, collectingFenceLang = false, fenceLang = '';
    let codeEl = null;
    let atLineStart = true, hashCount = 0, headingConfirmed = false;
    let starBuf = '', backtickBuf = '', spaceBuf='';
    let wordBuf = '';  // for URL detection outside of code
    const URL_RE = /^(https?:\/\/[^\s]+|www\.[^\s]+)$/i;
    const TRAIL_PUNCT_RE = /[)\].,!?;:]+$/;
    let nlStreak = 0;               // how many consecutive '\n' we saw
    let pendingParagraphClose = false; // close <p> on next non-newline / new block

    function makeLink(urlText) {
        const a = document.createElement('a');
        let trailing = '';
        const m = urlText.match(TRAIL_PUNCT_RE);
        if (m) {
            trailing = m[0];
            urlText = urlText.slice(0, -trailing.length);
        }
        const href =
            urlText.startsWith('http://') || urlText.startsWith('https://') ?
            urlText :
            'http://' + urlText;
        a.href = href;
        a.textContent = urlText;
        if (cfg.linkTarget) a.target = cfg.linkTarget;
        if (cfg.linkRel) a.rel = cfg.linkRel;
        return {a, trailing};
    }


    function blockParent() {
        if ( !block || 
             block.tagName === 'PRE' ||
             block.tagName.startsWith('H')
        ){
            block = document.createElement('p');
            root.appendChild(block);
        }
        return block;
    }

    function textParent() {
        return inline || blockParent();
    }

    function appendText(node, txt) {
        if (!txt) return;
        const last = node.lastChild;
        if (last && last.nodeType === 3)
            last.nodeValue += txt;
        else
            node.appendChild(document.createTextNode(txt));
    }

    function ensureParagraph() {
        blockParent();
        inline = inline;
    }

    function openHeading(level) {
        block = document.createElement('h' + Math.min(6, Math.max(1, level)));
        root.appendChild(block);
        inline = block;
        headingConfirmed = true;
    }

    function openFence() {
        inFence = true;
        collectingFenceLang = true;
        fenceLang = '';
        const wrapper = document.createElement('div');
        wrapper.className = 'code-block';
        const pre = document.createElement('pre');
        const code = document.createElement('code');
        pre.appendChild(code);
        const copyBtn = document.createElement('button');
        copyBtn.type = 'button';
        copyBtn.className = 'copy-code';
        copyBtn.textContent = 'Copy';
        copyBtn.addEventListener('click', async () => {
            const ok = await copyNodeTextToClipboard(code);
            copyBtn.textContent = ok ? 'Copied!' : 'Copy failed';
            setTimeout(() => (copyBtn.textContent = 'Copy'), 1500);
        });
        wrapper.appendChild(copyBtn);
        wrapper.appendChild(pre);
        root.appendChild(wrapper);
        codeEl = code;
        block = pre;
        atLineStart = false;
    }

    function closeFence() {
        inFence = false;
        collectingFenceLang = false;
        fenceLang = '';
        const doneCode = codeEl;
        codeEl = null;
        block = null;
        atLineStart = true;
        if (window.Prism && doneCode) Prism.highlightElement(doneCode);
        requestAnimationFrame(maybeScroll);
    }

    function flushStarBufAsText() {
        if (!starBuf) return;
        if (inFence) {
            appendText(codeEl, starBuf);
        } else
            appendText(textParent(), starBuf);
        starBuf = '';
    }

    function flushBacktickBufAsText() {
        if (!backtickBuf) return;
        if (inFence)
            appendText(codeEl, backtickBuf);
        else
            appendText(textParent(), backtickBuf);
        backtickBuf = '';
    }

    function flushPendingHeadingHashesAsText() {
        if (hashCount > 0 && !headingConfirmed) {
            appendText(textParent(), '#'.repeat(hashCount));
        }
        hashCount = 0;
        headingConfirmed = false;
    }

    function flushWordBuf() {
        if (!wordBuf) return;
        const parent = textParent();
        if (!inInlineCode && !inFence && URL_RE.test(wordBuf)) {
            const {a, trailing} = makeLink(wordBuf);
            parent.appendChild(a);
            if (trailing) appendText(parent, trailing);
        } else {
            appendText(parent, wordBuf);
        }
        wordBuf = '';
    }

    function toggleInlineCode() {
        flushWordBuf();
        if (!inInlineCode) {
            inInlineCode = true;
            const el = document.createElement('code');
            if (cfg.inlineCodeClass) el.classList.add(cfg.inlineCodeClass);
            blockParent().appendChild(el);
            inline = el;
        } else {
            inInlineCode = false;
            inline = null;
        }
    }

    function toggleBold() {
        flushWordBuf();
        if (!inBold) {
            inBold = true;
            const el = document.createElement('strong');
            blockParent().appendChild(el);
            inline = el;
        } else {
            inBold = false;
            inline = null;
        }
    }

    function renderMathIn(node) {
        if(window.renderMathInElement)
            renderMathInElement(node, {
                delimiters: [
                    {left: '$$', right: '$$', display: true},
                    {left: '\\[', right: '\\]', display: true},
                    {left: '\\(', right: '\\)', display: false}
                ],
                throwOnError: false,
                // Don’t try to typeset inside code blocks, etc.
                ignoredTags: ['script', 'style', 'textarea', 'pre', 'code']
            });
    }

    // this one matches [...](...)  or ![...](...) style
    function checklinks(b) {
        var t = b.innerHTML;
        if (!t) return;

        // first try <img>
        let m = t.match(/\!\[[^\]]+\]\([^\)]+\)/g);
        if(m) {
            var len = m.length, cur=0;
            m.forEach(function(a) {
                var n = a.match(/\!\[([^\]]+)/)[1];
                var l = a.match(/\(([^\)]+)/)[1];
                var img = new Image();
                img.onload = function() {
                    // replace markdown with actual <img>
                    // console.log(a);
                    // console.log("text before:", t);
                    t = t.replace(a, '<img height=30 src="' + l + '" alt="' + n + '">');
                    // console.log("replaced txt:",t);
                    b.innerHTML=t;
                    // after the last, check for regular links.
                    cur++;
                    if(cur == len)
                        checklinks(b);
                };
                img.onerror = function() {
                    // replace markdown with fallback
                    t = t.replace(a, '<span class="img-not-found">[' + n + ' not found]</span>');
                    
                    cur++;
                    if(cur == len)
                        checklinks(b);
                };
                img.src = l;
            })
            return;
        }

        // <a> 
        m = t.match(/\[[^\]]+\]\([^\)]+\)/g);
        if (!m) return;
        m.forEach(function(a) {
            var n = a.match(/\[([^\]]+)/)[1];
            var l = a.match(/\(([^\)]+)/)[1];
            var rep = '<a target=_blank href="' + l + '">' + n + '</a>';
            t = t.replace(a, rep);
        })
        b.innerHTML=t;
    }

    function isWordBoundaryChar(ch) {
        return /\s/.test(ch) || /[(){}\[\]"'<>.,!?;:]/.test(ch);
    }

    function finishParagraph() {
        checklinks(block);
        renderMathIn(block);
        //console.log("End of Paragraph");
    }

    function maybeCloseParagraph() {
      if (pendingParagraphClose) {
        finishParagraph();
        pendingParagraphClose = false;
      }
      nlStreak = 0;
    }

    function newline() {
        flushStarBufAsText();
        flushBacktickBufAsText();
        flushWordBuf();
        if (inFence) {
            if (collectingFenceLang) {
                collectingFenceLang = false;
                const lang = fenceLang.trim();
                if (lang) codeEl.classList.add(cfg.codeLangClassPrefix + lang);
            } else
                appendText(codeEl, '\n');
            atLineStart = true;
            return;
        }
        if (hashCount && !headingConfirmed) flushPendingHeadingHashesAsText();
        if (!block) {
            atLineStart = true;
            nlStreak = 0;
            return;
        }
        if (block.tagName.startsWith('H')) {
            block = null;
            inline = null;
            atLineStart = true;
            return;
        }
        if (block.tagName === 'P') {

            if (nlStreak === 0) {
              // first newline inside a paragraph => soft line break
              block.appendChild(document.createElement("br"));
              nlStreak = 1;
              // check at end of line and again at end of paragraph
              checklinks(block);
              renderMathIn(block);
            } else {
              // second consecutive newline => end paragraph LATER
              pendingParagraphClose = true;  // 👈 don't finish here
              nlStreak = 2;                  // (stay >0 until next non-newline)
            }
        }
        inline = null;
        atLineStart = true;
    }

    function write(token) {
        const s = String(token);
        for (let i = 0; i < s.length; i++) {
            const ch = s[i];
            if (ch === '\n') {
                newline();
                continue;
            }
            maybeCloseParagraph();
            if (inFence) {
                if (collectingFenceLang) {
                    fenceLang += ch;
                    continue;
                }
                if (atLineStart) {
                    if( ch == ' ' ){
                        spaceBuf += ' ';
                        continue;
                    }
                    if (ch === '`') {
                        backtickBuf += '`';
                        if (backtickBuf === '```') {
                            backtickBuf = '';
                            closeFence();
                            continue;
                        }
                        continue;
                    } else if (backtickBuf) {
                        appendText(codeEl, backtickBuf);
                        backtickBuf = '';
                    }
                    if(ch != ' ' && spaceBuf.length) {
                        appendText(codeEl, spaceBuf+ch);
                        spaceBuf='';
                        continue;
                    }
                    if(window.Prism)
                        Prism.highlightElement(codeEl);
                }
                appendText(codeEl, ch);
                if(ch != ' ')
                    atLineStart = false;
                continue;
            }

            if (atLineStart) {
                if (ch === '#') {
                    if (hashCount < 6) {
                        hashCount++;
                    } else {
                        ensureParagraph();
                        appendText(textParent(), ch);
                    }
                    continue;
                }
                if (hashCount && ch === ' ' && !headingConfirmed) {
                    openHeading(hashCount);
                    atLineStart=false;
                    continue;
                }
                if (hashCount && ch !== ' ') {
                    flushPendingHeadingHashesAsText();
                }
                if (ch === '`') {
                    backtickBuf += '`';
                    if (backtickBuf === '```') {
                        backtickBuf = '';
                        openFence();
                        continue;
                    }
                    continue;
                } else if (backtickBuf) {
                    for (let k = 0; k < backtickBuf.length; k++)
                        toggleInlineCode();
                    backtickBuf = '';
                }
            }
            if (ch === '`') {
                flushStarBufAsText();
                flushWordBuf();
                toggleInlineCode();
                atLineStart = false;
                continue;
            }
            if (!inInlineCode && ch === '*') {
                starBuf += '*';
                if (starBuf === '**') {
                    starBuf = '';
                    toggleBold();
                    atLineStart = false;
                    continue;
                }
                continue;
            } else if (starBuf) {
                appendText(textParent(), starBuf);
                starBuf = '';
            }
            if (inInlineCode) {
                appendText(textParent(), ch);
            } else {
                if (isWordBoundaryChar(ch)) {
                    flushWordBuf();
                    appendText(textParent(), ch);
                } else {
                    wordBuf += ch;
                }
            }
            if(ch != ' ') //sometimes we get "\n   ```lang ... ```
                atLineStart = false;
        }
        maybeScroll();
    }

    function writeRaw(html) {
        root.insertAdjacentHTML('beforeend', String(html));
        block = null;
        inline = null;
        inBold = false;
        inInlineCode = false;
        inFence = false;
        collectingFenceLang = false;
        fenceLang = '';
        codeEl = null;
        atLineStart = true;
        hashCount = 0;
        headingConfirmed = false;
        starBuf = '';
        backtickBuf = '';
        wordBuf = '';
        requestAnimationFrame(maybeScroll);
    }

    function end() {
        flushStarBufAsText();
        if (backtickBuf) {
            for (let k = 0; k < backtickBuf.length; k++) toggleInlineCode();
            backtickBuf = '';
        }
        flushWordBuf();
        if (hashCount && !headingConfirmed) flushPendingHeadingHashesAsText();

    }

    return {write, end, writeRaw};
}

/* ================= Conversation Storage Helpers ================= */
var STORAGE_KEY = '_conversations';

function loadAllConversations() {
    try {
        console.log(STORAGE_KEY);
        const raw = localStorage.getItem(STORAGE_KEY);
        if (!raw) return [];
        const arr = JSON.parse(raw);
        if (Array.isArray(arr)) return arr;
        return [];
    } catch (e) {
        console.warn('Failed to parse conversations', e);
        return [];
    }
}

function saveAllConversations(conversations) {
    try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(conversations));
    } catch (e) {
        console.warn('Failed to save conversations', e);
    }
}

function createConversationObject() {
    const id = 'conv_' + Date.now();
    const ts = new Date().toISOString();
    return {id, title: 'New conversation', createdAt: ts, messages: []};
}

function addTitle(text) {
    const t = (text || '').trim().replace(/\s+/g, ' ');
    if (!t) return 'New conversation';
    //return (t.length > 40 ? t.slice(0, 37) + '…' : t);
    return t;
}

function findConversation(conversations, id) {
    return conversations.find(c => c.id === id);
}

function upsertConversation(conversations, conv) {
    const idx = conversations.findIndex(c => c.id === conv.id);
    if (idx === -1)
        conversations.unshift(conv);
    else
        conversations[idx] = conv;
    return conversations;
}

/* ================= UI & App ================= */
document.addEventListener("DOMContentLoaded", function() {

    if( llmSettings && llmSettings.model)
        STORAGE_KEY = llmSettings.model + STORAGE_KEY;
    else
        STORAGE_KEY = "default" + STORAGE_KEY;

    if(llmSettings && typeof llmSettings=='object'){
        if(llmSettings.endpoint) {
            wsapp=llmSettings.endpoint;
            delete llmSettings.endpoint;
        }
        if(llmSettings.server) {
            server=llmSettings.server;
            delete llmSettings.server;
        }
        if(Object.keys(llmSettings).length) {
            llmSettings=JSON.stringify(llmSettings);
        } else {
            llmSettings=null;
        }
    }

    var socket;
    var reconnected = false;
    const cd = document.getElementById("chatdiv");

    if(!cd)
        throw new Error("#chatdiv not found");

    var renderer = createLLMRenderer('#chatdiv');
    var endpoint = makeEndpoint(wsapp,server);

    console.log(endpoint);

    if(llmSettings)
        endpoint += '?s=' + llmSettings;
    if(endpoint.length > 8000)
        throw new Error("JSON llmSettings is too large");

    // sidebar state
    let conversations = loadAllConversations();
    let currentConv = conversations[0] || createConversationObject();
    if (!conversations.length) {
        conversations = upsertConversation(conversations, currentConv);
        saveAllConversations(conversations);
    }
    let assistantBuffer = '';  // collects streaming assistant text until end
    let sendContextNext = false;  // on first send after switch/load, include {q, context}
    if (currentConv.messages && currentConv.messages.length) {
        sendContextNext = true;
    }

    /* ---------- Sidebar rendering ---------- */
    function renderSidebar() {
        const ul = document.getElementById('convlist');
        ul.innerHTML = '';
        conversations.forEach(conv => {
            const li = document.createElement('li');
            li.className = conv.id === currentConv.id ? 'active' : '';
            const title = document.createElement('div');
            title.classList.add('ctitle');
            title.textContent = conv.title || 'Conversation';
            const sub = document.createElement('span');
            sub.className = 'subtitle';
            const d = new Date(conv.createdAt || Date.now());
            sub.textContent = d.toLocaleString();
            li.appendChild(title);
            li.appendChild(sub);
            li.addEventListener('click', () => {
                loadConversation(conv.id);
            });
            ul.appendChild(li);
        });
    }

    function loadConversation(id) {
        const conv = findConversation(conversations, id);
        if (!conv) return;
        currentConv = conv;
        sendContextNext = true;
        cd.innerHTML = "";
        assistantBuffer = '';
        // replay messages
        conv.messages.forEach(m => {
            if (m.role === 'user') {
                renderer.writeRaw(
                    '<span class="userdata">' +
                    (m.content || '') + '</span><br>');
            } else if (m.role === 'assistant') {
                renderer.write(String(m.content || ''));
            }
        });
        renderSidebar();
    }

    function newConversation() {
        currentConv = createConversationObject();
        sendContextNext = false;
        conversations = upsertConversation(conversations, currentConv);
        saveAllConversations(conversations);
        cd.innerHTML = "";
        assistantBuffer = '';
        renderSidebar();
    }

    function persist() {
        conversations = upsertConversation(conversations, currentConv);
        saveAllConversations(conversations);
        renderSidebar();
    }


    /* ---------- Chat plumbing ---------- */
    function isOpen(ws) {
        return ws && ws.readyState === ws.OPEN;
    }

    function showMessage(data) {
        if (typeof data == 'object') {
            if(data.msg) {
                renderer.writeRaw(`<span class="userdata">${data.msg}</span>`);
            } else if (data.update) {
                document.getElementById("status").innerHTML = data.update;
            } else if (data.srvmsg !== undefined ) { //it can be an empty string
                document.getElementById("msg").innerHTML = data.srvmsg;
            } else if (data.end) {
                // final HTML chunk from server is written raw
                renderer.writeRaw(data.end);
                // store assistant response using accumulated assistantBuffer
                if (assistantBuffer.trim().length) {
                    currentConv.messages.push(
                        {role: 'assistant', content: assistantBuffer});
                    assistantBuffer = '';
                    persist();
                }
                renderer.end();
            } else if (data.title) {
                currentConv.title = addTitle(data.title);
                currentConv.createdAt = new Date().toISOString();
                persist();
            } else if (data.error) {
                document.getElementById("msg").innerHTML = `<span style="color:red;font-size:12px;">${data.error}</span>`;
            } else {
                console.log("unhandled",data)
            }
        } else {
            // token stream (string/chunk)
            const token = String(data);
            assistantBuffer += token;
            renderer.write(token);
        }
    }

    function procmess(msg) {
        var data;
        try {
            data = JSON.parse(msg.data);
            if (typeof data != 'object')
                data = msg.data;
        } catch (e) {
            data = msg.data;
        }
        if (reconnected) {
            reconnected = false;
            return;
        }
        if (data) {
            if (data.file)
                window.ExpectedFileData =
                    (window.ExpectedFileData || []).concat([data]);
            else
                showMessage(data);
        }
    }

    function send() {
        const text = document.getElementById("chatin").value;
        if (text == '') {
            return;
        }

        var data = {msg: text};

        // Decide payload: continuing a saved conversation? send {q, context}
        var shouldSendContext = !!(
            sendContextNext &&
            currentConv &&
            Array.isArray(currentConv.messages) && 
            currentConv.messages.length
        );
        var payload = shouldSendContext ?
            JSON.stringify({q: text, context: currentConv.messages}) :
            text;

        /* Update title if this is the first user message
        if (!currentConv.messages.length) {
            currentConv.title = summarizeTitleFromFirstUser(text);
            currentConv.createdAt = new Date().toISOString();
        }
        */

        // Store user message immediately
        currentConv.messages.push({role: 'user', content: text});
        persist();

        const chatin = document.getElementById("chatin");
        try {
            if (!isOpen(socket) && !reconnected) {
                socket = new WebSocket(endpoint);
                socket.addEventListener('open', function(e) {
                    payload = JSON.stringify({q: text, context: currentConv.messages})
                    socket.send(payload);
                    if (shouldSendContext) {
                        sendContextNext = false;
                    }
                    reconnected = true;
                    chatin.value = "";
                    chatin.style.height = "auto";
                    showMessage(data);
                    socket.onmessage = procmess;
                });
                return;
            }
            socket.send(payload);
            if (shouldSendContext) {
                sendContextNext = false;
            }
            showMessage(data);
        } catch (e) {
            showMessage({from: 'System', msg: 'error sending message'});
        }
        chatin.value = "";
        chatin.style.height = "auto";
    }

    document.getElementById("cancel").addEventListener("click", () => {
        if (isOpen(socket)) {
            socket.send('{"cmd":"cancel"}');
        }
        const chatin = document.getElementById("chatin");
        chatin.value = "";
        chatin.style.height = "auto";
    });

    document.getElementById("reset").addEventListener("click", () => {
        if (isOpen(socket)) {
            socket.send('{"cmd":"reset"}');
        }
        newConversation();
    });

    function start() {
        if (socket) socket.close();
        socket = new WebSocket(endpoint);
        socket.onmessage = procmess;
    }
    // chatin input / keyup
    document.getElementById("chatin").addEventListener("input", resizeChatin);
    document.getElementById("chatin").addEventListener("keyup", function(event) {
        this.style.height = "auto";
        if (event.key === "Enter" && !event.shiftKey) {
            event.preventDefault();
            send();
        } else {
            this.style.height = this.scrollHeight + "px";
        }
    });

    function resizeChatin(e) {
        e.target.style.height = "auto";
        e.target.style.height = e.target.scrollHeight + "px";
    }

    // submit button
    document.getElementById("submit").addEventListener("click", () => {
        send();
    });

    // new conversation
    document.getElementById("newconv").addEventListener("click", () => {
        if (isOpen(socket)) {
            socket.send('{"cmd":"reset"}');
        }
        newConversation();
    });

    // delete conversation
    document.getElementById("deleteconv").addEventListener("click", () => {
        conversations = conversations.filter(c => c.id !== currentConv.id);
        if (!conversations.length) {
            conversations = [createConversationObject()];
        }
        currentConv = conversations[0];
        saveAllConversations(conversations);
        cd.innerHTML = "";
        assistantBuffer = "";
        loadConversation(currentConv.id);
    });

    renderSidebar();
    // load current (first) conversation view
    loadConversation(currentConv.id);
    // load Prism and katex, then run start();
    loadScripts(start);
});
