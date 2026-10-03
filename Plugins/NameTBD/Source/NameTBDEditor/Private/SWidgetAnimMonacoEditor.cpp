// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimMonacoEditor.h"

#include "WidgetAnimEditorSettings.h"
#include "NameTBDColors.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "WebBrowserModule.h"   // IWebBrowserModule
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"
#include "IWebBrowserWindow.h"   // EWebBrowserConsoleLogSeverity (SWebBrowser.h only declares it)
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimScriptEditor, Log, All);

namespace WidgetAnimScriptEditorImpl
{
	/** How long the page has to say it is ready before the plain box takes over. */
	constexpr double PageTimeoutSeconds = 12.0;

	FString GetPagePath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
		return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources/ScriptEditor/index.html")) : FString();
	}

	/** A color as the page's CSS reads it: #rrggbb, in sRGB. */
	FString ToCss(const FLinearColor& Color)
	{
		const FColor Srgb = Color.ToFColor(/*bSRGB=*/true);
		return FString::Printf(TEXT("\"#%02x%02x%02x\""), Srgb.R, Srgb.G, Srgb.B);
	}
}

// =====================================================================================================
// UWidgetAnimScriptEditorBridge
// =====================================================================================================

void UWidgetAnimScriptEditorBridge::Ready()
{
	if (OnReady)
	{
		OnReady();
	}
}

void UWidgetAnimScriptEditorBridge::TextChanged(const FString& Text)
{
	if (OnTextChanged)
	{
		OnTextChanged(Text);
	}
}

void UWidgetAnimScriptEditorBridge::Run(const FString& Text)
{
	if (OnRun)
	{
		OnRun(Text);
	}
}

void UWidgetAnimScriptEditorBridge::CursorMoved(int32 Line, int32 Column)
{
	if (OnCursorMoved)
	{
		OnCursorMoved(Line, Column);
	}
}

void UWidgetAnimScriptEditorBridge::Save()
{
	if (OnSave)
	{
		OnSave();
	}
}

// =====================================================================================================
// SWidgetAnimMonacoEditor
// =====================================================================================================

FString WidgetAnimScriptEditor::ToJsString(const FString& Text)
{
	FString Out;
	Out.Reserve(Text.Len() + 16);
	Out.AppendChar(TEXT('"'));
	for (const TCHAR Char : Text)
	{
		switch (Char)
		{
		case TEXT('"'):  Out += TEXT("\\\""); break;
		case TEXT('\\'): Out += TEXT("\\\\"); break;
		case TEXT('\n'): Out += TEXT("\\n"); break;
		case TEXT('\r'): break; // the page's text is \n only
		case TEXT('\t'): Out += TEXT("\\t"); break;
		case TEXT('<'):  Out += TEXT("\\u003c"); break;
		default:
			// Control characters, and the two line separators JavaScript reads as line ends inside a string.
			if (Char < 0x20 || Char == 0x2028 || Char == 0x2029)
			{
				Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Char));
			}
			else
			{
				Out.AppendChar(Char);
			}
		}
	}
	Out.AppendChar(TEXT('"'));
	return Out;
}

bool SWidgetAnimMonacoEditor::IsAvailable()
{
	if (!IWebBrowserModule::IsAvailable() || !IWebBrowserModule::Get().IsWebModuleAvailable())
	{
		return false;
	}
	const FString Page = WidgetAnimScriptEditorImpl::GetPagePath();
	return !Page.IsEmpty() && IFileManager::Get().FileExists(*Page);
}

FString SWidgetAnimMonacoEditor::GetPageUrl()
{
	FString Path = WidgetAnimScriptEditorImpl::GetPagePath();
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Only what can't stand in a URL as it is: a project under "Unreal Projects" has a space in its path.
	Path.ReplaceInline(TEXT("%"), TEXT("%25"));
	Path.ReplaceInline(TEXT(" "), TEXT("%20"));
	Path.ReplaceInline(TEXT("#"), TEXT("%23"));
	return Path.StartsWith(TEXT("/")) ? TEXT("file://") + Path : TEXT("file:///") + Path;
}

