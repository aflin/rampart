/* Mini IDE client: navigator, CodeMirror editor tabs, xterm terminals. */

const $ = (id) => document.getElementById(id);
const isMac = /Mac|iPhone|iPad/.test(navigator.platform);

const store = {
  get(k, d) {
    try { const v = localStorage.getItem('mini-ide.' + k); return v === null ? d : JSON.parse(v); }
    catch (e) { return d; }
  },
  set(k, v) {
    try { localStorage.setItem('mini-ide.' + k, JSON.stringify(v)); } catch (e) { /* private mode */ }
  }
};

const settings = Object.assign({
  appearance: 'auto', cmTheme: 'auto', fontSize: 13, indent: '4', wrap: false,
  navWidth: 260, inspWidth: 250, debugHeight: 260,
  showNav: true, showDebug: true, showInsp: false, showMenu: true
}, store.get('settings', {}));

function saveSettings() { store.set('settings', settings); }

/* ------------------------------------------------------------------ *
 * small helpers
 * ------------------------------------------------------------------ */

const basename = (p) => p.replace(/.*\//, '');
const dirname  = (p) => p.indexOf('/') < 0 ? '' : p.replace(/\/[^\/]*$/, '');
const joinPath = (d, n) => d ? d + '/' + n : n;

function esc(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
}

function fmtSize(n) {
  if (n < 1024) return n + ' bytes';
  if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
  return (n / 1048576).toFixed(1) + ' MB';
}

function isDark() {
  if (settings.appearance === 'dark') return true;
  if (settings.appearance === 'light') return false;
  return window.matchMedia('(prefers-color-scheme: dark)').matches;
}

let statusTimer = null;
function setStatus(msg, isErr) {
  const el = $('activity-status');
  el.textContent = msg || '';
  el.classList.toggle('err', !!isErr);
  clearTimeout(statusTimer);
  if (msg && !Terms.runningRun()) statusTimer = setTimeout(() => { el.textContent = ''; Terms.updateActivity(); }, 6000);
}

/* ------------------------------------------------------------------ *
 * server api
 * ------------------------------------------------------------------ */

async function api(name, body) {
  let r;
  try {
    r = await fetch('/ide/apps/ide/' + name + '.json', {
      method: 'POST',
      credentials: 'same-origin',
      headers: {'Content-Type': 'application/json', 'X-Mini-IDE': '1'},
      body: JSON.stringify(body || {})
    });
  } catch (e) {
    throw new Error('Cannot reach the server');
  }
  let j = {};
  try { j = await r.json(); } catch (e) { /* not json */ }
  if (r.status === 401 && name !== 'login') showLogin();
  if (!r.ok || j.error) {
    const err = new Error(j.error || ('Server error ' + r.status));
    err.status = r.status;
    err.data = j;
    throw err;
  }
  return j;
}

/* ------------------------------------------------------------------ *
 * modal dialogs and context menu
 * ------------------------------------------------------------------ */

function modal(opts) {
  return new Promise((resolve) => {
    const back = $('modal'), form = $('modal-form'), input = $('modal-input');
    const ok = $('modal-ok'), cancel = $('modal-cancel');
    $('modal-title').textContent = opts.title || '';
    const body = $('modal-body');
    body.textContent = '';
    if (opts.node) body.appendChild(opts.node);
    else if (opts.text) body.textContent = opts.text;
    input.hidden = opts.input === undefined;
    input.value = opts.input || '';
    ok.textContent = opts.okText || 'OK';
    ok.className = opts.danger ? 'danger' : 'primary';
    cancel.hidden = opts.noCancel;
    back.hidden = false;

    const done = (val) => {
      back.hidden = true;
      form.onsubmit = cancel.onclick = back.onmousedown = document.onkeydown = null;
      resolve(val);
    };
    form.onsubmit = (e) => { e.preventDefault(); done(input.hidden ? true : input.value.trim()); };
    cancel.onclick = () => done(null);
    back.onmousedown = (e) => { if (e.target === back) done(null); };
    document.onkeydown = (e) => { if (e.key === 'Escape') { e.preventDefault(); done(null); } };

    if (!input.hidden) {
      input.focus();
      const dot = input.value.lastIndexOf('.');
      input.setSelectionRange(0, opts.selectBase && dot > 0 ? dot : input.value.length);
    } else ok.focus();
  });
}

const confirmBox = (title, text, okText, danger) => modal({title, text, okText, danger});
const alertBox = (title, text) => modal({title, text, noCancel: true});

function contextMenu(x, y, items) {
  const m = $('ctxmenu');
  m.textContent = '';
  for (const it of items) {
    if (it === '-') { m.appendChild(document.createElement('hr')); continue; }
    const b = document.createElement('button');
    b.textContent = it.label;
    b.onclick = () => { m.hidden = true; it.action(); };
    m.appendChild(b);
  }
  m.hidden = false;
  const r = m.getBoundingClientRect();
  m.style.left = Math.min(x, innerWidth - r.width - 4) + 'px';
  m.style.top = Math.min(y, innerHeight - r.height - 4) + 'px';
}
document.addEventListener('mousedown', (e) => {
  if (!$('ctxmenu').contains(e.target)) $('ctxmenu').hidden = true;
});

/* ------------------------------------------------------------------ *
 * file icons
 * ------------------------------------------------------------------ */

const FOLDER_SVG = '<svg class="ficon dir" viewBox="0 0 16 16"><path d="M1.5 3.5a1 1 0 0 1 1-1h3.6l1.5 1.5h5.9a1 1 0 0 1 1 1v7.5a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1z"/></svg>';

function fileIcon(name) {
  const ext = (/\.([^.]+)$/.exec(name) || [])[1];
  let cls = '';
  if (ext) {
    const e = ext.toLowerCase();
    if (/^(js|mjs|cjs|ts|jsx|tsx)$/.test(e)) cls = 'js';
    else if (/^(html?|css|scss|less|svg)$/.test(e)) cls = 'web';
    else if (/^(json|csv|tsv|xml|ya?ml|sql|toml|ini|conf)$/.test(e)) cls = 'data';
    else if (/^(c|h|cc|cpp|hpp|py|sh|bash|rs|go|java|php|rb|pl|lua|txt|md)$/.test(e)) cls = 'code';
  }
  return '<svg class="ficon ' + cls + '" viewBox="0 0 16 16"><path d="M4 1.5h5l3.5 3.5v9.5H3.5v-13z M9 1.5V5h3.5" fill="none" stroke-width="1.1"/></svg>';
}

/* ------------------------------------------------------------------ *
 * navigator tree
 * ------------------------------------------------------------------ */

const Tree = {
  expanded: new Set(store.get('expanded', [])),
  hidden: store.get('hidden', false),
  selected: null,
  info: {},          // path -> entry from the last listing

  saveExpanded() { store.set('expanded', Array.from(this.expanded)); },

  async refresh() {
    const el = $('tree'), scroll = el.scrollTop;
    const box = document.createElement('div');
    this.info = {};
    await this.buildDir('', 0, box);
    el.textContent = '';
    el.appendChild(box);
    el.scrollTop = scroll;
    this.markSelected();
    this.markDirty();
    this.applyFilter();
    Watch.sync();
  },

  /* reload one shown directory in place (after a change notice) */
  async refreshDir(path) {
    if (!path) return this.refresh();
    const row = this.rowFor(path);
    const kids = row && row.nextElementSibling;
    if (!kids || !kids.classList.contains('children')) return;
    const el = $('tree'), scroll = el.scrollTop;
    const box = document.createElement('div');
    box.className = 'children';
    await this.buildDir(path, +row.dataset.depth + 1, box);
    if (row.nextElementSibling !== kids) return;      /* collapsed or rebuilt meanwhile */
    kids.replaceWith(box);
    el.scrollTop = scroll;
    this.markSelected();
    this.markDirty();
    this.applyFilter();
    Watch.sync();
  },

  async buildDir(path, depth, parent) {
    let res;
    try {
      res = await api('list', {path, hidden: this.hidden});
    } catch (e) {
      const m = document.createElement('div');
      m.className = 'tree-msg';
      m.style.paddingLeft = (8 + depth * 14) + 'px';
      m.textContent = e.message;
      parent.appendChild(m);
      if (e.status === 404 && path) { this.expanded.delete(path); this.saveExpanded(); }
      return;
    }
    const pending = [];
    for (const ent of res.entries) {
      const p = joinPath(path, ent.name);
      this.info[p] = ent;
      const row = document.createElement('div');
      row.className = 'row' + (ent.link ? ' link' : '');
      row.dataset.path = p;
      row.dataset.depth = depth;
      if (ent.dir) row.dataset.dir = '1';
      row.style.paddingLeft = (4 + depth * 14) + 'px';
      const open = ent.dir && this.expanded.has(p);
      row.innerHTML = '<span class="twisty">' + (ent.dir ? (open ? '&#9660;' : '&#9654;') : '') + '</span>' +
        (ent.dir ? FOLDER_SVG : fileIcon(ent.name)) + '<span class="name">' + esc(ent.name) + '</span>';
      row.title = p;
      parent.appendChild(row);
      if (open) {
        const kids = document.createElement('div');
        kids.className = 'children';
        parent.appendChild(kids);
        pending.push(this.buildDir(p, depth + 1, kids));
      }
    }
    if (!res.entries.length && path) {
      const m = document.createElement('div');
      m.className = 'tree-msg';
      m.style.paddingLeft = (22 + depth * 14) + 'px';
      m.textContent = 'Empty';
      parent.appendChild(m);
    }
    await Promise.all(pending);
  },

  rowFor(path) {
    return $('tree').querySelector('.row[data-path="' + CSS.escape(path) + '"]');
  },

  async toggle(row) {
    const p = row.dataset.path, depth = +row.dataset.depth;
    const next = row.nextElementSibling;
    if (this.expanded.has(p)) {
      this.expanded.delete(p);
      if (next && next.classList.contains('children')) next.remove();
      row.querySelector('.twisty').innerHTML = '&#9654;';
    } else {
      this.expanded.add(p);
      const kids = document.createElement('div');
      kids.className = 'children';
      row.after(kids);
      row.querySelector('.twisty').innerHTML = '&#9660;';
      await this.buildDir(p, depth + 1, kids);
      this.markDirty();
      this.applyFilter();
    }
    this.saveExpanded();
    Watch.sync();
  },

  /* expand every directory leading to path */
  async reveal(path) {
    let d = dirname(path), changed = false;
    while (d) {
      if (!this.expanded.has(d)) { this.expanded.add(d); changed = true; }
      d = dirname(d);
    }
    if (changed) { this.saveExpanded(); await this.refresh(); }
    this.select(path);
    const row = this.rowFor(path);
    if (row) row.scrollIntoView({block: 'nearest'});
  },

  select(path) {
    this.selected = path;
    this.markSelected();
    Inspector.update();
  },

  markSelected() {
    for (const r of $('tree').querySelectorAll('.row.selected')) r.classList.remove('selected');
    const row = this.selected !== null && this.rowFor(this.selected);
    if (row) row.classList.add('selected');
  },

  markDirty() {
    for (const r of $('tree').querySelectorAll('.row')) {
      const t = Editor.find(r.dataset.path);
      r.classList.toggle('dirty', !!(t && t.dirty));
    }
  },

  /* directory in which new files are created: the selected folder, or the selected file's folder */
  targetDir() {
    const s = this.selected;
    if (!s) return '';
    const ent = this.info[s];
    return ent && ent.dir ? s : dirname(s);
  },

  applyFilter() {
    const q = $('nav-filter').value.trim().toLowerCase();
    const walk = (container) => {
      let any = false;
      for (const el of container.children) {
        if (el.classList.contains('row')) {
          const nameHit = !q || basename(el.dataset.path).toLowerCase().includes(q);
          const kids = el.nextElementSibling;
          let kidHit = false;
          if (kids && kids.classList.contains('children')) {
            kidHit = walk(kids);
            kids.hidden = !!q && !kidHit && !nameHit;
          }
          const show = nameHit || kidHit;
          el.hidden = !show;
          any = any || show;
        } else if (el.classList.contains('tree-msg')) {
          el.hidden = !!q;
        }
      }
      return any;
    };
    const root = $('tree').firstElementChild;
    if (root) walk(root);
  }
};

/* ------------------------------------------------------------------ *
 * CodeMirror loading, languages and themes
 * ------------------------------------------------------------------ */

let CM = null;

async function loadCM() {
  if (CM) return CM;
  const b = await import('./lib/codemirror-bundle.js');
  CM = {
    b,
    EditorView: b.codemirror.EditorView,
    EditorState: b.state.EditorState,
    Compartment: b.state.Compartment,
    basicSetup: b.codemirror.basicSetup
  };
  return CM;
}

const THEMES = {
  'auto':            ['Auto'],
  'one-dark':        ['One Dark',        (b) => b.themeOneDark.oneDark],
  'dracula':         ['Dracula',         (b) => b.themeDracula.dracula],
  'github-light':    ['GitHub Light',    (b) => b.themeGithub.githubLight],
  'github-dark':     ['GitHub Dark',     (b) => b.themeGithub.githubDark],
  'material-light':  ['Material Light',  (b) => b.themeMaterial.materialLight],
  'material-dark':   ['Material Dark',   (b) => b.themeMaterial.materialDark],
  'monokai':         ['Monokai',         (b) => b.themeMonokai.monokai],
  'nord':            ['Nord',            (b) => b.themeNord.nord],
  'solarized-light': ['Solarized Light', (b) => b.themeSolarized.solarizedLight],
  'solarized-dark':  ['Solarized Dark',  (b) => b.themeSolarized.solarizedDark],
  'tokyo-night':     ['Tokyo Night',     (b) => b.themeTokyoNight.tokyoNight],
  'vscode-light':    ['VS Code Light',   (b) => b.themeVscode.vscodeLight],
  'vscode-dark':     ['VS Code Dark',    (b) => b.themeVscode.vscodeDark]
};

function themeExt() {
  const t = THEMES[settings.cmTheme];
  if (!t || !t[1]) return isDark() ? CM.b.themeOneDark.oneDark : [];
  try { return t[1](CM.b) || []; } catch (e) { return []; }
}

function prefsExt() {
  const fs = settings.fontSize + 'px';
  const unit = settings.indent === 'tab' ? '\t' : ' '.repeat(+settings.indent || 4);
  return [
    CM.EditorView.theme({'&': {fontSize: fs}, '.cm-gutters': {fontSize: fs}}),
    CM.EditorState.tabSize.of(settings.indent === 'tab' ? 4 : unit.length),
    CM.b.language.indentUnit.of(unit),
    settings.wrap ? CM.EditorView.lineWrapping : []
  ];
}

async function languageFor(name) {
  const descs = CM.b.languageData.languages;
  let desc = descs.find((d) => d.filename && d.filename.test(name));
  if (!desc) {
    const m = /\.([^.]+)$/.exec(name);
    if (m) {
      const ext = m[1].toLowerCase();
      desc = descs.find((d) => d.extensions && d.extensions.indexOf(ext) > -1);
    }
  }
  if (!desc) return {name: 'Plain Text', ext: []};
  try { return {name: desc.name, ext: await desc.load()}; }
  catch (e) { return {name: desc.name, ext: []}; }
}

/* ------------------------------------------------------------------ *
 * editor tabs
 * ------------------------------------------------------------------ */

const Editor = {
  tabs: [],
  active: null,

  find(path) { return this.tabs.find((t) => t.path === path); },

  async open(path, quiet) {
    let tab = this.find(path);
    if (tab) { this.activate(tab); return tab; }

    let data;
    try {
      data = await api('read', {path});
    } catch (e) {
      if (!quiet) alertBox('Cannot open ' + basename(path), e.message);
      return null;
    }
    tab = this.find(path);        // opened meanwhile by a double click
    if (tab) { this.activate(tab); return tab; }

    tab = {path, info: data, mtime: data.mtime, dirty: false, view: null, lang: 'Plain Text'};
    tab.pane = document.createElement('div');
    tab.pane.className = 'editor-pane';
    $('editors').appendChild(tab.pane);
    tab.tabEl = document.createElement('div');
    tab.tabEl.className = 'tab';
    tab.tabEl.innerHTML = '<span class="tname"></span><button class="tclose" title="Close"></button>';
    tab.tabEl.querySelector('.tname').textContent = basename(path);
    tab.tabEl.title = path;
    tab.tabEl.onmousedown = (e) => { if (e.button === 0 && !e.target.classList.contains('tclose')) this.activate(tab); };
    tab.tabEl.onauxclick = (e) => { if (e.button === 1) this.close(tab); };
    tab.tabEl.querySelector('.tclose').onclick = () => this.close(tab);
    $('tabs').appendChild(tab.tabEl);
    this.tabs.push(tab);

    if (data.binary || data.tooLarge) {
      const m = document.createElement('div');
      m.className = 'editor-msg';
      m.textContent = data.binary ? 'Binary file - not shown in the editor.'
                                  : 'File is too large to edit here (' + fmtSize(data.size) + ').';
      tab.pane.appendChild(m);
      tab.readOnly = true;
    } else {
      await this.makeView(tab, data.content);
    }
    this.activate(tab);
    this.saveOpenList();
    return tab;
  },

  async makeView(tab, content) {
    try {
      await loadCM();
    } catch (e) {
      /* fallback: plain textarea */
      const ta = document.createElement('textarea');
      ta.value = content;
      ta.spellcheck = false;
      ta.oninput = () => this.setDirty(tab, ta.value !== tab.saved);
      tab.pane.appendChild(ta);
      tab.saved = content;
      tab.getText = () => ta.value;
      tab.markSaved = () => { tab.saved = ta.value; };
      tab.focus = () => ta.focus();
      return;
    }
    const lang = await languageFor(basename(tab.path));
    tab.lang = lang.name;
    tab.langComp = new CM.Compartment();
    tab.themeComp = new CM.Compartment();
    tab.prefsComp = new CM.Compartment();
    const keys = CM.b.view.keymap.of([
      CM.b.commands.indentWithTab,
      {key: 'Mod-s', preventDefault: true, run: () => { this.save(tab); return true; }},
      {key: 'Mod-Shift-s', preventDefault: true, run: () => { this.saveAs(tab); return true; }},
      {key: 'Mod-r', preventDefault: true, run: () => { runActive(); return true; }}
    ]);
    tab.view = new CM.EditorView({
      doc: content,
      parent: tab.pane,
      extensions: [
        CM.basicSetup,
        keys,
        tab.langComp.of(lang.ext),
        tab.themeComp.of(themeExt()),
        tab.prefsComp.of(prefsExt()),
        CM.EditorView.updateListener.of((u) => {
          if (u.docChanged) this.setDirty(tab, !u.state.doc.eq(tab.savedDoc));
          if (u.docChanged || u.selectionSet) this.updateCursor(tab);
        })
      ]
    });
    tab.savedDoc = tab.view.state.doc;
    tab.getText = () => tab.view.state.doc.toString();
    tab.markSaved = () => { tab.savedDoc = tab.view.state.doc; };
    tab.focus = () => tab.view.focus();
  },

  setDirty(tab, dirty) {
    if (tab.dirty === dirty) return;
    tab.dirty = dirty;
    tab.tabEl.classList.toggle('dirty', dirty);
    const row = Tree.rowFor(tab.path);
    if (row) row.classList.toggle('dirty', dirty);
  },

  activate(tab) {
    this.active = tab;
    for (const t of this.tabs) {
      t.pane.hidden = t !== tab;
      t.tabEl.classList.toggle('active', t === tab);
    }
    $('welcome').hidden = !!tab;
    if (tab) {
      tab.tabEl.scrollIntoView({block: 'nearest', inline: 'nearest'});
      if (tab.focus) tab.focus();
      Tree.select(tab.path);
      store.set('activeTab', tab.path);
    } else {
      Inspector.update();
    }
    this.updateJumpBar();
    this.updateCursor(tab);
    updateRunButtons();
  },

  async close(tab, force) {
    if (tab.dirty && !force) {
      const ok = await confirmBox('Close ' + basename(tab.path) + '?',
        'It has unsaved changes, which will be lost.', 'Discard Changes', true);
      if (!ok) return false;
    }
    const i = this.tabs.indexOf(tab);
    this.tabs.splice(i, 1);
    if (tab.view) tab.view.destroy();
    tab.pane.remove();
    tab.tabEl.remove();
    const row = Tree.rowFor(tab.path);
    if (row) row.classList.remove('dirty');
    if (this.active === tab) this.activate(this.tabs[Math.min(i, this.tabs.length - 1)] || null);
    this.saveOpenList();
    return true;
  },

  async save(tab, force) {
    if (!tab || tab.readOnly || !tab.getText) return true;
    const content = tab.getText();
    let res;
    try {
      res = await api('write', {path: tab.path, content, mtime: tab.mtime, force: !!force});
    } catch (e) {
      if (e.data && e.data.conflict) {
        const ok = await confirmBox(basename(tab.path) + ' changed on disk',
          'The file was modified outside the editor since it was opened.  Overwrite it with your version?',
          'Overwrite', true);
        return ok ? this.save(tab, true) : false;
      }
      setStatus('Could not save ' + basename(tab.path) + ': ' + e.message, true);
      return false;
    }
    tab.mtime = res.mtime;
    tab.info = res;
    if (tab.getText() === content) {
      tab.markSaved();
      this.setDirty(tab, false);
    }
    setStatus('Saved ' + basename(tab.path));
    if (Tree.info[tab.path]) Object.assign(Tree.info[tab.path], res);
    if (this.active === tab) Inspector.update();
    return true;
  },

  /* write the buffer to a new path; the tab then refers to the new file */
  async saveAs(tab) {
    if (!tab || tab.readOnly || !tab.getText) return false;
    const to = await modal({title: 'Save As', text: 'Path relative to ' + App.root + '/',
                            input: tab.path, okText: 'Save', selectBase: true});
    if (!to) return false;
    const path = to.replace(/^\/+/, '');
    if (path === tab.path) return this.save(tab);
    if (this.find(path)) {
      alertBox('Cannot save as ' + basename(path), path + ' is open in another tab.  Close that tab first.');
      return false;
    }
    const content = tab.getText();
    let res;
    try {
      res = await api('write', {path, content, create: true});
    } catch (e) {
      if (e.status !== 409) { alertBox('Could not save ' + basename(path), e.message); return false; }
      if (!(await confirmBox(basename(path) + ' already exists', '/' + path + ' will be replaced.', 'Replace', true)))
        return false;
      try {
        res = await api('write', {path, content, force: true});
      } catch (e2) {
        alertBox('Could not save ' + basename(path), e2.message);
        return false;
      }
    }
    tab.path = path;
    tab.mtime = res.mtime;
    tab.info = res;
    tab.tabEl.querySelector('.tname').textContent = basename(path);
    tab.tabEl.title = path;
    if (tab.getText() === content) { tab.markSaved(); this.setDirty(tab, false); }
    await this.setLanguage(tab);
    this.saveOpenList();
    this.updateJumpBar();
    setStatus('Saved ' + basename(path));
    await Tree.refresh();
    await Tree.reveal(path);
    updateRunButtons();
    return true;
  },

  /* discard edits and reload from disk */
  async revert(tab) {
    if (!tab || !tab.view) return;
    if (tab.dirty && !(await confirmBox('Revert ' + basename(tab.path) + '?',
        'Unsaved changes will be discarded and the file reloaded from disk.', 'Revert', true))) return;
    let data;
    try {
      data = await api('read', {path: tab.path});
    } catch (e) {
      return alertBox('Cannot reload ' + basename(tab.path), e.message);
    }
    if (data.binary || data.tooLarge) return alertBox('Cannot reload ' + basename(tab.path), 'The file is no longer editable text.');
    tab.view.dispatch({changes: {from: 0, to: tab.view.state.doc.length, insert: data.content}});
    tab.mtime = data.mtime;
    tab.info = data;
    tab.markSaved();
    this.setDirty(tab, false);
    setStatus('Reverted ' + basename(tab.path));
  },

  async setLanguage(tab) {
    if (!tab.view) return;
    const lang = await languageFor(basename(tab.path));
    tab.lang = lang.name;
    tab.view.dispatch({effects: tab.langComp.reconfigure(lang.ext)});
    this.updateCursor(tab);
  },

  async saveAll() {
    for (const t of this.tabs)
      if (t.dirty && !(await this.save(t))) return false;
    return true;
  },

  /* a file or directory was renamed: retarget open tabs */
  renamed(from, to) {
    for (const t of this.tabs) {
      if (t.path === from || t.path.startsWith(from + '/')) {
        t.path = to + t.path.substring(from.length);
        t.tabEl.querySelector('.tname').textContent = basename(t.path);
        t.tabEl.title = t.path;
        this.setLanguage(t);
      }
    }
    this.saveOpenList();
    this.updateJumpBar();
  },

  deleted(path) {
    for (const t of this.tabs.slice())
      if (t.path === path || t.path.startsWith(path + '/')) this.close(t, true);
  },

  saveOpenList() {
    store.set('openTabs', this.tabs.map((t) => t.path));
  },

  updateJumpBar() {
    const el = $('jump-path'), t = this.active;
    if (!t) { el.textContent = ''; return; }
    const parts = [App.root].concat(t.path.split('/'));
    el.innerHTML = parts.map((p, i) => '<span' + (i === parts.length - 1 ? ' class="last"' : '') + '>' +
      esc(p) + '</span>').join('<span class="sep">&rsaquo;</span>');
    document.title = basename(t.path) + ' - Mini IDE';
  },

  updateCursor(tab) {
    const el = $('cursor-pos');
    if (!tab || tab !== this.active || !tab.view) { el.textContent = ''; return; }
    const st = tab.view.state, pos = st.selection.main.head, line = st.doc.lineAt(pos);
    el.textContent = 'Line ' + line.number + ', Col ' + (pos - line.from + 1) + '  ·  ' + tab.lang;
  },

  /* settings changed: reconfigure every open editor */
  reconfigure() {
    if (!CM) return;
    for (const t of this.tabs) {
      if (!t.view) continue;
      t.view.dispatch({effects: [t.themeComp.reconfigure(themeExt()), t.prefsComp.reconfigure(prefsExt())]});
    }
  }
};

/* ------------------------------------------------------------------ *
 * terminals (debug area)
 * ------------------------------------------------------------------ */

function termTheme() {
  const css = getComputedStyle(document.documentElement);
  const bg = css.getPropertyValue('--term-bg').trim(), fg = css.getPropertyValue('--term-fg').trim();
  const t = {background: bg, foreground: fg, cursor: fg, cursorAccent: bg};
  if (isDark()) {
    t.selectionBackground = 'rgba(120,160,255,.35)';
  } else {
    Object.assign(t, {
      selectionBackground: 'rgba(10,100,230,.22)',
      black: '#1d1d1f', white: '#8e8e93', brightBlack: '#6e6e73', brightWhite: '#48484a',
      yellow: '#9a6a00', brightYellow: '#b07d00', green: '#1f8a3a', brightGreen: '#26a148',
      cyan: '#0e7c86', brightCyan: '#1592a0'
    });
  }
  return t;
}

const Terms = {
  list: [],
  active: null,
  seq: 0,

  create(mode, opts) {
    opts = opts || {};
    if (!App.termOnly) showPanel('debug', true);
    const s = {id: ++this.seq, mode, path: opts.path || '', running: true};
    s.title = mode === 'repl' ? 'rampart' : mode === 'run' ? basename(s.path)
            : (s.path ? basename(s.path) : 'Terminal');
    s.kind = mode === 'repl' ? 'REPL' : mode === 'run' ? 'RUN' : 'SH';

    s.el = document.createElement('div');
    s.el.className = 'term-pane';
    $('terms').appendChild(s.el);

    s.tabEl = document.createElement('div');
    s.tabEl.className = 'tab running';
    s.tabEl.innerHTML = '<span class="kind"></span><span class="tname"></span><button class="tclose" title="Close"></button>';
    s.tabEl.querySelector('.kind').textContent = s.kind;
    s.tabEl.querySelector('.tname').textContent = s.title;
    s.tabEl.title = s.path || App.root;
    s.tabEl.onmousedown = (e) => { if (e.button === 0 && !e.target.classList.contains('tclose')) this.activate(s); };
    s.tabEl.querySelector('.tclose').onclick = () => this.close(s);
    $('term-tabs').appendChild(s.tabEl);
    this.list.push(s);

    s.term = new window.Terminal({
      fontFamily: getComputedStyle(document.documentElement).getPropertyValue('--font-mono'),
      fontSize: settings.fontSize,
      cursorBlink: true,
      scrollback: 5000,
      theme: termTheme()
    });
    s.fit = new window.FitAddon.FitAddon();
    s.term.loadAddon(s.fit);
    this.activate(s);
    s.term.open(s.el);
    try { s.fit.fit(); } catch (e) { /* not visible yet */ }

    const q = new URLSearchParams({mode, cols: s.term.cols, rows: s.term.rows});
    if (s.path) q.set('path', s.path);
    if (opts.args) q.set('args', opts.args);
    const url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ide/wsapps/term.js?' + q;
    s.ws = new WebSocket(url);
    s.ws.binaryType = 'arraybuffer';
    s.ws.onmessage = (e) => s.term.write(typeof e.data === 'string' ? e.data : new Uint8Array(e.data));
    s.ws.onclose = () => this.ended(s);
    s.term.onData((d) => { if (s.ws.readyState === 1) s.ws.send(d); });
    s.term.onResize((sz) => this.sendResize(s, sz.cols, sz.rows));
    s.term.focus();
    this.updateActivity();
    updateRunButtons();
    return s;
  },

  sendResize(s, cols, rows) {
    if (s.ws && s.ws.readyState === 1)
      s.ws.send(new TextEncoder().encode(JSON.stringify({resize: {cols, rows}})));
  },

  ended(s) {
    if (!s.running) return;
    s.running = false;
    s.tabEl.classList.remove('running');
    s.tabEl.classList.add('ended');
    if (!s.closing) s.term.write('\r\n\x1b[2m[' + (s.stopped ? 'stopped' : 'session ended') + ']\x1b[0m\r\n');
    if (s.mode === 'run') setStatus(s.title + (s.stopped ? ' stopped' : ' finished'));
    this.updateActivity();
    updateRunButtons();
    Tree.refresh();     /* the script or shell may have changed files */
  },

  activate(s) {
    this.active = s;
    for (const x of this.list) {
      x.el.hidden = x !== s;
      x.tabEl.classList.toggle('active', x === s);
    }
    $('terms-empty').hidden = !!s;
    if (s) {
      s.tabEl.scrollIntoView({block: 'nearest', inline: 'nearest'});
      requestAnimationFrame(() => { this.fit(); s.term.focus(); });
    }
    updateRunButtons();
  },

  fit() {
    const s = this.active;
    if (!s || $('debug').hidden) return;
    try { s.fit.fit(); } catch (e) { /* ignore */ }
  },

  close(s) {
    s.closing = true;
    if (s.ws && s.ws.readyState <= 1) s.ws.close();
    s.running = false;
    s.term.dispose();
    s.el.remove();
    s.tabEl.remove();
    const i = this.list.indexOf(s);
    this.list.splice(i, 1);
    if (this.active === s) this.activate(this.list[Math.min(i, this.list.length - 1)] || null);
    this.updateActivity();
    updateRunButtons();
  },

  stop(s) {
    if (!s || !s.running) return;
    s.stopped = true;
    s.ws.close();
  },

  runningRun() {
    const a = this.active;
    if (a && a.mode === 'run' && a.running) return a;
    for (let i = this.list.length - 1; i >= 0; i--)
      if (this.list[i].mode === 'run' && this.list[i].running) return this.list[i];
    return null;
  },

  updateActivity() {
    const r = this.runningRun(), st = $('activity-status');
    if (r) {
      clearTimeout(statusTimer);
      st.classList.remove('err');
      st.textContent = 'Running ' + r.title;
    }
  },

  retheme() {
    for (const s of this.list) {
      s.term.options.theme = termTheme();
      s.term.options.fontSize = settings.fontSize;
    }
    this.fit();
  }
};

/* ------------------------------------------------------------------ *
 * run / stop
 * ------------------------------------------------------------------ */

function canRun(tab) {
  return !!tab && /\.(js|mjs|cjs)$/i.test(tab.path);
}

function updateRunButtons() {
  $('btn-run').disabled = !canRun(Editor.active);
  $('btn-stop').disabled = !Terms.runningRun();
}

async function runActive() {
  const tab = Editor.active;
  if (!canRun(tab)) { setStatus('Open a .js file to run it'); return; }
  if (!(await Editor.saveAll())) return;
  for (const s of Terms.list.slice())
    if (s.mode === 'run' && s.path === tab.path) Terms.close(s);
  Terms.create('run', {path: tab.path, args: $('run-args').value.trim()});
}

/* ------------------------------------------------------------------ *
 * inspector
 * ------------------------------------------------------------------ */

const Inspector = {
  update() {
    const dl = $('insp-file');
    const path = Tree.selected;
    if (path === null || path === undefined) { dl.innerHTML = '<dt>No file selected</dt>'; return; }
    const tab = Editor.find(path);
    const inf = (tab && tab.info) || Tree.info[path];
    if (!inf) { dl.innerHTML = '<dt>No file selected</dt>'; return; }
    const rows = [
      ['Name', basename(path)],
      ['Path', '/' + path],
      ['Type', inf.dir ? 'Folder' : (tab ? tab.lang : 'File') + (inf.link ? ' (link)' : '')]
    ];
    if (!inf.dir) rows.push(['Size', fmtSize(inf.size)]);
    if (tab && tab.view) rows.push(['Lines', String(tab.view.state.doc.lines)]);
    rows.push(['Modified', new Date(inf.mtime).toLocaleString()]);
    rows.push(['Mode', inf.permissions || '']);
    rows.push(['Owner', (inf.owner || '') + ':' + (inf.group || '')]);
    if (!inf.writable) rows.push(['Access', 'read only']);
    dl.innerHTML = rows.map((r) => '<dt>' + esc(r[0]) + '</dt><dd>' + esc(r[1]) + '</dd>').join('');
  }
};

/* ------------------------------------------------------------------ *
 * file operations
 * ------------------------------------------------------------------ */

async function newFile(dir) {
  const name = await modal({title: 'New file in /' + dir, input: '', okText: 'Create'});
  if (!name) return;
  const path = joinPath(dir, name);
  try {
    await api('write', {path, content: '', create: true});
  } catch (e) {
    return alertBox('Could not create ' + name, e.message);
  }
  await Tree.reveal(path);
  await Editor.open(path);
}

async function newFolder(dir) {
  const name = await modal({title: 'New folder in /' + dir, input: '', okText: 'Create'});
  if (!name) return;
  const path = joinPath(dir, name);
  try {
    await api('mkdir', {path});
  } catch (e) {
    return alertBox('Could not create ' + name, e.message);
  }
  await Tree.reveal(path);
}

async function renamePath(path) {
  const name = await modal({title: 'Rename ' + basename(path), input: basename(path), okText: 'Rename', selectBase: true});
  if (!name || name === basename(path)) return;
  const to = joinPath(dirname(path), name);
  try {
    await api('rename', {from: path, to});
  } catch (e) {
    return alertBox('Could not rename ' + basename(path), e.message);
  }
  for (const p of Array.from(Tree.expanded)) {
    if (p === path || p.startsWith(path + '/')) {
      Tree.expanded.delete(p);
      Tree.expanded.add(to + p.substring(path.length));
    }
  }
  Tree.saveExpanded();
  Editor.renamed(path, to);
  Tree.selected = to;
  await Tree.refresh();
  Inspector.update();
}

async function deletePath(path) {
  const ent = Tree.info[path];
  const ok = await confirmBox('Delete ' + basename(path) + '?',
    (ent && ent.dir ? 'The folder /' + path + ' and everything in it will be deleted.'
                    : '/' + path + ' will be deleted.') + '  This cannot be undone.',
    'Delete', true);
  if (!ok) return;
  try {
    await api('delete', {path});
  } catch (e) {
    return alertBox('Could not delete ' + basename(path), e.message);
  }
  Editor.deleted(path);
  if (Tree.selected === path) Tree.selected = null;
  await Tree.refresh();
  Inspector.update();
}

function treeMenu(e, path) {
  e.preventDefault();
  const ent = path ? Tree.info[path] : {dir: true};
  const dir = ent && ent.dir ? path : dirname(path);
  const items = [];
  if (path && !ent.dir) items.push({label: 'Open', action: () => Editor.open(path)});
  if (path && !ent.dir && /\.(js|mjs|cjs)$/i.test(path))
    items.push({label: 'Run with rampart', action: async () => { await Editor.open(path); runActive(); }});
  if (items.length) items.push('-');
  items.push({label: 'New File...', action: () => newFile(dir)});
  items.push({label: 'New Folder...', action: () => newFolder(dir)});
  items.push({label: 'Open Terminal Here', action: () => Terms.create('shell', {path: dir})});
  if (path) {
    items.push('-');
    items.push({label: 'Rename...', action: () => renamePath(path)});
    items.push({label: 'Delete...', action: () => deletePath(path)});
    items.push('-');
    items.push({label: 'Copy Path', action: () => copyText(App.rootPath + '/' + path)});
  }
  contextMenu(e.clientX, e.clientY, items);
}

function copyText(t) {
  if (navigator.clipboard && window.isSecureContext)
    navigator.clipboard.writeText(t).then(() => setStatus('Copied path'), () => {});
  else
    modal({title: 'Path', input: t, noCancel: true});
}

/* ------------------------------------------------------------------ *
 * layout: panels, splitters, settings
 * ------------------------------------------------------------------ */

function showPanel(which, show) {
  const key = {nav: 'showNav', debug: 'showDebug', insp: 'showInsp', menu: 'showMenu'}[which];
  if (show === undefined) show = !settings[key];
  settings[key] = show;
  saveSettings();
  applyLayout();
}

function applyLayout() {
  $('navigator').hidden = $('split-nav').hidden = !settings.showNav;
  $('debug').hidden = $('split-debug').hidden = !settings.showDebug;
  $('inspector').hidden = $('split-insp').hidden = !settings.showInsp;
  $('menubar').hidden = !settings.showMenu;
  $('toggle-menu').classList.toggle('on', settings.showMenu);
  $('toggle-nav').classList.toggle('on', settings.showNav);
  $('toggle-debug').classList.toggle('on', settings.showDebug);
  $('toggle-insp').classList.toggle('on', settings.showInsp);
  $('navigator').style.width = settings.navWidth + 'px';
  $('inspector').style.width = settings.inspWidth + 'px';
  $('debug').style.height = settings.debugHeight + 'px';
  if (settings.showDebug) requestAnimationFrame(() => Terms.fit());
}

function splitter(id, onMove) {
  const el = $(id);
  el.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    document.body.classList.add('dragging');
    const move = (ev) => { onMove(ev); applyLayout(); };
    const up = () => {
      el.removeEventListener('pointermove', move);
      document.body.classList.remove('dragging');
      saveSettings();
      Terms.fit();
    };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', up, {once: true});
  });
}

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

