// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

#include "WidgetAnimScripting.h"

class SBox;
class SMultiLineEditableTextBox;
class SWidgetAnimMonacoEditor;
struct FPropertyChangedEvent;
class SWidgetAnimTimeline;

/**
 * Colors Python as it is typed: keywords, strings (single, double, triple-quoted - across lines - and prefixed ones such as
 * f-strings), comments, numbers, builtins, the scripting API's own names (anim, TrackType, Ease...) and what def and class
 * name. A lexer of its own rather than Slate's rule tokenizer, because a triple-quoted string runs over several lines and
 * the tokenizer sees one line at a time.
 */
class FWidgetAnimPythonMarshaller : public FPlainTextLayoutMarshaller
{
public:

	struct FStyles
	{
		FTextBlockStyle Normal;
		FTextBlockStyle Keyword;
		FTextBlockStyle Constant;
		FTextBlockStyle String;
		FTextBlockStyle Comment;
		FTextBlockStyle Number;
		FTextBlockStyle Builtin;
		FTextBlockStyle Api;
		FTextBlockStyle Definition;
		FTextBlockStyle Operator;
	};

	explicit FWidgetAnimPythonMarshaller(const FStyles& InStyles);

	// ITextLayoutMarshaller
	virtual void SetText(const FString& SourceString, FTextLayout& TargetTextLayout) override;
	virtual bool RequiresLiveUpdate() const override { return true; }

private:

	FStyles Styles;
};

/** The Script area's output: each line in the color of what it is - printed, a warning, an error, the run's outcome. */
class FWidgetAnimScriptOutputMarshaller : public FPlainTextLayoutMarshaller
{
public:

	enum class EKind : uint8 { Info, Warning, Error, Done, Failed };

	explicit FWidgetAnimScriptOutputMarshaller(const FTextBlockStyle& InBaseStyle);

	/** The kind of each line of the text about to be set, in order. */
	TArray<EKind> LineKinds;

	/** The color of each kind, in EKind's order - the theme's. The text is laid out again for them to show. */
	void SetKindColors(const FLinearColor (&Colors)[5]);

	// ITextLayoutMarshaller
	virtual void SetText(const FString& SourceString, FTextLayout& TargetTextLayout) override;

private:

	FTextBlockStyle Styles[5];
};

/**
 * The Widget Animations panel's Script area: Python that makes and changes tracks, run on the animation in the panel.
 *
 * The code is written in Monaco, the editor in VS Code (SWidgetAnimMonacoEditor) - suggestions for the scripting API, find and
 * replace, multi-cursor, folding - set up in Editor Preferences > Plugins > Widget Animations Editor > Script Editor. Where it
 * can't run, or with Use Code Editor off, a plain code box takes its place: monospace, colored as Python, Tab four spaces,
 * Enter keeping the indent and adding one after a colon. Either way Ctrl+Enter runs, and the output of the last run is beside it. Run is one undoable edit (one Ctrl+Z takes all of it back), and a
 * run that stops with an error changes nothing; the error's line is where the cursor goes. Snippets fills the box with a
 * ready-made script from the plugin's Content/Python/WidgetAnimSnippets - or the project's own of that name - and API
 * Reference prints what a script can use.
 *
 * Python is optional: with the Python Editor Script Plugin off, the area says so and nothing else in the panel changes.
 *
 * The script is kept between panels and sessions in Saved/NameTBD/WidgetAnimScript.py. See WidgetAnimScripting.h.
 */
class SWidgetAnimScriptPanel : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimScriptPanel)
		: _InWindow(false)
		, _Docked(false)
		{}

		/** The panel the script runs on - its animation and what is selected in it. */
		SLATE_ARGUMENT(TWeakPtr<SWidgetAnimTimeline>, Panel)

		/** What the close button does: hides the area, or closes its window. */
		SLATE_EVENT(FSimpleDelegate, OnClose)

		/** In a window of its own rather than under the Track Editor: no Open in Window button, and the close button closes it. */
		SLATE_ARGUMENT(bool, InWindow)

		/** What Open in Window does. The button shows only with this bound. */
		SLATE_EVENT(FSimpleDelegate, OnPopOut)

		/**
		 * Docked as a tab of the editor's layout (see SWidgetAnimTimeline::DockScript): the output under the code rather than
		 * beside it, no Open in Window, and the close button closes the tab.
		 */
		SLATE_ARGUMENT(bool, Docked)

		/** What Dock as Tab does. The button shows only with this bound. */
		SLATE_EVENT(FSimpleDelegate, OnDock)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SWidgetAnimScriptPanel() override;

	/** Runs the script on the panel's animation. */
	void Run();

