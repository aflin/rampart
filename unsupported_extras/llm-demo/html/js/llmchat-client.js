// Settings:
//console.log(window.llmsettings);

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
    // Only disable auto-scroll when the USER actively scrolls away from bottom.
    // Content growth alone never disables it.
    let userScrolledUp = false;
    let _lastAutoScrollTime = 0;

    const isNearBottom = () =>
        (root.scrollHeight - (root.scrollTop + root.clientHeight)) <= 100;

    const maybeScroll = () => {
        if (!userScrolledUp) {
            _lastAutoScrollTime = Date.now();
            requestAnimationFrame(() => {
                root.scrollTop = root.scrollHeight;
            });
        }
    };

    const forceScroll = () => {
        userScrolledUp = false;
        root.scrollTop = root.scrollHeight;
    };

    root.addEventListener('scroll', () => {
        if (isNearBottom()) {
            userScrolledUp = false;
        } else if (Date.now() - _lastAutoScrollTime > 200) {
            // not near bottom and no recent programmatic scroll
            // — must be user-initiated (wheel, scrollbar, touch, keyboard)
            userScrolledUp = true;
        }
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
    let inBold = false, inItalic = false, inInlineCode = false;
    let inFence = false, collectingFenceLang = false, fenceLang = '';
    let codeEl = null;
    let atLineStart = true, hashCount = 0, headingConfirmed = false;
    let starBuf = '', backtickBuf = '', spaceBuf='';
    let wordBuf = '';  // for URL detection outside of code
    let listBuf = '';  // buffer for detecting list markers at line start
    let currentList = null;  // current <ul> or <ol> element
    let listStack = [];  // stack of parent lists for nesting (OL inside UL, etc.)
    const URL_RE = /^(https?:\/\/[^\s]+|www\.[^\s]+)$/i;
    const TRAIL_PUNCT_RE = /[)\].,!?;:]+$/;
    let nlStreak = 0;               // how many consecutive '\n' we saw
    let pendingParagraphClose = false; // close <p> on next non-newline / new block
    let thinkDetails = null;  // current <details> element for thinking
    let thinkContent = null;  // <div> inside details for thinking text
    let inTable = false;      // currently inside a table
    let tableEl = null;       // <table> element
    let tbodyEl = null;       // <tbody> element (created after separator row)
    let tableBuf = '';        // buffering current table row text
    let tableSeparatorSeen = false;  // have we seen |---|---| row

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
        if ( !block || block.tagName === 'PRE' ){
            block = document.createElement('p');
            root.appendChild(block);
        }
        return block;
    }

    function closeList() {
        if (currentList) {
            // process links in the last LI before closing
            if (block && block.tagName === 'LI') checklinks(block);
            // find the top-level list to render math on
            var topList = listStack.length ? listStack[0] : currentList;
            renderMathIn(topList);
            currentList = null;
            listStack = [];
        }
    }

    function closeTable() {
        if (tableEl) {
            renderMathIn(tableEl);
            tableEl = null;
            tbodyEl = null;
            inTable = false;
            tableBuf = '';
            tableSeparatorSeen = false;
            block = null;
            inline = null;
        }
    }

    function parseTableCells(line) {
        var cells = line.split('|');
        // remove empty first/last from leading/trailing |
        if (cells.length && cells[0].trim() === '') cells.shift();
        if (cells.length && cells[cells.length - 1].trim() === '') cells.pop();
        return cells.map(function(c) { return c.trim(); });
    }

    function renderCellContent(text) {
        var h = escHtml(text);
        h = h.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
        h = h.replace(/\*(.+?)\*/g, '<em>$1</em>');
        h = h.replace(/`(.+?)`/g, '<code>$1</code>');
        return h;
    }

    function addTableRow(line) {
        var cells = parseTableCells(line);
        if (!cells.length) return;

        // separator row: |---|:---:|---:|
        if (cells.every(function(c) { return /^:?-+:?$/.test(c); })) {
            if (tableEl && !tableSeparatorSeen) {
                // promote existing rows to thead with <th>
                var thead = document.createElement('thead');
                var rows = tableEl.querySelectorAll('tr');
                rows.forEach(function(tr) {
                    var newTr = document.createElement('tr');
                    tr.querySelectorAll('td').forEach(function(td) {
                        var th = document.createElement('th');
                        th.innerHTML = td.innerHTML;
                        newTr.appendChild(th);
                    });
                    thead.appendChild(newTr);
                    tr.remove();
                });
                tableEl.insertBefore(thead, tableEl.firstChild);
                tbodyEl = document.createElement('tbody');
                tableEl.appendChild(tbodyEl);
                tableSeparatorSeen = true;
            }
            return;
        }

        // create table on first data row
        if (!tableEl) {
            tableEl = document.createElement('table');
            root.appendChild(tableEl);
        }

        var tr = document.createElement('tr');
        cells.forEach(function(cellText) {
            var td = document.createElement('td');
            td.innerHTML = renderCellContent(cellText);
            tr.appendChild(td);
        });

        (tbodyEl || tableEl).appendChild(tr);
    }

    function openListItem(type) {
        // close current block if needed
        if (block && (block.tagName === 'P' || block.tagName === 'LI')) {
            if (block.tagName === 'P') finishParagraph();
            else if (block.tagName === 'LI') { checklinks(block); renderMathIn(block); }
            block = null;
            inline = null;
        }

        if (!currentList) {
            // no active list — create a new one at root level
            currentList = document.createElement(type);
            root.appendChild(currentList);
        } else if (currentList.tagName !== type) {
            // different list type — check if a parent list matches
            var foundIdx = -1;
            for (var s = listStack.length - 1; s >= 0; s--) {
                if (listStack[s].tagName === type) {
                    foundIdx = s;
                    break;
                }
            }
            if (foundIdx >= 0) {
                // pop back to the matching parent list
                currentList = listStack[foundIdx];
                listStack = listStack.slice(0, foundIdx);
            } else {
                // nest a new sub-list inside the current LI
                listStack.push(currentList);
                var parentLI = currentList.lastElementChild;
                var sub = document.createElement(type);
                if (parentLI)
                    parentLI.appendChild(sub);
                else
                    root.appendChild(sub);
                currentList = sub;
            }
        }
        // same type (or resumed parent) — append new item
        var li = document.createElement('li');
        currentList.appendChild(li);
        block = li;
        inline = null;
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

    function toggleItalic() {
        flushWordBuf();
        if (!inItalic) {
            inItalic = true;
            const el = document.createElement('em');
            blockParent().appendChild(el);
            inline = el;
        } else {
            inItalic = false;
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
                    {left: '\\(', right: '\\)', display: false},
                    {left: '$', right: '$', display: false}
                ],
                throwOnError: false,
                // Don’t try to typeset inside code blocks, etc.
                ignoredTags: ['script', 'style', 'textarea', 'pre', 'code']
            });
    }

    // this one matches [...](...)  or ![...](...) style
    function escHtml(s) {
        return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
    }

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
                    t = t.replace(a, '<img height=30 src="' + escHtml(l) + '" alt="' + escHtml(n) + '">');
                    b.innerHTML=t;
                    cur++;
                    if(cur == len)
                        checklinks(b);
                };
                img.onerror = function() {
                    t = t.replace(a, '<span class="img-not-found">[' + escHtml(n) + ' not found]</span>');
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
        if (m) {
            m.forEach(function(a) {
                var n = a.match(/\[([^\]]+)/)[1];
                var l = a.match(/\(([^\)]+)/)[1];
                var rep = '<a target=_blank href="' + escHtml(l) + '">' + escHtml(n) + '</a>';
                t = t.replace(a, rep);
            })
            b.innerHTML=t;
        }
        autolink(b);
    }

    /* bare http(s) URLs in text become links; text already inside a link
       or code is left alone, and trailing punctuation stays outside */
    function autolink(node) {
        var kids = Array.prototype.slice.call(node.childNodes);
        kids.forEach(function(k) {
            if (k.nodeType === 1) {
                if (!/^(A|CODE|PRE)$/.test(k.tagName)) autolink(k);
                return;
            }
            if (k.nodeType !== 3 || !/https?:\/\//.test(k.nodeValue)) return;
            var s = k.nodeValue, re = /https?:\/\/[^\s<>"'`]+/g, m, last = 0;
            var frag = document.createDocumentFragment();
            while ((m = re.exec(s)) !== null) {
                var url = m[0];
                // drop trailing punctuation, and a ")" the URL did not open
                while (/[.,;:!?'"\]]$/.test(url) ||
                       (/\)$/.test(url) && (url.match(/\(/g) || []).length < (url.match(/\)/g) || []).length))
                    url = url.slice(0, -1);
                frag.appendChild(document.createTextNode(s.slice(last, m.index)));
                var a = document.createElement('a');
                a.href = url;
                a.target = '_blank';
                a.rel = 'noopener noreferrer';
                a.textContent = url;
                frag.appendChild(a);
                last = m.index + url.length;
                re.lastIndex = last;
            }
            frag.appendChild(document.createTextNode(s.slice(last)));
            node.replaceChild(frag, k);
        });
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
        // table row ends on newline
        if (inTable) {
            var row = tableBuf.trim();
            tableBuf = '';
            if (row)
                addTableRow(row);
            else
                closeTable();
            atLineStart = true;
            return;
        }
        flushStarBufAsText();
        flushBacktickBufAsText();
        flushWordBuf();
        // check for horizontal rule: ---, ***, ___ (3+ chars, same char)
        if (/^[-*_]{3,}$/.test(listBuf)) {
            closeList();
            if (block && block.tagName === 'P') finishParagraph();
            block = null;
            inline = null;
            root.appendChild(document.createElement('hr'));
            listBuf = '';
            atLineStart = true;
            nlStreak = 0;
            return;
        }
        listBuf = '';
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
            hashCount = 0;
            headingConfirmed = false;
            atLineStart = true;
            return;
        }
        if (block && block.tagName === 'LI') {
            // single newline inside a list item — soft break, keep appending to this LI
            block.appendChild(document.createElement("br"));
            inline = null;
            atLineStart = true;
            nlStreak = 0;
            return;
        }
        if (block.tagName === 'P') {

            if (nlStreak === 0) {
              // first newline inside a paragraph => soft line break
              block.appendChild(document.createElement("br"));
              nlStreak = 1;
              checklinks(block);
            } else {
              // second consecutive newline => end paragraph LATER
              pendingParagraphClose = true;
              nlStreak = 2;
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

            // Table: buffer all chars until newline
            if (inTable) {
                if (atLineStart) {
                    if (ch === '|') {
                        tableBuf = '|';
                        atLineStart = false;
                        continue;
                    } else if (ch === ' ') {
                        continue; // skip leading spaces
                    } else {
                        // line doesn't start with | — close table
                        closeTable();
                        // fall through to normal processing
                    }
                } else {
                    tableBuf += ch;
                    continue;
                }
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
                if (hashCount && !headingConfirmed) {
                    closeList();
                    openHeading(hashCount);
                    atLineStart=false;
                    if (ch === ' ') continue;
                    // not a space — fall through to normal char processing
                }
                if (!headingConfirmed) {
                    if (ch === '`') {
                        backtickBuf += '`';
                        if (backtickBuf === '```') {
                            backtickBuf = '';
                            closeList();
                            openFence();
                            continue;
                        }
                        continue;
                    } else if (backtickBuf) {
                        for (let k = 0; k < backtickBuf.length; k++)
                            toggleInlineCode();
                        backtickBuf = '';
                    }
                    // Table detection: | at line start
                    if (ch === '|' && listBuf === '') {
                        closeList();
                        if (block && block.tagName === 'P') finishParagraph();
                        block = null; inline = null;
                        inTable = true;
                        tableBuf = '|';
                        atLineStart = false;
                        continue;
                    }
                    // List detection: buffer chars to detect "- ", "* ", "1. " etc.
                    // skip leading spaces
                    if (ch === ' ' && listBuf.length === 0) {
                        continue;
                    }
                    listBuf += ch;
                    if (listBuf === '-' || listBuf === '*' || listBuf === '_') {
                        continue;  // wait for space or more chars
                    }
                    if (/^[-*_]{2,}$/.test(listBuf)) {
                        continue;  // accumulating potential horizontal rule
                    }
                    if (listBuf === '- ' || listBuf === '* ') {
                        listBuf = '';
                        openListItem('UL');
                        atLineStart = false;
                        continue;
                    }
                    if (/^\d+$/.test(listBuf) && listBuf.length <= 3) {
                        continue;  // accumulating digits
                    }
                    if (/^\d+\.$/.test(listBuf)) {
                        continue;  // got "1." waiting for space
                    }
                    if (/^\d+\. $/.test(listBuf)) {
                        listBuf = '';
                        openListItem('OL');
                        atLineStart = false;
                        continue;
                    }
                    // Not a list marker — flush listBuf as regular text
                    if (listBuf.length > 1 || (listBuf && ch !== '-' && ch !== '*' && !/\d/.test(ch))) {
                        const tmp = listBuf;
                        listBuf = '';
                        if (currentList) {
                            appendText(textParent(), tmp);
                        } else {
                            appendText(textParent(), tmp);
                        }
                        atLineStart = false;
                        continue;
                    }
                }
                // if headingConfirmed, skip fence/list detection and fall through
                // to normal char processing (bold, italic, text, etc.)
            } else if (currentList && nlStreak >= 2) {
                closeList();
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
                starBuf = '';
                toggleItalic();
                atLineStart = false;
                // don't consume ch — fall through to process it normally
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
        inItalic = false;
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
        listBuf = '';
        closeList();
        closeThinkBlock();
        requestAnimationFrame(maybeScroll);
    }

    function end() {
        if (inTable) {
            if (tableBuf.trim()) addTableRow(tableBuf);
            closeTable();
        }
        flushStarBufAsText();
        if (backtickBuf) {
            for (let k = 0; k < backtickBuf.length; k++) toggleInlineCode();
            backtickBuf = '';
        }
        flushWordBuf();
        listBuf = '';
        closeList();
        if (hashCount && !headingConfirmed) flushPendingHeadingHashesAsText();
        if (block) {
            checklinks(block);
        }
        renderMathIn(root);
    }

    function closeThinkBlock() {
        if (thinkDetails) {
            thinkDetails = null;
            thinkContent = null;
            block = null;
            inline = null;
        }
    }

    function writeThink(token) {
        if (!thinkDetails) {
            thinkDetails = document.createElement('details');
            thinkDetails.className = 'think-block';
            var summary = document.createElement('summary');
            summary.textContent = 'Thinking\u2026';
            thinkDetails.appendChild(summary);
            thinkContent = document.createElement('div');
            thinkContent.className = 'think-content';
            thinkDetails.appendChild(thinkContent);
            root.appendChild(thinkDetails);
        }
        var last = thinkContent.lastChild;
        if (last && last.nodeType === 3)
            last.nodeValue += token;
        else
            thinkContent.appendChild(document.createTextNode(token));
        maybeScroll();
    }

    /* A tool call, shown collapsed like a thinking block.  writeTool opens
       it when the call starts; writeToolResult fills it in when it ends. */
    const toolBlocks = {};

    function writeTool(info) {
        closeThinkBlock();
        block = null;
        inline = null;
        var d = document.createElement('details');
        d.className = 'tool-block running';
        var summary = document.createElement('summary');
        summary.textContent = (info.label || info.name) + '…';
        d.appendChild(summary);
        var body = document.createElement('div');
        body.className = 'tool-content';
        var args = document.createElement('div');
        args.className = 'tool-args';
        var argText = String(info.args || '');
        try { argText = JSON.stringify(JSON.parse(argText)); } catch (e) {}
        args.textContent = info.name + ' ' + argText;
        body.appendChild(args);
        d.appendChild(body);
        root.appendChild(d);
        if (info.id) toolBlocks[info.id] = {details: d, summary: summary, body: body, label: info.label || info.name};
        maybeScroll();
    }

    function writeToolResult(r) {
        var t = toolBlocks[r.id];
        if (!t) return;
        delete toolBlocks[r.id];
        var failed = /^Error:/.test(r.summary || '');
        t.details.classList.remove('running');
        if (failed) t.details.classList.add('failed');
        t.summary.textContent = t.label + (failed ? ' — failed' : '') +
            (r.ms !== undefined ? ' (' + (r.ms < 1000 ? r.ms + ' ms' : (r.ms / 1000).toFixed(1) + ' s') + ')' : '');
        if (r.link && /^https?:\/\//.test(r.link)) {
            // a page showing the same search, when the server provides one
            var lk = document.createElement('div');
            lk.className = 'tool-link';
            var a = document.createElement('a');
            a.href = r.link;
            a.target = '_blank';
            a.rel = 'noopener noreferrer';
            a.textContent = r.link;
            lk.appendChild(a);
            t.body.appendChild(lk);
        }
        var res = document.createElement('div');
        res.className = 'tool-result';
        res.textContent = r.summary || '';
        t.body.appendChild(res);
        if (r.raw) {
            var rawd = document.createElement('details');
            rawd.className = 'tool-raw';
            var rs = document.createElement('summary');
            rs.textContent = 'Result sent to the model (' + (r.chars || r.raw.length) + ' chars' +
                             (r.chars > r.raw.length ? ', first ' + r.raw.length + ' shown' : '') + ')';
            rawd.appendChild(rs);
            var pre = document.createElement('pre');
            pre.textContent = r.raw;
            rawd.appendChild(pre);
            t.body.appendChild(rawd);
        }
        maybeScroll();
    }

    function reset() {
        block = null;
        inline = null;
        inBold = false;
        inItalic = false;
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
        spaceBuf = '';
        wordBuf = '';
        listBuf = '';
        currentList = null;
        listStack = [];
        nlStreak = 0;
        pendingParagraphClose = false;
        thinkDetails = null;
        thinkContent = null;
        inTable = false;
        tableEl = null;
        tbodyEl = null;
        tableBuf = '';
        tableSeparatorSeen = false;
        userScrolledUp = false;
    }

    return {write, writeThink, closeThinkBlock, writeTool, writeToolResult, end, writeRaw, reset, forceScroll};
}

/* ================= Conversation Storage Helpers ================= */
var STORAGE_KEY = '_conversations';

/* Conversations live in localStorage.  When the browser refuses it
   (storage blocked for the site, some private modes, quota full), they are
   kept in memory instead for as long as the page is open.  The memory copy
   goes through JSON like storage does, so callers see the same behavior. */
var storageOK = null;           // null until tested
var memConversations = null;    // the in-memory store when storage is unusable

function storageUsable() {
    if (storageOK === null) {
        try {
            var k = '_llmchat_storage_test';
            localStorage.setItem(k, '1');
            localStorage.removeItem(k);
            storageOK = true;
        } catch (e) {
            storageOK = false;
        }
        if (!storageOK) storageUnavailable();
    }
    return storageOK;
}

/* switch to memory and say so in the sidebar */
function storageUnavailable(conversations) {
    storageOK = false;
    if (conversations) memConversations = JSON.stringify(conversations);
    var note = document.querySelector('#sidebar .footer');
    if (note)
        note.textContent = 'Browser storage is unavailable; conversations are kept only until you leave this page.';
}

function loadAllConversations() {
    try {
        //console.log(STORAGE_KEY);
        const raw = storageUsable() ? localStorage.getItem(STORAGE_KEY) : memConversations;
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
    if (!storageUsable()) {
        memConversations = JSON.stringify(conversations);
        return;
    }
    try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(conversations));
    } catch (e) {
        /* storage refused mid-session (e.g. quota): keep going in memory */
        console.warn('Failed to save conversations; keeping them in memory', e);
        storageUnavailable(conversations);
    }
}

function createConversationObject() {
    const id = 'conv_' + Date.now();
    const ts = new Date().toISOString();
    return {id, title: 'New conversation', createdAt: ts, messages: []};
}

function addTitle(text) {
    var t = (text || '').trim()
        .replace(/[*_`~#>]+/g, '')   // strip markdown formatting chars
        .replace(/^\s*[-]\s+/, '')    // strip leading list marker
        .replace(/\[([^\]]*)\]\([^)]*\)/g, '$1')  // [text](url) → text
        .replace(/^["']+|["']+$/g, '')  // strip surrounding quotes
        .replace(/\s+/g, ' ')
        .trim();
    if (!t) return 'New conversation';
    return t;
}

function findConversation(conversations, id) {
    return conversations.find(c => c.id === id);
}

function upsertConversation(conversations, conv) {
    conv.updatedAt = new Date().toISOString();
    const idx = conversations.findIndex(c => c.id === conv.id);
    if (idx === -1)
        conversations.unshift(conv);
    else
        conversations[idx] = conv;
    /* sort by most recently updated */
    conversations.sort(function(a, b) {
        return (b.updatedAt || b.createdAt || '').localeCompare(a.updatedAt || a.createdAt || '');
    });
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

    //console.log(endpoint);

    if(llmSettings)
        endpoint += '?s=' + llmSettings;
    if(endpoint.length > 8000)
        throw new Error("JSON llmSettings is too large");

    // context limit tracking
    let maxTokens = 0;  // 0 = unknown, set by server on connect

    function estimateTokens(text) {
        // rough estimate: 1 token ≈ 4 characters
        return Math.ceil((text || '').length / 4);
    }

    function estimateConversationTokens(messages) {
        var total = 0;
        messages.forEach(function(m) {
            total += estimateTokens(m.content) + 4; // 4 tokens overhead per message
        });
        return total;
    }

    function trimConversation(messages) {
        // strip ragSources — display-only, not for the LLM
        var clean = messages.map(function(m) {
            if (!m.ragSources) return m;
            return {role: m.role, content: m.content};
        });
        if (!maxTokens || !clean.length) return clean;
        // reserve 25% of context for the response
        var limit = Math.floor(maxTokens * 0.75);
        var est = estimateConversationTokens(clean);
        if (est <= limit) return clean;
        // keep first message (system prompt context) and trim oldest pairs
        var trimmed = clean.slice();
        var removed = 0;
        while (estimateConversationTokens(trimmed) > limit && trimmed.length > 2) {
            trimmed.splice(0, 2);
            removed += 2;
        }
        //console.log('Context trimmed:', removed, 'messages removed,', messages.length - removed, 'remaining, ~' + estimateConversationTokens(trimmed), 'of', maxTokens, 'tokens');
        return trimmed;
    }

    // sidebar state
    let conversations = loadAllConversations();
    let currentConv = conversations[0] || createConversationObject();
    if (!conversations.length) {
        conversations = upsertConversation(conversations, currentConv);
        saveAllConversations(conversations);
    }
    let assistantBuffer = '';  // collects streaming assistant text until end
    let queryConvId = null;    // conversation ID the active query belongs to
    var _retitleTimer = null;
    var _retitleOldTitle = '';
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

            function commitEdit() {
                title.contentEditable = 'false';
                var txt = title.textContent.trim();
                if (!txt) txt = 'Conversation';
                conv.title = txt;
                title.textContent = txt;
                persist();
            }

            title.addEventListener('click', function(e) {
                e.stopPropagation();
                if (conv.id === currentConv.id) {
                    title.contentEditable = 'true';
                    title.focus();
                    var range = document.createRange();
                    range.selectNodeContents(title);
                    var sel = window.getSelection();
                    sel.removeAllRanges();
                    sel.addRange(range);
                } else {
                    loadConversation(conv.id);
                }
            });
            title.addEventListener('blur', commitEdit);
            title.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') {
                    e.preventDefault();
                    title.blur();
                } else if (e.key === 'Escape') {
                    title.textContent = conv.title || 'Conversation';
                    title.contentEditable = 'false';
                }
            });

            const titleRow = document.createElement('div');
            titleRow.className = 'title-row';
            titleRow.appendChild(title);

            if (conv.messages && conv.messages.length >= 2) {
                const regen = document.createElement('button');
                regen.className = 'retitle-btn';
                regen.title = 'Regenerate title';
                regen.innerHTML = '&#x21bb;';
                regen.addEventListener('click', function(e) {
                    e.stopPropagation();
                    if (isOpen(socket) && conv.messages && conv.messages.length) {
                        queryConvId = conv.id;
                        _retitleOldTitle = conv.title || 'Conversation';
                        conv.title = 'Generating\u2026';
                        renderSidebar();
                        if (_retitleTimer) clearTimeout(_retitleTimer);
                        _retitleTimer = setTimeout(function() {
                            _retitleTimer = null;
                            var tc = findConversation(conversations, conv.id);
                            if (tc && tc.title === 'Generating\u2026') {
                                tc.title = 'Title failed';
                                renderSidebar();
                                setTimeout(function() {
                                    tc.title = _retitleOldTitle;
                                    conversations = upsertConversation(conversations, tc);
                                    saveAllConversations(conversations);
                                    renderSidebar();
                                }, 2000);
                            }
                        }, 15000);
                        var msgs = conv.messages;
                        var users = [], assts = [];
                        for (var j = msgs.length - 1; j >= 0; j--) {
                            if (msgs[j].role === 'user' && users.length < 6)
                                users.unshift({role: 'user', content: msgs[j].content});
                            else if (msgs[j].role === 'assistant' && assts.length < 2)
                                assts.unshift({role: 'assistant', content: msgs[j].content.slice(0, 300)});
                        }
                        socket.send(JSON.stringify({cmd: "retitle", messages: users.concat(assts)}));
                    }
                });
                titleRow.appendChild(regen);
            }

            const sub = document.createElement('span');
            sub.className = 'subtitle';
            const d = new Date(conv.updatedAt || conv.createdAt || Date.now());
            sub.textContent = d.toLocaleString();
            li.appendChild(titleRow);
            li.appendChild(sub);
            li.addEventListener('click', () => {
                loadConversation(conv.id);
            });
            ul.appendChild(li);
        });
    }

    function renderRagSources(sources) {
        if (!sources || !sources.length) return;
        var seen = {};
        var unique = sources.filter(function(s) {
            if (seen[s.title]) return false;
            seen[s.title] = true;
            return true;
        });
        var srcDiv = document.createElement('div');
        srcDiv.className = 'rag-sources';
        srcDiv.innerHTML = '<span class="rag-label">Sources:</span> ' +
            unique.map(function(s) {
                return '<a href="' + s.link + '" target="_blank" rel="noopener">' +
                       s.title + '</a>';
            }).join(' &middot; ');
        cd.appendChild(srcDiv);
    }

    function loadConversation(id) {
        const conv = findConversation(conversations, id);
        if (!conv) return;
        currentConv = conv;
        sendContextNext = true;
        cd.innerHTML = "";
        renderer.reset();
        assistantBuffer = '';
        // replay messages
        var lastAssistant = '';
        conv.messages.forEach(m => {
            if (m.role === 'user') {
                renderer.end();
                const span = document.createElement('span');
                span.className = 'userdata';
                span.textContent = m.content || '';
                cd.appendChild(span);
                cd.appendChild(document.createElement('br'));
            } else if (m.role === 'assistant') {
                lastAssistant = String(m.content || '');
                renderer.write(lastAssistant);
                if (m.ragSources)
                    renderRagSources(m.ragSources);
            }
        });
        renderer.end();
        renderSidebar();
        updateTokenBar();
    }

    function newConversation() {
        currentConv = createConversationObject();
        sendContextNext = false;
        conversations = upsertConversation(conversations, currentConv);
        saveAllConversations(conversations);
        cd.innerHTML = "";
        renderer.reset();
        assistantBuffer = '';
        renderSidebar();
        updateTokenBar();
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

    function updateTokenBar() {
        var bar = document.getElementById('tokenbar');
        var label = document.getElementById('tokenlabel');
        if (!bar || !label) return;
        if (!maxTokens) {
            label.textContent = '';
            bar.style.width = '0%';
            return;
        }
        var used = estimateConversationTokens(currentConv.messages || []);
        var pct = Math.min(100, Math.round((used / maxTokens) * 100));
        bar.style.width = pct + '%';
        bar.style.background = pct > 85 ? '#e74c3c' : pct > 60 ? '#f39c12' : 'var(--brand)';
        label.textContent = pct + '% of ' + maxTokens + ' tokens';
    }

    function showMessage(data) {
        if (typeof data == 'object') {
            if(data.maxTokens) {
                maxTokens = data.maxTokens;
                updateTokenBar();
                return;
            }
            if(data.think) {
                renderer.writeThink(String(data.think));
                return;
            }
            if(data.tool) {
                renderer.writeTool(data.tool);
                return;
            }
            if(data.toolResult) {
                renderer.writeToolResult(data.toolResult);
                return;
            }
            if(data.msg) {
                const span = document.createElement('span');
                span.className = 'userdata';
                span.textContent = data.msg;
                cd.appendChild(span);
                cd.appendChild(document.createElement('br'));
            } else if (data.update) {
                document.getElementById("status").innerHTML = data.update;
            } else if (data.srvmsg !== undefined ) { //it can be an empty string
                document.getElementById("msg").innerHTML = data.srvmsg;
            } else if (data.end) {
                // end() first so checklinks runs on the last block
                renderer.end();
                // then write the separator HTML
                renderer.writeRaw(data.end);
                // store assistant response using accumulated assistantBuffer
                if (assistantBuffer.trim().length) {
                    currentConv.messages.push(
                        {role: 'assistant', content: assistantBuffer});
                    assistantBuffer = '';
                    persist();
                }
                updateTokenBar();
            } else if (data.ragSources) {
                // attach to last assistant message for replay
                var msgs = currentConv.messages;
                for (var i = msgs.length - 1; i >= 0; i--) {
                    if (msgs[i].role === 'assistant') {
                        msgs[i].ragSources = data.ragSources;
                        persist();
                        break;
                    }
                }
                renderRagSources(data.ragSources);
            } else if (data.title) {
                // apply to the conversation that requested the query, not necessarily currentConv
                if (_retitleTimer) { clearTimeout(_retitleTimer); _retitleTimer = null; }
                var targetConv = queryConvId ? findConversation(conversations, queryConvId) : currentConv;
                if (targetConv) {
                    targetConv.title = addTitle(data.title);
                    conversations = upsertConversation(conversations, targetConv);
                    saveAllConversations(conversations);
                    renderSidebar();
                }
            } else if (data.error) {
                var errText = typeof data.error === 'object' ? JSON.stringify(data.error) : data.error;
                // show in chat area so it persists
                var errDiv = document.createElement('div');
                errDiv.style.cssText = 'color:#b91c1c;background:#fef2f2;border:1px solid #fecaca;padding:8px 12px;border-radius:8px;margin:8px 0;font-size:13px;';
                errDiv.textContent = 'Error: ' + errText;
                cd.appendChild(errDiv);
                cd.scrollTop = cd.scrollHeight;
                // also show briefly in header
                document.getElementById("msg").innerHTML = '<span style="color:red;font-size:12px;">Error</span>';
            } else {
                console.log("unhandled",data)
            }
        } else {
            // regular token — close thinking block if open, then render
            renderer.closeThinkBlock();
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
            JSON.stringify({q: text, context: trimConversation(currentConv.messages)}) :
            text;

        /* Update title if this is the first user message
        if (!currentConv.messages.length) {
            currentConv.title = summarizeTitleFromFirstUser(text);
            currentConv.createdAt = new Date().toISOString();
        }
        */

        // Store user message immediately
        currentConv.messages.push({role: 'user', content: text});
        queryConvId = currentConv.id;
        persist();
        updateTokenBar();

        const chatin = document.getElementById("chatin");
        try {
            if (!isOpen(socket) && !reconnected) {
                socket = new WebSocket(endpoint);
                socket.addEventListener('open', function(e) {
                    payload = JSON.stringify({q: text, context: trimConversation(currentConv.messages)})
                    socket.send(payload);
                    if (shouldSendContext) {
                        sendContextNext = false;
                    }
                    reconnected = true;
                    chatin.value = "";
                    chatin.style.height = "auto";
                    showMessage(data);
                    renderer.forceScroll();
                    socket.onmessage = procmess;
                });
                return;
            }
            socket.send(payload);
            if (shouldSendContext) {
                sendContextNext = false;
            }
            showMessage(data);
            renderer.forceScroll();
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
        renderer.reset();
        assistantBuffer = "";
        loadConversation(currentConv.id);
    });

    renderSidebar();
    // load current (first) conversation view
    loadConversation(currentConv.id);
    // load Prism and katex, then run start();
    loadScripts(start);
});