function setAppearance(mode) {
  settings.appearance = mode;
  saveSettings();
  applyAppearance();
}

const APPEARANCE_ICONS = {
  auto:  '<svg viewBox="0 0 16 16"><circle cx="8" cy="8" r="5.5" fill="none" stroke-width="1.3"/><path d="M8 2.5a5.5 5.5 0 0 1 0 11z"/></svg>',
  light: '<svg viewBox="0 0 16 16"><circle cx="8" cy="8" r="3"/><path d="M8 1v2M8 13v2M1 8h2M13 8h2M3 3l1.4 1.4M11.6 11.6L13 13M3 13l1.4-1.4M11.6 4.4L13 3" stroke-width="1.3"/></svg>',
  dark:  '<svg viewBox="0 0 16 16"><path d="M13.5 10A6 6 0 0 1 6 2.5a6 6 0 1 0 7.5 7.5z"/></svg>'
};

function updateAppearanceButton() {
  const b = $('appearance-btn');
  if (!b) return;
  const m = settings.appearance;
  b.innerHTML = APPEARANCE_ICONS[m] || APPEARANCE_ICONS.auto;
  b.title = 'Appearance: ' + ({auto: 'Auto (follows the system)', light: 'Light', dark: 'Dark'})[m] + ' - click to change';
}

