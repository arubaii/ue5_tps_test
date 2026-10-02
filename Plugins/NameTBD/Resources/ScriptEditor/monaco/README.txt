Monaco Editor 0.52.2 (https://github.com/microsoft/monaco-editor), MIT License - see LICENSE.txt.
Only the files the Script area uses are kept: from the package's min/vs, the loader, the editor bundle and its stylesheet, the
worker and the icon font. Its own language support is not included: the page registers a Python tokenizer of its own.
To update it: npm pack monaco-editor@<version>, and copy the same files from package/min/vs.