void SWidgetAnimMonacoEditor::Construct(const FArguments& InArgs)
{
	Text = InArgs._InitialText.Replace(TEXT("\r\n"), TEXT("\n"));
	OnTextChangedDelegate = InArgs._OnTextChanged;
	OnRunDelegate = InArgs._OnRun;
	OnSaveDelegate = InArgs._OnSave;
	OnFailedDelegate = InArgs._OnFailed;

	// The page's calls, back on this editor - while it lives: the bridge can outlast it for a moment in the browser.
	Bridge.Reset(NewObject<UWidgetAnimScriptEditorBridge>(GetTransientPackage()));
	const TWeakPtr<SWidgetAnimMonacoEditor> WeakThisPtr = SharedThis(this);
	Bridge->OnReady = [WeakThisPtr]()
	{
		if (const TSharedPtr<SWidgetAnimMonacoEditor> Pinned = WeakThisPtr.Pin())
		{
			Pinned->OnPageReady();
		}
	};
	Bridge->OnTextChanged = [WeakThisPtr](const FString& NewText)
	{
		if (const TSharedPtr<SWidgetAnimMonacoEditor> Pinned = WeakThisPtr.Pin())
		{
			Pinned->Text = NewText;
			Pinned->OnTextChangedDelegate.ExecuteIfBound(NewText);
		}
	};
	Bridge->OnRun = [WeakThisPtr](const FString& NewText)
	{
		if (const TSharedPtr<SWidgetAnimMonacoEditor> Pinned = WeakThisPtr.Pin())
		{
			Pinned->Text = NewText;
			Pinned->OnRunDelegate.ExecuteIfBound(NewText);
		}
	};
	Bridge->OnCursorMoved = [WeakThisPtr](int32 Line, int32 Column)
	{
		if (const TSharedPtr<SWidgetAnimMonacoEditor> Pinned = WeakThisPtr.Pin())
		{
			Pinned->CursorLine = Line;
			Pinned->CursorColumn = Column;
		}
	};
	Bridge->OnSave = [WeakThisPtr]()
	{
		if (const TSharedPtr<SWidgetAnimMonacoEditor> Pinned = WeakThisPtr.Pin())
		{
			Pinned->OnSaveDelegate.ExecuteIfBound();
		}
	};

	const FColor Background = NameTBD::Colors::WidgetAnimEditor::ScriptCodeBackground.ToFColor(/*bSRGB=*/true);

	ChildSlot
	[
		SAssignNew(Browser, SWebBrowser)
		.InitialURL(GetPageUrl())
		.ShowControls(false)
		.ShowAddressBar(false)
		.ShowErrorMessage(true)
		.SupportsTransparency(false)
		.BackgroundColor(Background)
		.BrowserFrameRate(60)
		.OnLoadError_Lambda([this]() { OnPageFailed(TEXT("the page couldn't load")); })
		// The page's console in the Output Log: script errors, and the input probe in script_editor.js.
		.OnConsoleMessage_Lambda([](const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity)
		{
			if (Severity == EWebBrowserConsoleLogSeverity::Error || Severity == EWebBrowserConsoleLogSeverity::Fatal)
			{
				UE_LOG(LogWidgetAnimScriptEditor, Warning, TEXT("Code editor page: %s (%s:%d)"), *Message, *FPaths::GetCleanFilename(Source), Line);
			}
			else
			{
				UE_LOG(LogWidgetAnimScriptEditor, Log, TEXT("Code editor page: %s"), *Message);
			}
		})
	];

	// Bound for good, so it is there on the page however soon the page asks for it.
	Browser->BindUObject(TEXT("scripteditor"), Bridge.Get(), /*bIsPermanent=*/true);

	SettingsChangedHandle = UWidgetAnimEditorSettings::GetMutable().OnSettingChanged().AddSP(this, &SWidgetAnimMonacoEditor::OnSettingsChanged);

	StartTime = FPlatformTime::Seconds();
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetAnimMonacoEditor::WaitForPage));
}

SWidgetAnimMonacoEditor::~SWidgetAnimMonacoEditor()
{
	UWidgetAnimEditorSettings::GetMutable().OnSettingChanged().Remove(SettingsChangedHandle);

	if (Bridge.IsValid())
	{
		Bridge->OnReady = nullptr;
		Bridge->OnTextChanged = nullptr;
		Bridge->OnRun = nullptr;
		Bridge->OnCursorMoved = nullptr;
		Bridge->OnSave = nullptr;
		if (Browser.IsValid())
		{
			Browser->UnbindUObject(TEXT("scripteditor"), Bridge.Get(), /*bIsPermanent=*/true);
		}
	}
}