function applyAppearance() {
  updateAppearanceButton();
  document.documentElement.dataset.theme = settings.appearance;
  Editor.reconfigure();
  Terms.retheme();
}

function initSettingsUI() {
  const sel = $('set-cmtheme');
  for (const k in THEMES) {
    const o = document.createElement('option');
    o.value = k;
    o.textContent = THEMES[k][0];
    sel.appendChild(o);
  }
  sel.value = settings.cmTheme;
  $('set-fontsize').value = settings.fontSize;
  $('set-indent').value = settings.indent;
  $('set-wrap').checked = settings.wrap;

  sel.onchange = (e) => { settings.cmTheme = e.target.value; saveSettings(); Editor.reconfigure(); };
  $('set-fontsize').onchange = (e) => {
    settings.fontSize = clamp(parseInt(e.target.value) || 13, 9, 28);
    e.target.value = settings.fontSize;
    saveSettings(); Editor.reconfigure(); Terms.retheme();
  };
  $('set-indent').onchange = (e) => { settings.indent = e.target.value; saveSettings(); Editor.reconfigure(); };
  $('set-wrap').onchange = (e) => { settings.wrap = e.target.checked; saveSettings(); Editor.reconfigure(); };
  $('show-keys').onclick = showKeys;

  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
    if (settings.appearance === 'auto') applyAppearance();
  });
}

