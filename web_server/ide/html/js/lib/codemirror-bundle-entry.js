// Bundle entry point for CodeMirror
// Exports everything the file manager needs

// Core
export * as codemirror from 'codemirror';
export * as state from '@codemirror/state';

// Language registry
export * as languageData from '@codemirror/language-data';

// Individual languages (pre-bundled for offline use)
export * as langJavascript from '@codemirror/lang-javascript';
export * as langHtml from '@codemirror/lang-html';
export * as langCss from '@codemirror/lang-css';
export * as langJson from '@codemirror/lang-json';
export * as langMarkdown from '@codemirror/lang-markdown';
export * as langPython from '@codemirror/lang-python';
export * as langXml from '@codemirror/lang-xml';
export * as langSql from '@codemirror/lang-sql';
export * as langCpp from '@codemirror/lang-cpp';
export * as langJava from '@codemirror/lang-java';
export * as langPhp from '@codemirror/lang-php';
export * as langRust from '@codemirror/lang-rust';
export * as langYaml from '@codemirror/lang-yaml';
export * as legacyShell from '@codemirror/legacy-modes/mode/shell';

// Themes
export * as themeOneDark from '@codemirror/theme-one-dark';
export * as themeDracula from '@uiw/codemirror-theme-dracula';
export * as themeGithub from '@uiw/codemirror-theme-github';
export * as themeMaterial from '@uiw/codemirror-theme-material';
export * as themeMonokai from '@uiw/codemirror-theme-monokai';
export * as themeNord from '@uiw/codemirror-theme-nord';
export * as themeSolarized from '@uiw/codemirror-theme-solarized';
export * as themeTokyoNight from '@uiw/codemirror-theme-tokyo-night';
export * as themeVscode from '@uiw/codemirror-theme-vscode';

// Additions for the Mini IDE: keymaps, indentation and search commands
export * as view from '@codemirror/view';
export * as language from '@codemirror/language';
export * as commands from '@codemirror/commands';
export * as search from '@codemirror/search';