FReply SWidgetAnimMonacoEditor::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Whether Slate brings clicks here at all, and in what state - set against the page's own log of what it receives, it
	// says on which side input stops. Only the first few, and the click goes on to the browser untouched.
	if (LoggedClicks < 3)
	{
		++LoggedClicks;
		UE_LOG(LogWidgetAnimScriptEditor, Log, TEXT("Code editor: a click reached it (enabled %d, page ready %d, focus inside %d)."),
			IsEnabled() ? 1 : 0, bReady ? 1 : 0, HasAnyUserFocusOrFocusedDescendants() ? 1 : 0);
	}
	return FReply::Unhandled();
}

EActiveTimerReturnType SWidgetAnimMonacoEditor::WaitForPage(double InCurrentTime, float InDeltaTime)
{
	if (bReady || bFailed)
	{
		return EActiveTimerReturnType::Stop;
	}
	if (FPlatformTime::Seconds() - StartTime > WidgetAnimScriptEditorImpl::PageTimeoutSeconds)
	{
		OnPageFailed(TEXT("the page didn't answer"));
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SWidgetAnimMonacoEditor::OnPageReady()
{
	if (bFailed)
	{
		return;
	}
	bReady = true;

	// Everything the page needs, as it is now.
	Execute(FString::Printf(TEXT("widgetAnimEditor.setOptions(%s);"), *MakeOptionsJson()));
	Execute(FString::Printf(TEXT("widgetAnimEditor.setText(%s, false);"), *WidgetAnimScriptEditor::ToJsString(Text)));
	if (!CompletionsJson.IsEmpty())
	{
		Execute(FString::Printf(TEXT("widgetAnimEditor.setCompletions(%s);"), *CompletionsJson));
	}
	if (!WidgetsJson.IsEmpty())
	{
		Execute(FString::Printf(TEXT("widgetAnimEditor.setWidgets(%s);"), *WidgetsJson));
	}
}

void SWidgetAnimMonacoEditor::OnPageFailed(const FString& Why)
{
	if (bFailed || bReady)
	{
		return;
	}
	bFailed = true;
	UE_LOG(LogWidgetAnimScriptEditor, Warning, TEXT("The Script area's code editor couldn't start (%s): using the plain code box. Page: %s"), *Why, *GetPageUrl());
	OnFailedDelegate.ExecuteIfBound();
}

void SWidgetAnimMonacoEditor::OnSettingsChanged(UObject* Settings, FPropertyChangedEvent& Event)
{
	Execute(FString::Printf(TEXT("widgetAnimEditor.setOptions(%s);"), *MakeOptionsJson()));
}

void SWidgetAnimMonacoEditor::Execute(const FString& Script)
{
	if (bReady && Browser.IsValid())
	{
		Browser->ExecuteJavascript(Script);
	}
}

void SWidgetAnimMonacoEditor::SetText(const FString& NewText, bool bUndoable)
{
	Text = NewText.Replace(TEXT("\r\n"), TEXT("\n"));
	Execute(FString::Printf(TEXT("widgetAnimEditor.setText(%s, %s);"), *WidgetAnimScriptEditor::ToJsString(Text), bUndoable ? TEXT("true") : TEXT("false")));
}

void SWidgetAnimMonacoEditor::ShowError(int32 Line, const FString& Message)
{
	Execute(FString::Printf(TEXT("widgetAnimEditor.setError(%d, %s);"), Line, *WidgetAnimScriptEditor::ToJsString(Message)));
}

void SWidgetAnimMonacoEditor::ClearError()
{
	Execute(TEXT("widgetAnimEditor.clearError();"));
}

void SWidgetAnimMonacoEditor::SetCompletionsJson(const FString& Json)
{
	CompletionsJson = Json;
	if (!Json.IsEmpty())
	{
		Execute(FString::Printf(TEXT("widgetAnimEditor.setCompletions(%s);"), *Json));
	}
}

void SWidgetAnimMonacoEditor::SetWidgetNames(const TArray<FWidgetName>& Widgets)
{
	using WidgetAnimScriptEditor::ToJsString;

	TArray<FString> Entries;
	for (const FWidgetName& Widget : Widgets)
	{
		Entries.Add(FString::Printf(TEXT("{\"name\":%s,\"path\":%s,\"cls\":%s}"), *ToJsString(Widget.Name), *ToJsString(Widget.Path), *ToJsString(Widget.Class)));
	}
	WidgetsJson = TEXT("[") + FString::Join(Entries, TEXT(",")) + TEXT("]");
	Execute(FString::Printf(TEXT("widgetAnimEditor.setWidgets(%s);"), *WidgetsJson));
}

void SWidgetAnimMonacoEditor::FocusEditor()
{
	Execute(TEXT("widgetAnimEditor.focus();"));
}

FString SWidgetAnimMonacoEditor::MakeOptionsJson() const
{
	using namespace WidgetAnimScriptEditorImpl;
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	const UWidgetAnimEditorSettings& Settings = UWidgetAnimEditorSettings::Get();

	const TCHAR* Theme = TEXT("nametbd-unreal");
	switch (Settings.ScriptEditorTheme)
	{
	case EWidgetAnimScriptEditorTheme::Dark:         Theme = TEXT("nametbd-dark"); break;
	case EWidgetAnimScriptEditorTheme::Light:        Theme = TEXT("nametbd-light"); break;
	case EWidgetAnimScriptEditorTheme::HighContrast: Theme = TEXT("nametbd-hc"); break;
	default: break;
	}

	// The Unreal theme's colors, from NameTBDColors.h - the same as the plain box's.
	const FString Colors = FString::Printf(
		TEXT("{\"background\":%s,\"foreground\":%s,\"keyword\":%s,\"constant\":%s,\"string\":%s,\"comment\":%s,\"number\":%s,")
		TEXT("\"builtin\":%s,\"api\":%s,\"definition\":%s,\"operator\":%s,\"lineHighlight\":%s,\"lineNumber\":%s,")
		TEXT("\"lineNumberActive\":%s,\"selection\":%s,\"cursor\":%s,\"widget\":%s}"),
		*ToCss(Palette::ScriptCodeBackground), *ToCss(Palette::ScriptPlain), *ToCss(Palette::ScriptKeyword), *ToCss(Palette::ScriptConstant),
		*ToCss(Palette::ScriptString), *ToCss(Palette::ScriptComment), *ToCss(Palette::ScriptNumber), *ToCss(Palette::ScriptBuiltin),
		*ToCss(Palette::ScriptApi), *ToCss(Palette::ScriptDefinition), *ToCss(Palette::ScriptOperator), *ToCss(Palette::ScriptLineHighlight),
		*ToCss(Palette::ScriptLineNumber), *ToCss(Palette::ScriptLineNumberActive), *ToCss(Palette::ScriptSelection), *ToCss(Palette::ScriptCursor),
		*ToCss(Palette::ScriptOutputBackground));

	auto Bool = [](bool bValue) { return bValue ? TEXT("true") : TEXT("false"); };
	return FString::Printf(
		TEXT("{\"theme\":\"%s\",\"fontSize\":%d,\"fontFamily\":%s,\"tabSize\":%d,\"lineNumbers\":%s,\"minimap\":%s,")
		TEXT("\"wordWrap\":%s,\"autoClose\":%s,\"suggestions\":%s,\"colors\":%s}"),
		Theme,
		FMath::Clamp(Settings.ScriptEditorFontSize, 8, 32),
		*WidgetAnimScriptEditor::ToJsString(Settings.ScriptEditorFontFamily.IsEmpty() ? FString(TEXT("monospace")) : Settings.ScriptEditorFontFamily),
		FMath::Clamp(Settings.ScriptEditorTabSize, 1, 8),
		Bool(Settings.bScriptEditorLineNumbers),
		Bool(Settings.bScriptEditorMinimap),
		Bool(Settings.bScriptEditorWordWrap),
		Bool(Settings.bScriptEditorAutoClose),
		Bool(Settings.bScriptEditorSuggestions),
		*Colors);
}