function showKeys() {
  const m = isMac ? '⌘' : 'Ctrl', a = isMac ? '⌥' : 'Alt', sh = isMac ? '⇧' : 'Shift';
  const keys = [
    ['Save', m + '+S'],
    ['Save As', sh + '+' + m + '+S'],
    ['Run current file', m + '+R'],
    ['Find / Replace', m + '+F'],
    ['Find next / previous', m + '+G / ' + sh + '+' + m + '+G'],
    ['Go to line', isMac ? '⌘+L' : 'Ctrl+Alt+G'],
    ['Select next match', m + '+D'],
    ['Toggle comment', m + '+/'],
    ['Indent / dedent', 'Tab / ' + sh + '+Tab'],
    ['Move line up / down', a + '+↑ / ' + a + '+↓'],
    ['Copy line up / down', sh + '+' + a + '+↑ / ↓'],
    ['Delete line', sh + '+' + m + '+K'],
    ['Add cursor', a + '+Click'],
    ['Fold / unfold', m + '+' + sh + '+[ / ]'],
    ['Autocomplete', 'Ctrl+Space'],
    ['Leave editor (focus next)', 'Esc, then Tab']
  ];
  const t = document.createElement('table');
  t.className = 'keys-table';
  t.innerHTML = keys.map((k) => '<tr><td>' + esc(k[0]) + '</td><td>' + esc(k[1]) + '</td></tr>').join('');
  modal({title: 'Keyboard Shortcuts', node: t, noCancel: true});
}

