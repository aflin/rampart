# Third-Party Licenses

This directory contains locally hosted copies of third-party libraries.
All libraries are used under their respective open-source licenses.

## xterm.js (v5.3.0)
- License: MIT
- https://github.com/xtermjs/xterm.js

## xterm-addon-fit (v0.8.0)
- License: MIT
- https://github.com/xtermjs/xterm.js

## codemirror (v6) and extensions
- License: MIT
- https://github.com/codemirror/dev
- Includes: codemirror, @codemirror/state, @codemirror/language-data,
  @codemirror/theme-one-dark, @codemirror/lang-javascript, @codemirror/lang-html,
  @codemirror/lang-css, @codemirror/lang-json, @codemirror/lang-markdown,
  @codemirror/lang-python, @codemirror/lang-xml, @codemirror/lang-sql,
  @codemirror/lang-cpp, @codemirror/lang-java, @codemirror/lang-php,
  @codemirror/lang-rust, @codemirror/lang-yaml, @codemirror/legacy-modes,
  @codemirror/view, @codemirror/language, @codemirror/commands, @codemirror/search
- codemirror-bundle.js is built from codemirror-bundle-entry.js with
  `esbuild codemirror-bundle-entry.js --bundle --format=esm --minify`

## @uiw/codemirror-themes
- License: MIT
- https://github.com/uiwjs/react-codemirror
- Includes: dracula, github, material, monokai, nord, solarized, tokyo-night, vscode
