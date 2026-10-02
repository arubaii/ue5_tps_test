// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

#include "SWidgetAnimMonacoEditor.generated.h"

class SWebBrowser;
struct FPropertyChangedEvent;

/**
 * What the code editor's page calls back into: bound into the page as window.ue.scripteditor, its UFUNCTIONs called from
 * JavaScript in lower case (ready, textchanged, run, cursormoved, save). Each passes the call on to the editor that owns it.
 * See Resources/ScriptEditor/script_editor.js.
 */
UCLASS(Transient)
class UWidgetAnimScriptEditorBridge : public UObject
{
	GENERATED_BODY()

public:

	TFunction<void()> OnReady;
	TFunction<void(const FString&)> OnTextChanged;
	TFunction<void(const FString&)> OnRun;
	TFunction<void(int32, int32)> OnCursorMoved;
	TFunction<void()> OnSave;

	/** The page is up and listening. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Script Editor")
	void Ready();

	/** The whole text, after any change to it. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Script Editor")
	void TextChanged(const FString& Text);

	/** Ctrl+Enter, with the text as it is now. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Script Editor")
	void Run(const FString& Text);

	/** Where the cursor is, from 1. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Script Editor")
	void CursorMoved(int32 Line, int32 Column);

	/** Ctrl+S. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Script Editor")
	void Save();
};

/**
 * The Script area's code editor: Monaco - the editor in VS Code - in the editor's web browser, loaded from the plugin's
 * Resources/ScriptEditor. Multi-cursor, find and replace, bracket matching, folding, and suggestions and hover help for the
 * scripting API; set up from Editor Preferences > Plugins > Widget Animations Editor > Script Editor, which it follows live.
 *
 * Text, options, suggestions and widget names are kept here and handed to the page once it says it is ready, and again
 * whenever they change. If the page doesn't come up - no web browser in this editor, files missing, it never answers - it
 * says so through OnFailed, and the Script area falls back to its plain code box.
 */
class SWidgetAnimMonacoEditor : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_OneParam(FOnText, const FString&);

	/** One widget the page can suggest inside target="...": its name, its path, and its class. */
	struct FWidgetName
	{
		FString Name;
		FString Path;
		FString Class;
	};

	SLATE_BEGIN_ARGS(SWidgetAnimMonacoEditor) {}

		SLATE_ARGUMENT(FString, InitialText)

		/** The text changed - typed, pasted, undone. */
		SLATE_EVENT(FOnText, OnTextChanged)

		/** Ctrl+Enter in the editor, with its text. */
		SLATE_EVENT(FOnText, OnRun)

		SLATE_EVENT(FSimpleDelegate, OnSave)

		/** The editor couldn't start: the caller shows something else. */
		SLATE_EVENT(FSimpleDelegate, OnFailed)

	SLATE_END_ARGS()

	/** Whether the code editor can run here: the web browser module working, and the page's files in place. */
	static bool IsAvailable();

	void Construct(const FArguments& InArgs);
	virtual ~SWidgetAnimMonacoEditor() override;

	FString GetText() const { return Text; }

	/** Replaces the text. bUndoable: as one edit Ctrl+Z takes back (a snippet picked), rather than a fresh start. */
	void SetText(const FString& NewText, bool bUndoable);

	/** Marks the line a run stopped on, with why, and puts the cursor there. */
	void ShowError(int32 Line, const FString& Message);
	void ClearError();

	/** What the page suggests: widget_anim_scripting.editor_completions() as JSON. */
	void SetCompletionsJson(const FString& Json);

	/** The widgets the page suggests inside target="...". */
	void SetWidgetNames(const TArray<FWidgetName>& Widgets);

	void FocusEditor();

	bool IsReady() const { return bReady; }
	int32 GetCursorLine() const { return CursorLine; }
	int32 GetCursorColumn() const { return CursorColumn; }

private:

	/** The page's index.html, as a file:// URL. */
	static FString GetPageUrl();

	void OnPageReady();
	void OnPageFailed(const FString& Why);
	void OnSettingsChanged(UObject* Settings, FPropertyChangedEvent& Event);

	/** The editor preferences as the page's options. */
	FString MakeOptionsJson() const;

	/** Runs Script on the page, if it is ready. */
	void Execute(const FString& Script);

	/** Waits for the page: gives up, and fails over, if it never says it is ready. */
	EActiveTimerReturnType WaitForPage(double InCurrentTime, float InDeltaTime);

	TSharedPtr<SWebBrowser> Browser;
	TStrongObjectPtr<UWidgetAnimScriptEditorBridge> Bridge;

	FOnText OnTextChangedDelegate;
	FOnText OnRunDelegate;
	FSimpleDelegate OnSaveDelegate;
	FSimpleDelegate OnFailedDelegate;

	FString Text;
	FString CompletionsJson;
	FString WidgetsJson;
	int32 CursorLine = 1;
	int32 CursorColumn = 1;

	bool bReady = false;
	bool bFailed = false;
	double StartTime = 0.0;
	FDelegateHandle SettingsChangedHandle;
};

namespace WidgetAnimScriptEditor
{
	/** Text as a JavaScript string literal, quotes included. */
	FString ToJsString(const FString& Text);
}