/* ------------------------------------------------------------------ *
 * menu bar
 * ------------------------------------------------------------------ */

const hasView = () => !!(Editor.active && Editor.active.view);
const hasEditable = () => !!(Editor.active && !Editor.active.readOnly && Editor.active.getText);
const keyName = (k) => isMac ? k.replace(/Shift\+/, '⇧').replace(/Alt\+/, '⌥').replace(/Mod\+/, '⌘')
                             : k.replace(/Mod\+/, 'Ctrl+');

/* run a CodeMirror command from the bundle on the active editor */
function cmd(mod, name) {
  return () => {
    const t = Editor.active;
    if (!t || !t.view) return;
    t.view.focus();
    CM.b[mod][name](t.view);
  };
}

function clip(action) {
  const t = Editor.active;
  if (!t || !t.view) return;
  t.view.focus();
  if (action !== 'paste') { document.execCommand(action); return; }
  if (navigator.clipboard && navigator.clipboard.readText && window.isSecureContext) {
    navigator.clipboard.readText().then((text) => {
      t.view.dispatch(t.view.state.replaceSelection(text));
      t.view.focus();
    }, () => setStatus('Paste with ' + keyName('Mod+V')));
  } else {
    setStatus('The browser only allows paste from the keyboard here: ' + keyName('Mod+V'));
  }
}