private:

	bool CanRun() const;
	bool IsPythonAvailableCached() const;

	FReply OnCodeKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	void OnCodeChanged(const FText& NewText);

	TSharedRef<SWidget> MakeSnippetMenu();
	void OpenSnippet(FString FilePath);
	void ShowApiReference();
	void ClearOutput();

	/** Shows Lines in the output - with Status, in its color, as the last line when given. */
	void ShowOutput(const TArray<WidgetAnimScripting::FOutputLine>& Lines, const FString& Status, bool bFailed);

	void SaveScript() const;

	FText GetCursorText() const;

	// The code: in the code editor, or the plain box where that can't run. CodeText is the script either way.

	/** Puts the code editor or the plain box in the code's place, as the editor preferences say and the editor allows. */
	void BuildCodeEditor();
	TSharedRef<SWidget> MakePlainCodeBox();

	/** Replaces the script. bUndoable: as one edit the code editor's Ctrl+Z takes back. */
	void SetCode(const FString& NewCode, bool bUndoable);

	void OnEditorSettingsChanged(UObject* Settings, FPropertyChangedEvent& Event);

	/** The code editor couldn't start: the plain box takes over, on the next frame - the editor may be mid-call. */
	void OnCodeEditorFailed();

	/** Hands the code editor what to suggest - worked out once per session, by the Python module itself. */
	void SendCompletions(bool bRefresh = false);

	/** Keeps the code editor's widget names - suggested inside target="..." - in step with the Widget Blueprint. */
	EActiveTimerReturnType SyncCodeEditor(double InCurrentTime, float InDeltaTime);

	/**
	 * The script is one for the session, shared by every Script area - the drawer's, the docked tab's, one in its own window.
	 * PublishScript makes this area's the session's; SyncFromSession takes up one another area published since.
	 */
	void PublishScript(const FString& Script);
	EActiveTimerReturnType SyncFromSession(double InCurrentTime, float InDeltaTime);

	// Your snippets and functions

	/** Saves the script as a snippet of this project's, under Name - asking first before replacing one. */
	void SaveAsSnippet(const FString& Name);
	void DeleteSnippet(FString FilePath);

	/**
	 * The code box shows the script, or - the Functions tab - your functions file (WidgetAnimScripting::GetFunctionsFile),
	 * which every run loads before the script. Run always runs the script.
	 */
	void SetEditingFunctions(bool bFunctions);

	/** Writes your functions file, if it was edited since it was last written. */
	void SaveFunctions();

	/** The text in the code box changed - in whichever document it shows. */
	void OnDocumentTextChanged(const FString& NewText);

	/** A Script / Functions tab of the strip. */
	TSharedRef<SWidget> MakeDocumentTab(bool bFunctions);

	/** The output's colors: the code editor's theme while it is the one in use, the panel's own otherwise. */
	struct FOutputColors
	{
		FLinearColor Background;
		FLinearColor Kinds[5];
	};
	FOutputColors GetOutputColors() const;

	/** The output in the colors of the theme now in force. */
	void RefreshOutputStyles();

	TWeakPtr<SWidgetAnimTimeline> Panel;
	FSimpleDelegate OnClose;

	TSharedPtr<SMultiLineEditableTextBox> CodeBox;
	TSharedPtr<SMultiLineEditableTextBox> OutputBox;
	TSharedPtr<FWidgetAnimScriptOutputMarshaller> OutputMarshaller;

	/** What the code box held before a snippet replaced it, for Restore Previous Script. */
	FString PreviousScript;

	/** Where the code goes - the code editor or the plain box - and the code editor, while it is the one there. */
	TSharedPtr<SBox> CodeHost;
	TSharedPtr<SWidgetAnimMonacoEditor> CodeEditor;

	/** The script, as it is in whichever is there. */
	FString CodeText;

	/** The code editor failed to start in this panel: the plain box from now on. */
	bool bCodeEditorFailed = false;

	FDelegateHandle SettingsChangedHandle;

	/** In its own window - see FArguments::InWindow - and what Open in Window does. */
	bool bInWindow = false;
	FSimpleDelegate OnPopOut;

	/** Docked as a tab - see FArguments::Docked - and what Dock as Tab does. */
	bool bDocked = false;
	FSimpleDelegate OnDock;

	/** The session script's revision this area last published or took up - see PublishScript. */
	uint32 SeenScriptRevision = 0;

	/** The Functions tab: shown now, its file, its text, whether that is unsaved, and whether the box is being switched over. */
	bool bEditingFunctions = false;
	FString FunctionsFile;
	FString FunctionsText;
	bool bFunctionsDirty = false;
	bool bSwitchingDocument = false;

	/** What reads the Widget Blueprint's widgets for the code editor, and what it read last. */
	TStrongObjectPtr<UWidgetAnimScriptAnimation> WidgetReader;
	uint32 WidgetNamesHash = 0;
	bool bCompletionsSent = false;

	/** Whether Python was available when last asked, and when that was - asking goes through reflection, so not every frame. */
	mutable bool bPythonAvailable = false;
	mutable double LastPythonCheck = -1.0;
};
