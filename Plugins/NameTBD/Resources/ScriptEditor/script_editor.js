// Copyright AR GameSystems 2026. All rights reserved.
//
// The Script area's code editor: Monaco set up for the plugin's Python scripts, and its two-way link with Unreal.
//
// Unreal -> page: SWidgetAnimMonacoEditor calls the functions on window.widgetAnimEditor (setText, setOptions, setError,
// setCompletions, setWidgets, focus) with ExecuteJavascript.
//
// Page -> Unreal: Unreal binds a UWidgetAnimScriptEditorBridge as window.ue.scripteditor, whose UFUNCTIONs show up here in
// lower case: ready(), textchanged(text), run(text), cursormoved(line, column), save(). The binding may arrive a moment
// after the page loads, so nothing calls it until it is there.
//
// Python is colored by a tokenizer of its own (language "nametbd-python"), with the same groups the plain code box uses -
// keywords, constants, strings, comments, numbers, builtins, the scripting API's names, what def and class name. Suggestions
// and hover help come from the scripting API itself: Unreal hands over a description of widget_anim_scripting's classes
// (setCompletions), and the type of what is before a "." is worked out from the script - anim is an Animation, what
// anim.add_track returns is a Track, the loop variable over a widget's children is a Widget.

(function () {
	"use strict";

	var KEYWORDS = ["and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else",
		"except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass",
		"raise", "return", "try", "while", "with", "yield", "match", "case"];
	var CONSTANTS = ["True", "False", "None"];
	var BUILTINS = ["print", "len", "range", "enumerate", "zip", "list", "dict", "set", "tuple", "int", "float", "str", "bool",
		"min", "max", "abs", "round", "sorted", "reversed", "sum", "any", "all", "isinstance", "getattr", "setattr", "hasattr",
		"type", "super", "map", "filter", "repr", "divmod", "pow", "self", "Exception", "ValueError", "KeyError", "IndexError",
		"TypeError", "RuntimeError"];
	var API = ["anim", "selection", "unreal", "TrackType", "Ease", "Curve", "Starts", "Timing", "Direction", "Stagger",
		"Spacing", "Order", "Falloff", "Smoothing", "ScriptError", "Slot"];

	var state = {
		editor: null,
		model: null,
		ready: false,
		options: null,
		completions: null,
		widgets: [],
		pendingText: null,
		suppressChange: false,
		decorations: []
	};

	// ---------------------------------------------------------------------------------------------
	// Talking to Unreal
	// ---------------------------------------------------------------------------------------------

	function bridge() {
		return window.ue && window.ue.scripteditor ? window.ue.scripteditor : null;
	}

	function call(name) {
		var target = bridge();
		if (!target || typeof target[name] !== "function") {
			return;
		}
		try {
			target[name].apply(target, Array.prototype.slice.call(arguments, 1));
		} catch (error) {
			console.error("widgetAnimEditor: " + name + " failed: " + error);
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Python, colored
	// ---------------------------------------------------------------------------------------------

	function registerLanguage() {
		monaco.languages.register({ id: "nametbd-python", extensions: [".py"], aliases: ["Python"] });

		monaco.languages.setMonarchTokensProvider("nametbd-python", {
			keywords: KEYWORDS,
			constants: CONSTANTS,
			builtins: BUILTINS,
			api: API,
			tokenizer: {
				root: [
					[/#.*$/, "comment"],
					[/\b(def|class)(\s+)([A-Za-z_]\w*)/, ["keyword", "white", "definition"]],
					[/[rRbBuUfF]{0,2}"""/, { token: "string", next: "@tripleDouble" }],
					[/[rRbBuUfF]{0,2}'''/, { token: "string", next: "@tripleSingle" }],
					[/[rRbBuUfF]{0,2}"([^"\\]|\\.)*"/, "string"],
					[/[rRbBuUfF]{0,2}'([^'\\]|\\.)*'/, "string"],
					[/[rRbBuUfF]{0,2}"([^"\\]|\\.)*$/, "string"],
					[/[rRbBuUfF]{0,2}'([^'\\]|\\.)*$/, "string"],
					[/0[xXoObB][0-9a-fA-F_]+/, "number"],
					[/(\d[\d_]*(\.[\d_]*)?|\.\d[\d_]*)([eE][+-]?\d+)?[jJ]?/, "number"],
					[/[A-Za-z_]\w*/, {
						cases: {
							"@keywords": "keyword",
							"@constants": "constant",
							"@api": "api",
							"@builtins": "builtin",
							"@default": "identifier"
						}
					}],
					[/\s+/, "white"],
					[/[^\w\s#"']+/, "operator"]
				],
				tripleDouble: [[/"""/, "string", "@pop"], [/[^"]+/, "string"], [/"/, "string"]],
				tripleSingle: [[/'''/, "string", "@pop"], [/[^']+/, "string"], [/'/, "string"]]
			}
		});

		monaco.languages.setLanguageConfiguration("nametbd-python", {
			comments: { lineComment: "#" },
			brackets: [["{", "}"], ["[", "]"], ["(", ")"]],
			autoClosingPairs: [
				{ open: "{", close: "}" },
				{ open: "[", close: "]" },
				{ open: "(", close: ")" },
				{ open: "\"", close: "\"", notIn: ["string"] },
				{ open: "'", close: "'", notIn: ["string", "comment"] }
			],
			surroundingPairs: [
				{ open: "{", close: "}" },
				{ open: "[", close: "]" },
				{ open: "(", close: ")" },
				{ open: "\"", close: "\"" },
				{ open: "'", close: "'" }
			],
			onEnterRules: [
				// A line ending in a colon starts a block.
				{ beforeText: /:\s*(#.*)?$/, action: { indentAction: monaco.languages.IndentAction.Indent } }
			],
			folding: { offSide: true }
		});
	}

	// ---------------------------------------------------------------------------------------------
	// Themes
	// ---------------------------------------------------------------------------------------------

	function hex(color) {
		return (color || "#cccccc").replace("#", "");
	}

	/** Token colors for the plugin's own groups, which Monaco's built-in themes know nothing of. */
	function tokenRules(colors) {
		return [
			{ token: "", foreground: hex(colors.foreground) },
			{ token: "identifier", foreground: hex(colors.foreground) },
			{ token: "keyword", foreground: hex(colors.keyword) },
			{ token: "constant", foreground: hex(colors.constant) },
			{ token: "string", foreground: hex(colors.string) },
			{ token: "comment", foreground: hex(colors.comment), fontStyle: "italic" },
			{ token: "number", foreground: hex(colors.number) },
			{ token: "builtin", foreground: hex(colors.builtin) },
			{ token: "api", foreground: hex(colors.api), fontStyle: "bold" },
			{ token: "definition", foreground: hex(colors.definition) },
			{ token: "operator", foreground: hex(colors.operator) }
		];
	}

	var THEME_COLORS = {
		dark: {
			foreground: "#d4d4d4", keyword: "#c586c0", constant: "#569cd6", string: "#ce9178", comment: "#6a9955",
			number: "#b5cea8", builtin: "#4ec9b0", api: "#ebab34", definition: "#dcdcaa", operator: "#d4d4d4"
		},
		light: {
			foreground: "#1f1f1f", keyword: "#af00db", constant: "#0000ff", string: "#a31515", comment: "#008000",
			number: "#098658", builtin: "#267f99", api: "#b35c00", definition: "#795e26", operator: "#1f1f1f"
		},
		highContrast: {
			foreground: "#ffffff", keyword: "#c586c0", constant: "#569cd6", string: "#ce9178", comment: "#7ca668",
			number: "#b5cea8", builtin: "#4ec9b0", api: "#ffb84d", definition: "#dcdcaa", operator: "#ffffff"
		}
	};

	function defineThemes(unrealColors) {
		var colors = unrealColors || THEME_COLORS.dark;
		monaco.editor.defineTheme("nametbd-unreal", {
			base: "vs-dark",
			inherit: true,
			rules: tokenRules(colors),
			colors: {
				"editor.background": colors.background || "#1c1c1c",
				"editor.foreground": colors.foreground || "#d6d6d6",
				"editorGutter.background": colors.background || "#1c1c1c",
				"editorLineNumber.foreground": colors.lineNumber || "#5a5a5a",
				"editorLineNumber.activeForeground": colors.lineNumberActive || "#a0a0a0",
				"editor.lineHighlightBackground": colors.lineHighlight || "#262626",
				"editor.lineHighlightBorder": colors.lineHighlight || "#262626",
				"editor.selectionBackground": colors.selection || "#264f78",
				"editorCursor.foreground": colors.cursor || "#ebab34",
				"editorWidget.background": colors.widget || "#252526",
				"minimap.background": colors.background || "#1c1c1c",
				"scrollbarSlider.background": "#ffffff1a"
			}
		});
		monaco.editor.defineTheme("nametbd-dark", { base: "vs-dark", inherit: true, rules: tokenRules(THEME_COLORS.dark), colors: {} });
		monaco.editor.defineTheme("nametbd-light", { base: "vs", inherit: true, rules: tokenRules(THEME_COLORS.light), colors: {} });
		monaco.editor.defineTheme("nametbd-hc", { base: "hc-black", inherit: true, rules: tokenRules(THEME_COLORS.highContrast), colors: {} });
	}

	// ---------------------------------------------------------------------------------------------
	// Working out what is before a "."
	//
	// The type of an expression is followed along its parts: anim is an Animation, and each member's "returns" says what
	// the next part is. A plain name is looked up as a global, else from the last line before the cursor that gave it a
	// value: an assignment, a for loop (over a list, or enumerate of one), or a with ... as.
	// ---------------------------------------------------------------------------------------------

	function typeInfo(typeName) {
		return state.completions && state.completions.types ? state.completions.types[typeName] : null;
	}

	function elementType(typeName) {
		if (!typeName) {
			return null;
		}
		var match = /^list\[(\w+)\]$/.exec(typeName);
		if (match) {
			return match[1];
		}
		var info = typeInfo(typeName);
		return info && info.iterates ? info.iterates : null;
	}

	function memberReturns(typeName, memberName) {
		var info = typeInfo(typeName);
		if (!info) {
			return null;
		}
		for (var index = 0; index < info.members.length; ++index) {
			if (info.members[index].name === memberName) {
				return info.members[index].returns || null;
			}
		}
		return null;
	}

	/** An expression's parts: anim.widget("Row").children[0] -> [anim], [widget, call], [children, index]. */
	function splitExpression(text) {
		var parts = [];
		var depth = 0;
		var current = { name: "", call: false, index: false };
		for (var index = 0; index < text.length; ++index) {
			var char = text[index];
			if (depth === 0 && char === ".") {
				parts.push(current);
				current = { name: "", call: false, index: false };
				continue;
			}
			if (char === "(" || char === "[") {
				if (depth === 0) {
					current[char === "(" ? "call" : "index"] = true;
				}
				++depth;
				continue;
			}
			if (char === ")" || char === "]") {
				depth = Math.max(depth - 1, 0);
				continue;
			}
			if (depth === 0 && /\w/.test(char)) {
				current.name += char;
			}
		}
		parts.push(current);
		return parts;
	}

	function expressionType(text, beforeLine, depthLeft) {
		if (depthLeft <= 0) {
			return null;
		}
		var expression = text.trim();
		var enumerateMatch = /^enumerate\((.*)\)$/.exec(expression);
		if (enumerateMatch) {
			expression = enumerateMatch[1];
		}
		var parts = splitExpression(expression);
		if (!parts.length || !parts[0].name) {
			return null;
		}

		var type = nameType(parts[0].name, beforeLine, depthLeft - 1);
		if (parts[0].index) {
			type = elementType(type);
		}
		for (var index = 1; index < parts.length && type; ++index) {
			type = memberReturns(type, parts[index].name);
			if (parts[index].index) {
				type = elementType(type);
			}
		}
		return type;
	}

	function nameType(name, beforeLine, depthLeft) {
		if (state.completions && state.completions.globals) {
			for (var index = 0; index < state.completions.globals.length; ++index) {
				var global = state.completions.globals[index];
				if (global.name === name && global.type) {
					return global.type;
				}
			}
		}
		if (!state.model || depthLeft <= 0) {
			return null;
		}

		var escaped = name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
		var assignment = new RegExp("^\\s*" + escaped + "\\s*=\\s*(?!=)(.+?)\\s*(#.*)?$");
		var loop = new RegExp("^\\s*for\\s+(?:\\w+\\s*,\\s*)?" + escaped + "\\s+in\\s+(.+?)\\s*:");
		var withAs = new RegExp("^\\s*with\\s+(.+?)\\s+as\\s+" + escaped + "\\s*:");

		for (var line = Math.min(beforeLine, state.model.getLineCount()); line >= 1; --line) {
			var content = state.model.getLineContent(line);
			var match = assignment.exec(content);
			if (match) {
				return expressionType(match[1], line - 1, depthLeft);
			}
			match = loop.exec(content);
			if (match) {
				var iterated = match[1].trim();
				var listType = expressionType(iterated, line - 1, depthLeft);
				return elementType(listType);
			}
			match = withAs.exec(content);
			if (match) {
				return expressionType(match[1], line - 1, depthLeft);
			}
		}
		return null;
	}

	/** The expression ending right before a "." at the end of text, or null. */
	function expressionBeforeDot(text) {
		if (!/\.\w*$/.test(text)) {
			return null;
		}
		var end = text.lastIndexOf(".");
		var depth = 0;
		var start = end;
		while (start > 0) {
			var char = text[start - 1];
			if (char === ")" || char === "]") {
				++depth;
			} else if (char === "(" || char === "[") {
				if (depth === 0) {
					break;
				}
				--depth;
			} else if (depth === 0 && !/[\w.]/.test(char)) {
				break;
			}
			--start;
		}
		var expression = text.substring(start, end);
		return expression.length ? expression : null;
	}

	function isInString(text) {
		var quote = null;
		for (var index = 0; index < text.length; ++index) {
			var char = text[index];
			if (quote) {
				if (char === "\\") {
					++index;
				} else if (char === quote) {
					quote = null;
				}
			} else if (char === "#") {
				return false;
			} else if (char === "\"" || char === "'") {
				quote = char;
			}
		}
		return quote !== null;
	}

	// ---------------------------------------------------------------------------------------------
	// Suggestions and hover help
	// ---------------------------------------------------------------------------------------------

	var Kind = null;

	function memberItem(member, range) {
		var isMethod = member.kind === "method";
		var item = {
			label: member.name,
			kind: isMethod ? Kind.Method : (member.kind === "constant" ? Kind.Constant : Kind.Property),
			detail: member.signature || "",
			documentation: member.doc ? { value: member.doc } : undefined,
			insertText: isMethod ? member.name + "($0)" : member.name,
			insertTextRules: isMethod ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet : undefined,
			range: range,
			sortText: (member.name.charAt(0) === "_" ? "z" : "a") + member.name
		};
		if (isMethod) {
			item.command = { id: "editor.action.triggerParameterHints", title: "" };
		}
		return item;
	}

	function membersOf(typeName) {
		var info = typeInfo(typeName);
		return info ? info.members : null;
	}

	/** Every member of every class, once each by name - for a "." whose type couldn't be worked out. */
	function allMembers() {
		var seen = {};
		var result = [];
		var types = state.completions && state.completions.types ? state.completions.types : {};
		Object.keys(types).forEach(function (typeName) {
			if (types[typeName].isNames) {
				return;
			}
			types[typeName].members.forEach(function (member) {
				if (!seen[member.name]) {
					seen[member.name] = true;
					result.push(member);
				}
			});
		});
		return result;
	}

	function provideCompletions(model, position) {
		if (!state.options || !state.options.suggestions || !state.completions) {
			return { suggestions: [] };
		}

		var before = model.getLineContent(position.lineNumber).substring(0, position.column - 1);
		var word = model.getWordUntilPosition(position);
		var range = {
			startLineNumber: position.lineNumber,
			endLineNumber: position.lineNumber,
			startColumn: word.startColumn,
			endColumn: word.endColumn
		};

		// In a string that names a widget - target="...", anim.widget("..."), GRID = "..." - the widgets.
		if (isInString(before)) {
			var namesWidget = /(target\s*=\s*|into\s*=\s*|template\s*=\s*|\.widget\(\s*|group_for\(\s*|_widget\(\s*|fill_grid\(\s*|layout_grid\(\s*|\bfind\([^)]*target\s*=\s*|^\s*[A-Z][A-Z0-9_]*\s*=\s*)[rRuU]?["'][^"']*$/.test(before);
			if (!namesWidget) {
				return { suggestions: [] };
			}
			return {
				suggestions: state.widgets.map(function (widget) {
					return { label: widget.name, kind: Kind.Value, detail: widget.path === widget.name ? widget.cls : widget.cls + " - " + widget.path, insertText: widget.path, range: range, filterText: widget.name };
				})
			};
		}

		var expression = expressionBeforeDot(before);
		if (expression) {
			var type = expressionType(expression, position.lineNumber - 1, 6);
			var members = (type && membersOf(type)) || allMembers();
			return { suggestions: members.map(function (member) { return memberItem(member, range); }) };
		}

		// A name of its own: what a script is given, Python's keywords and builtins.
		var suggestions = [];
		(state.completions.globals || []).forEach(function (global) {
			var isFunction = global.kind === "function";
			suggestions.push({
				label: global.name,
				kind: isFunction ? Kind.Function : (global.kind === "class" ? Kind.Class : (global.kind === "module" ? Kind.Module : Kind.Variable)),
				detail: global.detail || global.type || "",
				documentation: global.doc ? { value: global.doc } : undefined,
				insertText: isFunction ? global.name + "($0)" : global.name,
				insertTextRules: isFunction ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet : undefined,
				range: range,
				sortText: "a" + global.name
			});
		});
		KEYWORDS.concat(CONSTANTS).forEach(function (keyword) {
			suggestions.push({ label: keyword, kind: Kind.Keyword, insertText: keyword, range: range, sortText: "c" + keyword });
		});
		BUILTINS.forEach(function (builtin) {
			suggestions.push({ label: builtin, kind: Kind.Function, insertText: builtin, range: range, sortText: "b" + builtin });
		});
		return { suggestions: suggestions };
	}

	function provideHover(model, position) {
		if (!state.completions) {
			return null;
		}
		var word = model.getWordAtPosition(position);
		if (!word) {
			return null;
		}
		var line = model.getLineContent(position.lineNumber);
		var before = line.substring(0, word.startColumn - 1);
		var range = new monaco.Range(position.lineNumber, word.startColumn, position.lineNumber, word.endColumn);

		var expression = /\.$/.test(before) ? expressionBeforeDot(before) : null;

		var member = null;
		var owner = null;
		if (expression) {
			owner = expressionType(expression, position.lineNumber - 1, 6);
			var members = (owner && membersOf(owner)) || allMembers();
			for (var index = 0; index < members.length; ++index) {
				if (members[index].name === word.word) {
					member = members[index];
					break;
				}
			}
		} else {
			(state.completions.globals || []).forEach(function (global) {
				if (global.name === word.word) {
					member = { name: global.name, signature: global.detail || global.type, doc: global.doc };
				}
			});
		}
		if (!member) {
			return null;
		}

		var contents = [{ value: "```python\n" + (owner ? owner + "." : "") + member.name + (member.signature || "") + "\n```" }];
		if (member.doc) {
			contents.push({ value: member.doc });
		}
		return { range: range, contents: contents };
	}

	// ---------------------------------------------------------------------------------------------
	// The editor
	// ---------------------------------------------------------------------------------------------

	function editorOptions(options) {
		return {
			fontSize: options.fontSize,
			fontFamily: options.fontFamily,
			lineNumbers: options.lineNumbers ? "on" : "off",
			minimap: { enabled: !!options.minimap },
			wordWrap: options.wordWrap ? "on" : "off",
			autoClosingBrackets: options.autoClose ? "languageDefined" : "never",
			autoClosingQuotes: options.autoClose ? "languageDefined" : "never",
			quickSuggestions: options.suggestions ? { other: true, comments: false, strings: true } : false,
			suggestOnTriggerCharacters: !!options.suggestions,
			wordBasedSuggestions: "off",
			renderWhitespace: options.renderWhitespace ? "all" : "none",
			glyphMargin: true,
			folding: true,
			links: false,
			scrollBeyondLastLine: false,
			fixedOverflowWidgets: true,
			automaticLayout: true,
			smoothScrolling: true,
			mouseWheelZoom: true,
			contextmenu: true,
			tabSize: options.tabSize
		};
	}

	function applyOptions() {
		if (!state.editor || !state.options) {
			return;
		}
		defineThemes(state.options.colors);
		monaco.editor.setTheme(state.options.theme || "nametbd-unreal");
		state.editor.updateOptions(editorOptions(state.options));
		state.model.updateOptions({ tabSize: state.options.tabSize, indentSize: state.options.tabSize, insertSpaces: true });
		document.body.style.background = state.options.colors && state.options.colors.background ? state.options.colors.background : "";
	}

	function clearError() {
		if (!state.model) {
			return;
		}
		monaco.editor.setModelMarkers(state.model, "widgetanim", []);
		state.decorations = state.editor.deltaDecorations(state.decorations, []);
	}

	function create() {
		Kind = monaco.languages.CompletionItemKind;
		registerLanguage();
		defineThemes(null);

		monaco.languages.registerCompletionItemProvider("nametbd-python", {
			triggerCharacters: [".", "\"", "'"],
			provideCompletionItems: provideCompletions
		});
		monaco.languages.registerHoverProvider("nametbd-python", { provideHover: provideHover });

		state.model = monaco.editor.createModel(state.pendingText !== null ? state.pendingText : "", "nametbd-python");
		state.model.setEOL(monaco.editor.EndOfLineSequence.LF);
		state.editor = monaco.editor.create(document.getElementById("editor"), {
			model: state.model,
			theme: "nametbd-unreal",
			automaticLayout: true,
			scrollBeyondLastLine: false,
			fixedOverflowWidgets: true,
			wordBasedSuggestions: "off",
			links: false,
			glyphMargin: true
		});
		state.pendingText = null;
		applyOptions();
		document.getElementById("status").style.display = "none";

		state.model.onDidChangeContent(function () {
			clearError();
			if (!state.suppressChange) {
				call("textchanged", state.model.getValue());
			}
		});
		state.editor.onDidChangeCursorPosition(function (event) {
			call("cursormoved", event.position.lineNumber, event.position.column);
		});

		// Ctrl+Enter runs, as the plain box does; Ctrl+S keeps the script, rather than reaching the browser.
		state.editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, function () {
			call("run", state.model.getValue());
		});
		state.editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, function () {
			call("save");
		});

		// Ready once Unreal's half of the link is there.
		var tries = 0;
		(function waitForBridge() {
			if (bridge()) {
				state.ready = true;
				call("ready");
				return;
			}
			if (++tries < 400) {
				setTimeout(waitForBridge, 25);
			}
		})();
	}

	// ---------------------------------------------------------------------------------------------
	// What Unreal calls
	// ---------------------------------------------------------------------------------------------

	window.widgetAnimEditor = {
		/** Replaces the text. bUndoable: as an edit Ctrl+Z takes back - a snippet picked - rather than a fresh start. */
		setText: function (text, undoable) {
			if (!state.model) {
				state.pendingText = text;
				return;
			}
			if (text === state.model.getValue()) {
				return;
			}
			state.suppressChange = true;
			if (undoable) {
				state.editor.pushUndoStop();
				state.editor.executeEdits("widgetanim", [{ range: state.model.getFullModelRange(), text: text }]);
				state.editor.pushUndoStop();
				state.editor.setPosition({ lineNumber: 1, column: 1 });
			} else {
				state.model.setValue(text);
			}
			state.suppressChange = false;
		},

		setOptions: function (options) {
			state.options = options;
			applyOptions();
		},

		/** Marks the line a run stopped on, with why, and puts the cursor there. */
		setError: function (line, message) {
			if (!state.model || line < 1) {
				return;
			}
			line = Math.min(line, state.model.getLineCount());
			monaco.editor.setModelMarkers(state.model, "widgetanim", [{
				severity: monaco.MarkerSeverity.Error,
				message: message,
				startLineNumber: line,
				startColumn: state.model.getLineFirstNonWhitespaceColumn(line) || 1,
				endLineNumber: line,
				endColumn: state.model.getLineMaxColumn(line)
			}]);
			state.decorations = state.editor.deltaDecorations(state.decorations, [{
				range: new monaco.Range(line, 1, line, 1),
				options: { isWholeLine: true, className: "widgetanim-error-line", glyphMarginClassName: "widgetanim-error-glyph" }
			}]);
			state.editor.revealLineInCenter(line);
			state.editor.setPosition({ lineNumber: line, column: state.model.getLineFirstNonWhitespaceColumn(line) || 1 });
			state.editor.focus();
		},

		clearError: clearError,

		setCompletions: function (completions) {
			state.completions = completions;
		},

		setWidgets: function (widgets) {
			state.widgets = widgets || [];
		},

		focus: function () {
			if (state.editor) {
				state.editor.focus();
			}
		}
	};

	// The error line's tint and its marker in the gutter.
	var style = document.createElement("style");
	style.textContent =
		".widgetanim-error-line { background: rgba(255, 90, 80, 0.14); }" +
		".widgetanim-error-glyph { background: #ff6b61; border-radius: 50%; width: 8px !important; height: 8px !important; margin: 6px 0 0 6px; }";
	document.head.appendChild(style);

	// Workers can't start from a page loaded from disk; Monaco then does their work on the page itself, which is fine at the
	// size of a script. Pointing it at the worker file keeps the attempt quick.
	window.MonacoEnvironment = {
		getWorkerUrl: function () {
			return "monaco/vs/base/worker/workerMain.js";
		}
	};

	require.config({ paths: { vs: "monaco/vs" } });
	require(["vs/editor/editor.main"], create, function (error) {
		document.getElementById("status").textContent = "The code editor couldn't load: " + error;
		console.error("widgetAnimEditor: Monaco failed to load: " + error);
	});
})();