function openReplace() {
  cmd('search', 'openSearchPanel')();
  const t = Editor.active;
  const f = t && t.view && t.view.dom.querySelector('.cm-search input[name=replace]');
  if (f) f.focus();
}

function fontStep(d) {
  settings.fontSize = clamp(settings.fontSize + d, 9, 28);
  $('set-fontsize').value = settings.fontSize;
  saveSettings(); Editor.reconfigure(); Terms.retheme();
}

const MENUS = [
  ['File', [
    {label: 'New File...',   action: () => newFile(Tree.targetDir())},
    {label: 'New Folder...', action: () => newFolder(Tree.targetDir())},
    '-',
    {label: 'Save',          key: 'Mod+S',       enabled: hasEditable, action: () => Editor.save(Editor.active)},
    {label: 'Save As...',    key: 'Shift+Mod+S', enabled: hasEditable, action: () => Editor.saveAs(Editor.active)},
    {label: 'Save All',      enabled: () => Editor.tabs.some((t) => t.dirty), action: () => Editor.saveAll()},
    {label: 'Revert to Saved', enabled: hasView, action: () => Editor.revert(Editor.active)},
    '-',
    {label: 'Close Tab',     enabled: () => !!Editor.active, action: () => Editor.close(Editor.active)},
    {label: 'Close All Tabs', enabled: () => Editor.tabs.length > 0, action: async () => {
      for (const t of Editor.tabs.slice()) if (!(await Editor.close(t))) break;
    }},
    '-',
    {label: 'Sign Out',      action: () => $('btn-logout').click()}
  ]],
  ['Edit', [
    {label: 'Undo',          key: 'Mod+Z',       enabled: hasView, action: cmd('commands', 'undo')},
    {label: 'Redo',          key: isMac ? 'Shift+Mod+Z' : 'Mod+Y', enabled: hasView, action: cmd('commands', 'redo')},
    '-',
    {label: 'Cut',           key: 'Mod+X',       enabled: hasView, action: () => clip('cut')},
    {label: 'Copy',          key: 'Mod+C',       enabled: hasView, action: () => clip('copy')},
    {label: 'Paste',         key: 'Mod+V',       enabled: hasView, action: () => clip('paste')},
    {label: 'Select All',    key: 'Mod+A',       enabled: hasView, action: cmd('commands', 'selectAll')},
    '-',
    {label: 'Toggle Comment', key: 'Mod+/',      enabled: hasView, action: cmd('commands', 'toggleComment')},
    {label: 'Indent',        key: 'Tab',         enabled: hasView, action: cmd('commands', 'indentMore')},
    {label: 'Outdent',       key: 'Shift+Tab',   enabled: hasView, action: cmd('commands', 'indentLess')},
    '-',
    {label: 'Move Line Up',  key: 'Alt+↑',  enabled: hasView, action: cmd('commands', 'moveLineUp')},
    {label: 'Move Line Down', key: 'Alt+↓', enabled: hasView, action: cmd('commands', 'moveLineDown')},
    {label: 'Duplicate Line', key: 'Shift+Alt+↓', enabled: hasView, action: cmd('commands', 'copyLineDown')},
    {label: 'Delete Line',   key: 'Shift+Mod+K', enabled: hasView, action: cmd('commands', 'deleteLine')}
  ]],
  ['Find', [
    {label: 'Find...',       key: 'Mod+F',       enabled: hasView, action: cmd('search', 'openSearchPanel')},
    {label: 'Find and Replace...', enabled: hasView, action: openReplace},
    {label: 'Find Next',     key: 'Mod+G',       enabled: hasView, action: cmd('search', 'findNext')},
    {label: 'Find Previous', key: 'Shift+Mod+G', enabled: hasView, action: cmd('search', 'findPrevious')},
    {label: 'Select All Matches', key: 'Mod+Shift+L', enabled: hasView, action: cmd('search', 'selectMatches')},
    '-',
    {label: 'Go to Line...', key: isMac ? 'Mod+L' : 'Ctrl+Alt+G', enabled: hasView, action: cmd('search', 'gotoLine')}
  ]],
  ['View', [
    {label: 'Appearance: Auto',  checked: () => settings.appearance === 'auto',  action: () => setAppearance('auto')},
    {label: 'Appearance: Light', checked: () => settings.appearance === 'light', action: () => setAppearance('light')},
    {label: 'Appearance: Dark',  checked: () => settings.appearance === 'dark',  action: () => setAppearance('dark')},
    '-',
    {label: 'Menu Bar',      checked: () => settings.showMenu,  action: () => showPanel('menu')},
    {label: 'Navigator',     checked: () => settings.showNav,   action: () => showPanel('nav')},
    {label: 'Terminal Area', checked: () => settings.showDebug, action: () => showPanel('debug')},
    {label: 'Inspector',     checked: () => settings.showInsp,  action: () => showPanel('insp')},
    '-',
    {label: 'Wrap Lines',    checked: () => settings.wrap, action: () => {
      settings.wrap = !settings.wrap; $('set-wrap').checked = settings.wrap; saveSettings(); Editor.reconfigure();
    }},
    {label: 'Bigger Text',   action: () => fontStep(1)},
    {label: 'Smaller Text',  action: () => fontStep(-1)},
    '-',
    {label: 'Fold All',      key: 'Mod+Alt+[',   enabled: hasView, action: cmd('language', 'foldAll')},
    {label: 'Unfold All',    key: 'Mod+Alt+]',   enabled: hasView, action: cmd('language', 'unfoldAll')}
  ]],
  ['Run', [
    {label: 'Run',           key: 'Mod+R', enabled: () => canRun(Editor.active), action: runActive},
    {label: 'Stop',          enabled: () => !!Terms.runningRun(), action: () => Terms.stop(Terms.runningRun())},
    '-',
    {label: 'New Terminal',  action: () => Terms.create('shell')},
    {label: 'New Terminal in File\'s Folder', enabled: () => !!Editor.active,
     action: () => Terms.create('shell', {path: dirname(Editor.active.path)})},
    {label: 'New Rampart Session', action: () => Terms.create('repl')},
    '-',
    {label: 'New Terminal in New Tab', action: () => openTermTab('shell')},
    {label: 'New Rampart Session in New Tab', action: () => openTermTab('repl')}
  ]],
  ['Help', [
    {label: 'Rampart Documentation', action: () => DocSearch.open('')},
    {label: 'Keyboard Shortcuts', action: showKeys}
  ]]
];

const Menu = {
  openEl: null,

  build() {
    const bar = $('menubar');
    /* keep focus (and the selection) in the editor while using the menus */
    bar.addEventListener('mousedown', (e) => e.preventDefault());
    for (const [title, items] of MENUS) {
      const m = document.createElement('div');
      m.className = 'menu';
      const b = document.createElement('button');
      b.className = 'menu-title';
      b.textContent = title;
      b.onmousedown = () => { if (this.openEl === m) this.close(); else this.show(m, items); };
      b.onmouseenter = () => { if (this.openEl && this.openEl !== m) this.show(m, items); };
      m.appendChild(b);
      bar.appendChild(m);
    }
    /* light / dark toggle at the right end: auto -> light -> dark */
    const ab = document.createElement('button');
    ab.className = 'appearance-btn';
    ab.id = 'appearance-btn';
    ab.onclick = () => setAppearance(({auto: 'light', light: 'dark', dark: 'auto'})[settings.appearance] || 'auto');
    bar.appendChild(ab);
    updateAppearanceButton();
    document.addEventListener('mousedown', (e) => {
      if (this.openEl && !$('menubar').contains(e.target)) this.close();
    });
    document.addEventListener('keydown', (e) => {
      if (this.openEl && e.key === 'Escape') { e.preventDefault(); this.close(); }
    }, true);
  },

  show(m, items) {
    this.close();
    const drop = document.createElement('div');
    drop.className = 'menu-drop';
    for (const it of items) {
      if (it === '-') { drop.appendChild(document.createElement('hr')); continue; }
      const b = document.createElement('button');
      const check = it.checked ? (it.checked() ? '✓' : '') : null;
      b.innerHTML = '<span>' + (check !== null ? '<span class="check">' + check + '</span>' : '') +
        esc(it.label) + '</span>' + (it.key ? '<span class="key">' + esc(keyName(it.key)) + '</span>' : '');
      b.disabled = it.enabled ? !it.enabled() : false;
      b.onclick = () => { this.close(); it.action(); };
      drop.appendChild(b);
    }
    m.appendChild(drop);
    m.classList.add('open');
    this.openEl = m;
  },

  close() {
    if (!this.openEl) return;
    const d = this.openEl.querySelector('.menu-drop');
    if (d) d.remove();
    this.openEl.classList.remove('open');
    this.openEl = null;
  }
};

/* ------------------------------------------------------------------ *
 * documentation search (ide/apps/docs/rsearch.js)
 * ------------------------------------------------------------------ */

const DocSearch = {
  items: [],
  sel: -1,
  timer: null,
  seq: 0,

  /* open the docs in their own tab; page is relative to /ide/docs/ */
  open(page) {
    window.open('/ide/docs/' + (page || ''), 'rampart-docs');
  },

  /* full search: the docs pages run it themselves from ?q= */
  search(q) {
    if (!q) return this.open('');
    this.open('index.html?q=' + encodeURIComponent(q));
  },

  async suggest(q) {
    const my = ++this.seq;
    let res;
    try {
      const r = await fetch('/apps/docs/rsearch/suggest.json?query=' + encodeURIComponent(q), {credentials: 'same-origin'});
      res = await r.json();
    } catch (e) {
      res = null;
    }
    if (my !== this.seq || $('docsearch-input').value.trim() !== q) return;   /* stale */
    this.items = ((res && res.suggestions) || []).slice(0, 15)
      .map((s) => typeof s === 'string' ? {value: s, data: 'search'} : s);
    this.render();
  },

  render() {
    const list = $('docsearch-list');
    list.textContent = '';
    this.sel = -1;
    if (!this.items.length) { list.hidden = true; return; }
    this.items.forEach((it, i) => {
      const d = document.createElement('div');
      const isWord = it.data === 'search';
      d.className = 'sug' + (isWord ? ' word' : '');
      if (isWord) {
        d.innerHTML = esc(it.value) + ' <span class="where">— search</span>';
      } else {
        /* "page : section : subsection" - show the last part first */
        const parts = String(it.value).split(' : ');
        const last = parts.pop();
        d.innerHTML = esc(last) + (parts.length ? ' <span class="where">— ' + esc(parts.join(' › ')) + '</span>' : '');
      }
      d.title = it.value;
      d.onmousedown = (e) => { e.preventDefault(); this.pick(i); };
      list.appendChild(d);
    });
    list.hidden = false;
  },

  move(d) {
    if (!this.items.length) return;
    /* cycle through -1 (the typed text) and 0..n-1 */
    const n = this.items.length + 1;
    this.sel = (this.sel + 1 + d + n) % n - 1;
    const rows = $('docsearch-list').children;
    for (let i = 0; i < rows.length; i++) rows[i].classList.toggle('on', i === this.sel);
    if (this.sel >= 0) rows[this.sel].scrollIntoView({block: 'nearest'});
  },

  pick(i) {
    const it = this.items[i];
    this.close();
    if (!it) return;
    if (it.data === 'search') {
      $('docsearch-input').value = it.value;
      this.search(it.value);
    } else {
      this.open(it.data);
    }
  },

  close() {
    $('docsearch-list').hidden = true;
    this.items = [];
    this.sel = -1;
    this.seq++;
  },

  init() {
    const input = $('docsearch-input');
    input.addEventListener('input', () => {
      clearTimeout(this.timer);
      const q = input.value.trim();
      if (!q) { this.close(); return; }
      this.timer = setTimeout(() => this.suggest(q), 150);
    });
    input.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowDown') { e.preventDefault(); this.move(1); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); this.move(-1); }
      else if (e.key === 'Escape') { this.close(); }
      else if (e.key === 'Enter') {
        e.preventDefault();
        clearTimeout(this.timer);
        if (this.sel >= 0) this.pick(this.sel);
        else { const q = input.value.trim(); this.close(); this.search(q); }
      }
    });
    input.addEventListener('blur', () => setTimeout(() => this.close(), 100));
    $('btn-docs').onclick = () => this.open('');
  }
};

/* ------------------------------------------------------------------ *
 * live file tree: the server watches the directories shown in the tree
 * (ide/wsapps/watch.js) and says which one changed
 * ------------------------------------------------------------------ */

const Watch = {
  ws: null,
  sent: '',
  retry: 1000,
  timer: null,

  start() {
    if (App.termOnly || this.ws) return;
    clearTimeout(this.timer);
    this.retry = 1000;
    this.connect();
  },

  connect() {
    const url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ide/wsapps/watch.js';
    let ws;
    try { ws = new WebSocket(url); } catch (e) { return this.later(); }
    this.ws = ws;
    this.sent = '';
    ws.onopen = () => { this.retry = 1000; this.sync(); };
    ws.onmessage = (e) => {
      let m;
      try { m = JSON.parse(e.data); } catch (x) { return; }
      if (typeof m.changed === 'string') Tree.refreshDir(m.changed);
    };
    ws.onclose = () => { if (this.ws === ws) { this.ws = null; this.later(); } };
  },

  /* reconnect with backoff, e.g. after a server restart or while signed out */
  later() {
    clearTimeout(this.timer);
    this.timer = setTimeout(() => { this.timer = null; this.connect(); }, this.retry);
    this.retry = Math.min(this.retry * 2, 30000);
  },

  /* the root plus every expanded folder whose contents are in the tree */
  dirs() {
    const out = [''];
    for (const row of $('tree').querySelectorAll('.row[data-dir]')) {
      const n = row.nextElementSibling;
      if (n && n.classList.contains('children')) out.push(row.dataset.path);
    }
    return out;
  },

  sync() {
    if (!this.ws || this.ws.readyState !== 1) return;
    const msg = JSON.stringify({watch: this.dirs()});
    if (msg === this.sent) return;
    this.sent = msg;
    this.ws.send(msg);
  }
};

/* ------------------------------------------------------------------ *
 * login and startup
 * ------------------------------------------------------------------ */

const App = {root: '', rootPath: '', started: false,
             /* /ide/?term=shell|repl: a tab holding only terminals */
             termOnly: ({shell: 'shell', repl: 'repl'})[new URLSearchParams(location.search).get('term')] || null};

function openTermTab(mode) {
  window.open('/ide/?term=' + mode, '_blank');
}

/* right click or press-and-hold on a terminal button: offer a new tab */
function termButtonMenu(btn, mode) {
  const label = mode === 'repl' ? 'Rampart Session' : 'Terminal';
  const show = (x, y) => contextMenu(x, y, [
    {label: 'Open ' + label + ' Here', action: () => Terms.create(mode)},
    {label: 'Open ' + label + ' in New Tab', action: () => openTermTab(mode)}
  ]);
  let timer = null, held = false;
  btn.addEventListener('contextmenu', (e) => { e.preventDefault(); clearTimeout(timer); show(e.clientX, e.clientY); });
  btn.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    held = false;
    const x = e.clientX, y = e.clientY;
    timer = setTimeout(() => { held = true; show(x, y); }, 500);
  });
  for (const ev of ['pointerup', 'pointerleave', 'pointercancel'])
    btn.addEventListener(ev, () => clearTimeout(timer));
  btn.addEventListener('click', (e) => {
    if (held) { held = false; return; }      /* the long press already showed the menu */
    Terms.create(mode);
  });
}

/* terminal-only tab: everything but the terminal area is hidden */
function startTermOnly() {
  document.body.classList.add('term-only');
  document.title = App.termOnly === 'repl' ? 'rampart - Mini IDE' : 'Terminal - Mini IDE';
  const s = Terms.create(App.termOnly);
  s.term.onTitleChange((t) => { if (t) document.title = t; });
}

function showLogin(host) {
  $('app').hidden = true;
  $('login').hidden = false;
  if (host) $('login-sub').textContent = host;
  $('login-pass').value = '';
  $('login-pass').focus();
}

$('login-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  $('login-error').textContent = '';
  try {
    await api('login', {password: $('login-pass').value});
  } catch (err) {
    $('login-error').textContent = err.status === 429 ? 'Too many attempts, wait a minute.' : err.message;
    $('login-pass').select();
    return;
  }
  boot();
});

async function boot() {
  let st;
  try {
    st = await api('status');
  } catch (e) {
    document.body.textContent = 'Mini IDE: ' + e.message;
    return;
  }
  if (!st.loggedIn) return showLogin(st.host);

  App.root = st.root;
  App.rootPath = st.rootPath;
  $('login').hidden = true;
  $('app').hidden = false;
  $('root-name').textContent = st.root;
  $('root-name').title = st.rootPath + ' on ' + st.host;
  $('nav-hidden').classList.toggle('on', Tree.hidden);
  if (App.started) { if (!App.termOnly) { await Tree.refresh(); Watch.start(); } return; }
  App.started = true;
  if (App.termOnly) return startTermOnly();

  applyLayout();
  await Tree.refresh();
  for (const p of store.get('openTabs', [])) await Editor.open(p, true);
  const at = Editor.find(store.get('activeTab', ''));
  if (at) Editor.activate(at);
  updateRunButtons();
  Watch.start();
  loadCM().catch(() => {});
}

/* ------------------------------------------------------------------ *
 * event wiring
 * ------------------------------------------------------------------ */

$('tree').addEventListener('click', (e) => {
  const row = e.target.closest('.row');
  if (!row) return;
  const p = row.dataset.path;
  Tree.select(p);
  if (row.dataset.dir) Tree.toggle(row);
  else Editor.open(p);
});
$('tree').addEventListener('contextmenu', (e) => {
  const row = e.target.closest('.row');
  if (row) Tree.select(row.dataset.path);
  treeMenu(e, row ? row.dataset.path : '');
});
$('tree').addEventListener('keydown', (e) => {
  if (Tree.selected === null) return;
  if (e.key === 'Delete' || (isMac && e.key === 'Backspace' && e.metaKey)) { e.preventDefault(); deletePath(Tree.selected); }
  else if (e.key === 'F2') { e.preventDefault(); renamePath(Tree.selected); }
});

$('nav-newfile').onclick = () => newFile(Tree.targetDir());
$('nav-newfolder').onclick = () => newFolder(Tree.targetDir());
$('nav-refresh').onclick = () => Tree.refresh();
$('nav-hidden').onclick = () => {
  Tree.hidden = !Tree.hidden;
  store.set('hidden', Tree.hidden);
  $('nav-hidden').classList.toggle('on', Tree.hidden);
  Tree.refresh();
};
$('nav-filter').addEventListener('input', () => Tree.applyFilter());

$('btn-run').onclick = runActive;
$('btn-stop').onclick = () => Terms.stop(Terms.runningRun());
$('run-args').addEventListener('keydown', (e) => { if (e.key === 'Enter') runActive(); });
termButtonMenu($('btn-shell'), 'shell');
termButtonMenu($('btn-repl'), 'repl');
$('debug-newshell').onclick = () => Terms.create('shell');
$('debug-clear').onclick = () => { if (Terms.active) { Terms.active.term.clear(); Terms.active.term.focus(); } };
$('toggle-menu').onclick = () => showPanel('menu');
$('toggle-nav').onclick = () => showPanel('nav');
$('toggle-debug').onclick = () => showPanel('debug');
$('toggle-insp').onclick = () => showPanel('insp');
$('btn-logout').onclick = async () => {
  const dirty = Editor.tabs.some((t) => t.dirty);
  const ok = await confirmBox('Sign out?',
    (dirty ? 'There are unsaved changes, which will be lost.  ' : '') +
    'This signs out every browser signed in to this IDE.',
    'Sign Out', true);
  if (!ok) return;
  try { await api('logout'); } catch (e) { /* ignore */ }
  location.reload();
};

splitter('split-nav', (e) => {
  settings.navWidth = clamp(e.clientX - $('navigator').getBoundingClientRect().left, 140, 600);
});
splitter('split-insp', (e) => {
  settings.inspWidth = clamp($('inspector').getBoundingClientRect().right - e.clientX, 170, 600);
});
splitter('split-debug', (e) => {
  const r = $('center').getBoundingClientRect();
  settings.debugHeight = clamp(r.bottom - e.clientY, 60, r.height - 100);
});

new ResizeObserver(() => Terms.fit()).observe($('terms'));

/* global shortcuts when focus is outside the editor and terminals */
document.addEventListener('keydown', (e) => {
  if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
  if (e.target.closest && e.target.closest('.terms, .cm-editor')) return;
  const k = e.key.toLowerCase();
  if (k === 's') { e.preventDefault(); if (e.shiftKey) Editor.saveAs(Editor.active); else Editor.save(Editor.active); }
  else if (k === 'r') { e.preventDefault(); runActive(); }
});

window.addEventListener('beforeunload', (e) => {
  if (Editor.tabs.some((t) => t.dirty)) { e.preventDefault(); e.returnValue = ''; }
});

/* pick up files created from a terminal when coming back to the window */
let lastFocusRefresh = 0;
window.addEventListener('focus', () => {
  if (!App.started || $('app').hidden || Date.now() - lastFocusRefresh < 2000) return;
  lastFocusRefresh = Date.now();
  Tree.refresh();
});

/* never let the dialog form submit natively (that would reload the page) */
$('modal-form').addEventListener('submit', (e) => e.preventDefault());

document.documentElement.dataset.theme = settings.appearance;
initSettingsUI();
Menu.build();
DocSearch.init();
window.MiniIDE = {Editor, Tree, Terms};   /* for debugging from the browser console */
boot();
