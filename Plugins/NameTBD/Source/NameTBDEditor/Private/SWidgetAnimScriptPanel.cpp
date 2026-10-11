// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimScriptPanel.h"

#include "SWidgetAnimAnchoredTip.h"
#include "SWidgetAnimMonacoEditor.h"
#include "WidgetAnimEditorSettings.h"
#include "SWidgetAnimTimeline.h"
#include "WidgetAnimPanelSource.h"
#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/TextLayout.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimScriptPanel"

// Helpers for this file, in a namespace of their own rather than an anonymous one: in a unity build this file shares a
// translation unit with others that have their own Palette, WidgetAnimTips and PickBrush.
namespace WidgetAnimScriptPanelImpl
{
	namespace ScriptPalette = NameTBD::Colors::WidgetAnimEditor;
	namespace ScriptTips = NameTBD::Tooltips::WidgetAnimEditor;

	/**
	 * Where a widget's script is kept between sessions: Saved/NameTBD/Scripts/<its package path, as a file name>.py. With no
	 * key (no Widget Blueprint to go by), the one shared script of earlier versions, Saved/NameTBD/WidgetAnimScript.py.
	 */
	FString GetScriptFilePath(const FString& Key)
	{
		if (Key.IsEmpty())
		{
			return FPaths::ProjectSavedDir() / TEXT("NameTBD/WidgetAnimScript.py");
		}
		FString FileName = Key;
		FileName.RemoveFromStart(TEXT("/"));
		FileName.ReplaceInline(TEXT("/"), TEXT("_"));
		return FPaths::ProjectSavedDir() / TEXT("NameTBD/Scripts") / (FPaths::MakeValidFileName(FileName, TEXT('_')) + TEXT(".py"));
	}

	/**
	 * One widget's script for this session - shared by every Script area of that widget (its drawer, its docked tab, the area
	 * under its Track Editor), and by no other widget's - with a count of its changes, so each area can tell when another
	 * changed it.
	 */
	struct FSessionScript
	{
		TOptional<FString> Script;
		uint32 Revision = 1;

		/** Changed since it was last written to disk, and when. */
		bool bUnsaved = false;
		double ChangedAt = 0.0;
	};

	TMap<FString, FSessionScript>& GetSessions()
	{
		static TMap<FString, FSessionScript> Sessions;
		return Sessions;
	}

	FSessionScript& GetSession(const FString& Key)
	{
		return GetSessions().FindOrAdd(Key);
	}

	FString GetScriptFilePath(const FString& Key);

	void WriteSession(const FString& Key, FSessionScript& Session)
	{
		if (!Session.Script.IsSet())
		{
			return;
		}
		const FString Path = GetScriptFilePath(Key);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		FFileHelper::SaveStringToFile(Session.Script.GetValue(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Session.bUnsaved = false;
	}

	/** Writes every script changed since it was last written - all of them, or only those left alone for a moment. */
	void FlushSessions(bool bAll)
	{
		const double Now = FPlatformTime::Seconds();
		for (TPair<FString, FSessionScript>& Pair : GetSessions())
		{
			if (Pair.Value.bUnsaved && (bAll || Now - Pair.Value.ChangedAt > 0.75))
			{
				WriteSession(Pair.Key, Pair.Value);
			}
		}
	}

	/**
	 * A script changed: written to disk shortly after typing stops, and before the editor exits. Saving can't wait for its Script
	 * area to close - the drawer's and the docked tab's live as long as their editor, and a closed editor's past the point an exit
	 * would still write them.
	 */
	void NoteSessionChanged(const FString& Key)
	{
		FSessionScript& Session = GetSession(Key);
		Session.bUnsaved = true;
		Session.ChangedAt = FPlatformTime::Seconds();

		static FTSTicker::FDelegateHandle TickerHandle;
		if (!TickerHandle.IsValid())
		{
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
			{
				FlushSessions(/*bAll=*/false);
				return true;
			}), 0.5f);
			FCoreDelegates::OnEnginePreExit.AddLambda([]()
			{
				FlushSessions(/*bAll=*/true);
				FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			});
		}
	}

	TOptional<FString>& GetSessionScript(const FString& Key)
	{
		return GetSession(Key).Script;
	}

	uint32& GetSessionRevision(const FString& Key)
	{
		return GetSession(Key).Revision;
	}

	FString LoadSessionScript(const FString& Key)
	{
		TOptional<FString>& Script = GetSessionScript(Key);
		if (!Script.IsSet())
		{
			FString Loaded;
			if (!FFileHelper::LoadFileToString(Loaded, *GetScriptFilePath(Key)))
			{
				Loaded = ScriptTips::ScriptStarter().ToString();
			}
			Script = Loaded;
		}
		return Script.GetValue();
	}

	/** A style of the code box's monospace font in Color. Comments too: the editor's fonts have no monospace italic. */
	FTextBlockStyle MakeCodeStyle(const FLinearColor& Color)
	{
		FTextBlockStyle Style = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
		Style.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 9));
		Style.SetColorAndOpacity(FSlateColor(Color));
		return Style;
	}

	/** The first of several brushes this engine version has, or none. */
	const FSlateBrush* PickBrush(std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* Name : Names)
		{
			if (const FSlateBrush* Brush = FAppStyle::Get().GetOptionalBrush(Name, nullptr, nullptr))
			{
				return Brush;
			}
		}
		return nullptr;
	}

	TSharedRef<SWidget> MakeIconLabel(const FSlateBrush* Icon, const FText& Label)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		if (Icon)
		{
			Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(14.f)
				.HeightOverride(14.f)
				[
					SNew(SImage)
					.Image(Icon)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}
		Box->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Label)
		];
		return Box;
	}

	// ---------------------------------------------------------------------------------------------
	// Python's words
	// ---------------------------------------------------------------------------------------------

	const TSet<FString>& GetKeywords()
	{
		static const TSet<FString> Words = {
			TEXT("and"), TEXT("as"), TEXT("assert"), TEXT("async"), TEXT("await"), TEXT("break"), TEXT("class"), TEXT("continue"),
			TEXT("def"), TEXT("del"), TEXT("elif"), TEXT("else"), TEXT("except"), TEXT("finally"), TEXT("for"), TEXT("from"),
			TEXT("global"), TEXT("if"), TEXT("import"), TEXT("in"), TEXT("is"), TEXT("lambda"), TEXT("nonlocal"), TEXT("not"),
			TEXT("or"), TEXT("pass"), TEXT("raise"), TEXT("return"), TEXT("try"), TEXT("while"), TEXT("with"), TEXT("yield"),
			TEXT("match"), TEXT("case") };
		return Words;
	}

	const TSet<FString>& GetConstants()
	{
		static const TSet<FString> Words = { TEXT("True"), TEXT("False"), TEXT("None") };
		return Words;
	}

	const TSet<FString>& GetBuiltins()
	{
		static const TSet<FString> Words = {
			TEXT("print"), TEXT("len"), TEXT("range"), TEXT("enumerate"), TEXT("zip"), TEXT("list"), TEXT("dict"), TEXT("set"),
			TEXT("tuple"), TEXT("int"), TEXT("float"), TEXT("str"), TEXT("bool"), TEXT("min"), TEXT("max"), TEXT("abs"),
			TEXT("round"), TEXT("sorted"), TEXT("reversed"), TEXT("sum"), TEXT("any"), TEXT("all"), TEXT("isinstance"),
			TEXT("getattr"), TEXT("setattr"), TEXT("hasattr"), TEXT("type"), TEXT("super"), TEXT("map"), TEXT("filter"),
			TEXT("repr"), TEXT("divmod"), TEXT("pow"), TEXT("self"), TEXT("Exception"), TEXT("ValueError"), TEXT("KeyError"),
			TEXT("IndexError"), TEXT("TypeError"), TEXT("RuntimeError") };
		return Words;
	}

	/** The scripting API's own names - what a script is given - in their own color, so they stand out from plain Python. */
	const TSet<FString>& GetApiNames()
	{
		static const TSet<FString> Words = {
			TEXT("anim"), TEXT("selection"), TEXT("unreal"), TEXT("TrackType"), TEXT("Ease"), TEXT("Curve"), TEXT("Starts"),
			TEXT("Timing"), TEXT("Direction"), TEXT("Stagger"), TEXT("Spacing"), TEXT("Order"), TEXT("Falloff"),
			TEXT("Smoothing"), TEXT("ScriptError") };
		return Words;
	}

	bool IsIdentifierStart(TCHAR Char)
	{
		return FChar::IsAlpha(Char) || Char == TEXT('_');
	}

	bool IsIdentifierChar(TCHAR Char)
	{
		return FChar::IsAlnum(Char) || Char == TEXT('_');
	}

	bool IsStringPrefix(TCHAR Char)
	{
		const TCHAR Lower = FChar::ToLower(Char);
		return Lower == TEXT('r') || Lower == TEXT('b') || Lower == TEXT('u') || Lower == TEXT('f');
	}
}

// =====================================================================================================
// FWidgetAnimPythonMarshaller
// =====================================================================================================

FWidgetAnimPythonMarshaller::FWidgetAnimPythonMarshaller(const FStyles& InStyles)
	: Styles(InStyles)
{
	using namespace WidgetAnimScriptPanelImpl;

}

void FWidgetAnimPythonMarshaller::SetText(const FString& SourceString, FTextLayout& TargetTextLayout)
{
	using namespace WidgetAnimScriptPanelImpl;

	TArray<FTextRange> LineRanges;
	FTextRange::CalculateLineRangesFromString(SourceString, LineRanges);

	TArray<FTextLayout::FNewLineData> Lines;
	Lines.Reserve(LineRanges.Num());

	// The quote of a triple-quoted string still open at the end of the line before, or none.
	TCHAR OpenTriple = 0;
	bool bAfterDefinitionWord = false;

	for (const FTextRange& LineRange : LineRanges)
	{
		const TSharedRef<FString> LineText = MakeShared<FString>(SourceString.Mid(LineRange.BeginIndex, LineRange.Len()));
		const FString& Line = *LineText;
		const int32 Length = Line.Len();

		TArray<TSharedRef<IRun>> Runs;
		auto Emit = [&Runs, &LineText](int32 Begin, int32 End, const FTextBlockStyle& Style)
		{
			if (End > Begin)
			{
				Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, Style, FTextRange(Begin, End)));
			}
		};

		int32 Index = 0;

		// The rest of a triple-quoted string from the lines above.
		if (OpenTriple != 0)
		{
			const FString Closing = FString::ChrN(3, OpenTriple);
			const int32 Close = Line.Find(Closing, ESearchCase::CaseSensitive);
			if (Close == INDEX_NONE)
			{
				Emit(0, Length, Styles.String);
				Index = Length;
			}
			else
			{
				Emit(0, Close + 3, Styles.String);
				Index = Close + 3;
				OpenTriple = 0;
			}
		}

		while (Index < Length)
		{
			const TCHAR Char = Line[Index];

			// A comment: the rest of the line.
			if (Char == TEXT('#'))
			{
				Emit(Index, Length, Styles.Comment);
				break;
			}

			// A string, with up to two prefix letters (f"", rb'').
			int32 QuoteAt = Index;
			while (QuoteAt < Length && QuoteAt - Index < 2 && IsStringPrefix(Line[QuoteAt]))
			{
				++QuoteAt;
			}
			if (QuoteAt < Length && (Line[QuoteAt] == TEXT('"') || Line[QuoteAt] == TEXT('\'')))
			{
				const TCHAR Quote = Line[QuoteAt];
				const bool bTriple = QuoteAt + 2 < Length && Line[QuoteAt + 1] == Quote && Line[QuoteAt + 2] == Quote;
				int32 End = Length;
				if (bTriple)
				{
					const int32 Close = Line.Find(FString::ChrN(3, Quote), ESearchCase::CaseSensitive, ESearchDir::FromStart, QuoteAt + 3);
					if (Close == INDEX_NONE)
					{
						OpenTriple = Quote;
					}
					else
					{
						End = Close + 3;
					}
				}
				else
				{
					int32 Scan = QuoteAt + 1;
					while (Scan < Length)
					{
						if (Line[Scan] == TEXT('\\'))
						{
							Scan += 2;
							continue;
						}
						if (Line[Scan] == Quote)
						{
							++Scan;
							break;
						}
						++Scan;
					}
					End = FMath::Min(Scan, Length);
				}
				Emit(Index, End, Styles.String);
				Index = End;
				bAfterDefinitionWord = false;
				continue;
			}

			// A number: digits, a decimal point, an exponent, hex and the like.
			if (FChar::IsDigit(Char) || (Char == TEXT('.') && Index + 1 < Length && FChar::IsDigit(Line[Index + 1])))
			{
				int32 End = Index + 1;
				while (End < Length)
				{
					const TCHAR Next = Line[End];
					const bool bExponentSign = (Next == TEXT('+') || Next == TEXT('-')) && (Line[End - 1] == TEXT('e') || Line[End - 1] == TEXT('E'));
					if (!(IsIdentifierChar(Next) || Next == TEXT('.') || bExponentSign))
					{
						break;
					}
					++End;
				}
				Emit(Index, End, Styles.Number);
				Index = End;
				bAfterDefinitionWord = false;
				continue;
			}

			// A word: a keyword, a constant, a builtin, one of the API's names - or what def or class names.
			if (IsIdentifierStart(Char))
			{
				int32 End = Index + 1;
				while (End < Length && IsIdentifierChar(Line[End]))
				{
					++End;
				}
				const FString Word = Line.Mid(Index, End - Index);

				const FTextBlockStyle* Style = &Styles.Normal;
				if (bAfterDefinitionWord)
				{
					Style = &Styles.Definition;
				}
				else if (GetKeywords().Contains(Word))
				{
					Style = &Styles.Keyword;
				}
				else if (GetConstants().Contains(Word))
				{
					Style = &Styles.Constant;
				}
				else if (GetApiNames().Contains(Word))
				{
					Style = &Styles.Api;
				}
				else if (GetBuiltins().Contains(Word))
				{
					Style = &Styles.Builtin;
				}
				Emit(Index, End, *Style);

				bAfterDefinitionWord = Word == TEXT("def") || Word == TEXT("class");
				Index = End;
				continue;
			}

			// White space, kept with whatever comes next being decided on its own.
			if (FChar::IsWhitespace(Char))
			{
				int32 End = Index + 1;
				while (End < Length && FChar::IsWhitespace(Line[End]))
				{
					++End;
				}
				Emit(Index, End, Styles.Normal);
				Index = End;
				continue;
			}

			// Operators and punctuation, a run of them at a time.
			int32 End = Index + 1;
			while (End < Length)
			{
				const TCHAR Next = Line[End];
				if (IsIdentifierChar(Next) || FChar::IsWhitespace(Next) || Next == TEXT('#') || Next == TEXT('"') || Next == TEXT('\''))
				{
					break;
				}
				++End;
			}
			Emit(Index, End, Styles.Operator);
			Index = End;
			bAfterDefinitionWord = false;
		}

		// An empty line still needs a run to put the cursor on.
		if (Runs.Num() == 0)
		{
			Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, Styles.Normal, FTextRange(0, 0)));
		}
		Lines.Emplace(LineText, MoveTemp(Runs));
	}

	TargetTextLayout.AddLines(Lines);
}

// =====================================================================================================
// FWidgetAnimScriptOutputMarshaller
// =====================================================================================================

void FWidgetAnimScriptOutputMarshaller::SetKindColors(const FLinearColor (&Colors)[5])
{
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Styles[Index].SetColorAndOpacity(FSlateColor(Colors[Index]));
	}
	MakeDirty();
}

FWidgetAnimScriptOutputMarshaller::FWidgetAnimScriptOutputMarshaller(const FTextBlockStyle& InBaseStyle)
{
	using namespace WidgetAnimScriptPanelImpl;

	const FLinearColor KindColors[] = { ScriptPalette::ScriptOutputInfo, ScriptPalette::ScriptOutputWarning, ScriptPalette::ScriptOutputError, ScriptPalette::ScriptStatusDone, ScriptPalette::ScriptStatusFailed };
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Styles[Index] = InBaseStyle;
		Styles[Index].SetColorAndOpacity(FSlateColor(KindColors[Index]));
	}
}

void FWidgetAnimScriptOutputMarshaller::SetText(const FString& SourceString, FTextLayout& TargetTextLayout)
{
	using namespace WidgetAnimScriptPanelImpl;

	TArray<FTextRange> LineRanges;
	FTextRange::CalculateLineRangesFromString(SourceString, LineRanges);

	TArray<FTextLayout::FNewLineData> Lines;
	Lines.Reserve(LineRanges.Num());
	for (int32 LineIndex = 0; LineIndex < LineRanges.Num(); ++LineIndex)
	{
		const TSharedRef<FString> LineText = MakeShared<FString>(SourceString.Mid(LineRanges[LineIndex].BeginIndex, LineRanges[LineIndex].Len()));
		const EKind Kind = LineKinds.IsValidIndex(LineIndex) ? LineKinds[LineIndex] : EKind::Info;

		TArray<TSharedRef<IRun>> Runs;
		Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, Styles[static_cast<int32>(Kind)]));
		Lines.Emplace(LineText, MoveTemp(Runs));
	}
	TargetTextLayout.AddLines(Lines);
}

// =====================================================================================================
// SWidgetAnimScriptPanel
// =====================================================================================================

void SWidgetAnimScriptPanel::Construct(const FArguments& InArgs)
{
	using namespace WidgetAnimScriptPanelImpl;

	Panel = InArgs._Panel;
	ScriptKey = InArgs._ScriptKey;
	FindPanel = InArgs._FindPanel;
	FindSource = InArgs._FindSource;
	bInDrawer = InArgs._InDrawer;
	OnClose = InArgs._OnClose;
	bInWindow = InArgs._InWindow;
	OnPopOut = InArgs._OnPopOut;
	bDocked = InArgs._Docked;
	OnDock = InArgs._OnDock;
	SeenScriptRevision = GetSessionRevision(ScriptKey);

	CodeText = LoadSessionScript(ScriptKey);

	OutputMarshaller = MakeShared<FWidgetAnimScriptOutputMarshaller>(MakeCodeStyle(ScriptPalette::ScriptOutputInfo));

	const FSlateFontInfo MonoFont = FCoreStyle::GetDefaultFontStyle("Mono", 9);

	SAssignNew(OutputBox, SMultiLineEditableTextBox)
		.Font(MonoFont)
		.Marshaller(OutputMarshaller)
		.IsReadOnly(true)
		.AutoWrapText(true)
		.HintText(ScriptTips::ScriptOutputHint())
		.BackgroundColor_Lambda([this]() { return FSlateColor(GetOutputColors().Background); })
		.ForegroundColor_Lambda([this]() { return FSlateColor(GetOutputColors().Kinds[0]); })
		.ReadOnlyForegroundColor_Lambda([this]() { return FSlateColor(GetOutputColors().Kinds[0]); });

	const FSlateBrush* RunIcon = PickBrush({ TEXT("Icons.Play"), TEXT("Animation.Forward"), TEXT("PlayWorld.PlayInViewport") });
	const FSlateBrush* PopOutIcon = PickBrush({ TEXT("Icons.OpenInExternalEditor"), TEXT("Icons.OpenInBrowser"), TEXT("Icons.Fullscreen") });
	const FSlateBrush* DockIcon = PickBrush({ TEXT("Icons.Layout"), TEXT("Icons.Dock"), TEXT("LevelEditor.Tabs.Details") });
	const FSlateBrush* CloseIcon = PickBrush({ TEXT("Icons.X"), TEXT("Icons.Close"), TEXT("Symbols.X") });

	// What a run edits: a timeline by the panel's timeline icon, an asset by its class icon.
	const FSlateBrush* OwnTimelineIcon = FNameTBDStyle::FindIcon(FNameTBDStyle::TimelineIcon);
	const FSlateBrush* TimelineIcon = OwnTimelineIcon ? OwnTimelineIcon : PickBrush({ TEXT("GraphEditor.Timeline_16x"), TEXT("ClassIcon.TimelineTemplate"), TEXT("ClassIcon.LevelSequence") });
	const FSlateBrush* OwnAssetIcon = FNameTBDStyle::Get().GetOptionalBrush("ClassIcon.WidgetAnimPresetAsset", nullptr, nullptr);
	const FSlateBrush* AssetIcon = OwnAssetIcon ? OwnAssetIcon : PickBrush({ TEXT("ClassIcon.DataAsset") });

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(0.f))
		[
			SNew(SVerticalBox)

			// The strip: what this is, and its buttons.
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
				.Padding(FMargin(8.f, 2.f, 4.f, 2.f))
				[
					SNew(SHorizontalBox)

					// What the code box shows: the script, or your functions.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						MakeDocumentTab(/*bFunctions=*/false)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
					[
						MakeDocumentTab(/*bFunctions=*/true)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
					[
						SNew(SWidgetAnimAnchoredTip)
						.Text(ScriptTips::ScriptRunTooltip())
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
							.ContentPadding(FMargin(8.f, 1.f))
							.IsEnabled(this, &SWidgetAnimScriptPanel::CanRun)
							.OnClicked_Lambda([this]()
							{
								Run();
								return FReply::Handled();
							})
							[
								MakeIconLabel(RunIcon, LOCTEXT("Run", "Run"))
							]
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(4.f, 0.f))
					[
						SNew(SWidgetAnimAnchoredTip)
						.Text(ScriptTips::ScriptSnippetsTooltip())
						[
							SNew(SComboButton)
							.ContentPadding(FMargin(4.f, 0.f))
							.OnGetMenuContent(this, &SWidgetAnimScriptPanel::MakeSnippetMenu)
							.ButtonContent()
							[
								SNew(STextBlock).Text(LOCTEXT("Snippets", "Snippets"))
							]
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(4.f, 0.f))
					[
						SNew(SWidgetAnimAnchoredTip)
						.Text(ScriptTips::ScriptApiReferenceTooltip())
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.IsEnabled_Lambda([this]() { return IsPythonAvailableCached(); })
							.OnClicked_Lambda([this]()
							{
								ShowApiReference();
								return FReply::Handled();
							})
							[
								SNew(STextBlock).Text(LOCTEXT("ApiReference", "API Reference"))
							]
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(4.f, 0.f))
					[
						SNew(SWidgetAnimAnchoredTip)
						.Text(ScriptTips::ScriptClearOutputTooltip())
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.OnClicked_Lambda([this]()
							{
								ClearOutput();
								return FReply::Handled();
							})
							[
								SNew(STextBlock).Text(LOCTEXT("ClearOutput", "Clear Output"))
							]
						]
					]

					// Runs on: the timeline (or asset) open in the Widget Animations panel, which is what a run edits.
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(FMargin(12.f, 0.f, 0.f, 0.f))
					[
						SNew(SWidgetAnimAnchoredTip)
						.Text(ScriptTips::ScriptTargetTooltip())
						.Visibility_Lambda([this]() { return GetTargetName().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
							[
								SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Text(LOCTEXT("RunsOn", "Runs on"))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
							[
								SNew(SBox)
								.WidthOverride(14.f)
								.HeightOverride(14.f)
								[
									SNew(SImage)
									.Image_Lambda([this, TimelineIcon, AssetIcon]() { return IsTargetTimeline() ? TimelineIcon : AssetIcon; })
									.ColorAndOpacity(FSlateColor::UseForeground())
								]
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.ColorAndOpacity(FSlateColor(ScriptPalette::ScriptTargetName))
								.Text(this, &SWidgetAnimScriptPanel::GetTargetName)
							]
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(8.f, 0.f))
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(this, &SWidgetAnimScriptPanel::GetCursorText)
					]

					// Dock as Tab: the area as a tab of the editor's layout - beside the Details panel to start with.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 2.f, 0.f))
					[
						SNew(SWidgetAnimAnchoredTip)
						.Visibility(!bDocked && OnDock.IsBound() ? EVisibility::Visible : EVisibility::Collapsed)
						.Text(bInDrawer ? ScriptTips::ScriptingDockInLayoutTooltip() : ScriptTips::DockScriptTooltip())
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(4.f, 2.f))
							.OnClicked_Lambda([this]()
							{
								SaveScript();
								SaveFunctions();
								OnDock.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								DockIcon
									? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)[SNew(SImage).Image(DockIcon).ColorAndOpacity(FSlateColor::UseForeground())])
									: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("DockAsTab", "Dock as Tab")))
							]
						]
					]

					// Open in Window: the area in a window of its own, which can go anywhere - another monitor.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 2.f, 0.f))
					[
						SNew(SWidgetAnimAnchoredTip)
						.Visibility(!bInWindow && !bDocked && OnPopOut.IsBound() ? EVisibility::Visible : EVisibility::Collapsed)
						.Text(ScriptTips::ScriptPopOutTooltip())
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(4.f, 2.f))
							.OnClicked_Lambda([this]()
							{
								SaveScript();
								OnPopOut.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								PopOutIcon
									? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)[SNew(SImage).Image(PopOutIcon).ColorAndOpacity(FSlateColor::UseForeground())])
									: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("PopOut", "Open in Window")))
							]
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SWidgetAnimAnchoredTip)
						.Text(bDocked ? ScriptTips::ScriptTabCloseTooltip() : bInWindow ? ScriptTips::ScriptWindowCloseTooltip() : bInDrawer ? ScriptTips::ScriptingDrawerCloseTooltip() : ScriptTips::ScriptCloseTooltip())
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ContentPadding(FMargin(4.f, 2.f))
							.OnClicked_Lambda([this]()
							{
								OnClose.ExecuteIfBound();
								return FReply::Handled();
							})
							[
								CloseIcon
									? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)[SNew(SImage).Image(CloseIcon).ColorAndOpacity(FSlateColor::UseForeground())])
									: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("Close", "x")))
							]
						]
					]
				]
			]

			// The code beside the output - or, docked as a tab (usually tall and narrow), above it.
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SSplitter)
				.Orientation(bDocked ? Orient_Vertical : Orient_Horizontal)
				.PhysicalSplitterHandleSize(2.f)

				+ SSplitter::Slot()
				.Value(bDocked ? 0.7f : 0.62f)
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					[
						SAssignNew(CodeHost, SBox)
					]

					// Without Python, what to do about it - over the code, which stays readable but can't run.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(16.f))
					[
						SNew(SBorder)
						.Visibility_Lambda([this]() { return IsPythonAvailableCached() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
						.Padding(FMargin(12.f, 8.f))
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Justification(ETextJustify::Center)
							.ColorAndOpacity(ScriptPalette::ScriptUnavailableText)
							.Text(ScriptTips::ScriptPythonUnavailable())
						]
					]
				]

				+ SSplitter::Slot()
				.Value(bDocked ? 0.3f : 0.38f)
				[
					OutputBox.ToSharedRef()
				]
			]
		]
	];

	// The code editor, or the plain box - and whichever the editor preferences switch to, from then on.
	BuildCodeEditor();
	SettingsChangedHandle = UWidgetAnimEditorSettings::GetMutable().OnSettingChanged().AddSP(this, &SWidgetAnimScriptPanel::OnEditorSettingsChanged);
	RegisterActiveTimer(1.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetAnimScriptPanel::SyncCodeEditor));
	RegisterActiveTimer(0.3f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetAnimScriptPanel::SyncFromSession));
}

SWidgetAnimScriptPanel::~SWidgetAnimScriptPanel()
{
	using namespace WidgetAnimScriptPanelImpl;

	UWidgetAnimEditorSettings::GetMutable().OnSettingChanged().Remove(SettingsChangedHandle);
	SaveScript();
	SaveFunctions();
}

bool SWidgetAnimScriptPanel::IsPythonAvailableCached() const
{
	using namespace WidgetAnimScriptPanelImpl;

	// Asked through reflection, so at most once a second: Python doesn't come and go while the editor runs.
	const double Now = FPlatformTime::Seconds();
	if (LastPythonCheck < 0.0 || Now - LastPythonCheck > 1.0)
	{
		LastPythonCheck = Now;
		bPythonAvailable = WidgetAnimScripting::IsPythonAvailable();
	}
	return bPythonAvailable;
}

FString SWidgetAnimScriptPanel::MakeScriptKey(const UObject* Asset)
{
	return Asset ? Asset->GetOutermost()->GetName() : FString();
}

TSharedPtr<SWidgetAnimTimeline> SWidgetAnimScriptPanel::GetPanel() const
{
	if (const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin())
	{
		return Pinned;
	}
	return FindPanel ? FindPanel() : nullptr;
}

IWidgetAnimPanelSource* SWidgetAnimScriptPanel::FindTargetSource(TSharedPtr<IWidgetAnimPanelSource>& OutKeepAlive) const
{
	if (const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin())
	{
		return Pinned->GetSource();
	}
	OutKeepAlive = FindSource ? FindSource() : nullptr;
	return OutKeepAlive.Get();
}

FText SWidgetAnimScriptPanel::GetTargetName() const
{
	TSharedPtr<IWidgetAnimPanelSource> KeepAlive;
	const IWidgetAnimPanelSource* Target = FindTargetSource(KeepAlive);
	return Target ? Target->GetEditedName() : FText::GetEmpty();
}

bool SWidgetAnimScriptPanel::IsTargetTimeline() const
{
	TSharedPtr<IWidgetAnimPanelSource> KeepAlive;
	const IWidgetAnimPanelSource* Target = FindTargetSource(KeepAlive);
	return Target && Target->GetMode() == IWidgetAnimPanelSource::EMode::Timeline;
}

bool SWidgetAnimScriptPanel::CanRun() const
{
	using namespace WidgetAnimScriptPanelImpl;

	// Not GetPanel: finding the panel may build one, and this is asked while painting. An area that finds its panel when it
	// runs counts as having one.
	const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin();
	const bool bHasPanel = Pinned.IsValid() ? Pinned->GetSource() != nullptr : static_cast<bool>(FindPanel);
	return bHasPanel && IsPythonAvailableCached();
}

void SWidgetAnimScriptPanel::Run()
{
	using namespace WidgetAnimScriptPanelImpl;

	const TSharedPtr<SWidgetAnimTimeline> Pinned = GetPanel();
	if (!Pinned.IsValid())
	{
		return;
	}

	// Your functions as they are in the box, and the script - whichever is shown, the script is what runs.
	SaveScript();
	SaveFunctions();
	if (CodeEditor.IsValid())
	{
		CodeEditor->ClearError();
	}
	const WidgetAnimScripting::FRunResult Result = WidgetAnimScripting::RunPanelScript(Pinned.ToSharedRef(), CodeText);

	FString Status;
	if (Result.bFailed)
	{
		Status = Result.bRolledBack
			? FText::Format(LOCTEXT("RunFailedRolledBack", "Stopped - {0}. Nothing was changed."), FText::FromString(Result.Error)).ToString()
			: FText::Format(LOCTEXT("RunFailed", "Stopped - {0}"), FText::FromString(Result.Error)).ToString();
	}
	else
	{
		const int32 TracksChange = Result.TracksAfter - Result.TracksBefore;
		const int32 GroupsChange = Result.GroupsAfter - Result.GroupsBefore;
		FText What = LOCTEXT("RunNoCountChange", "no tracks added or removed");
		if (TracksChange != 0 || GroupsChange != 0)
		{
			What = FText::Format(LOCTEXT("RunCounts", "{0} tracks, {1} groups (was {2} and {3})"),
				FText::AsNumber(Result.TracksAfter), FText::AsNumber(Result.GroupsAfter), FText::AsNumber(Result.TracksBefore), FText::AsNumber(Result.GroupsBefore));
		}
		Status = FText::Format(LOCTEXT("RunDone", "Done in {0} ms: {1}."), FText::AsNumber(FMath::RoundToInt(Result.Seconds * 1000.0)), What).ToString();
	}
	ShowOutput(Result.Output, Status, Result.bFailed);

	// The panel follows what the script did.
	Pinned->OnScriptRan(Result.TracksBefore, Result.TracksAfter, Result.GroupsBefore, Result.GroupsAfter);

	// Your functions may have changed: the code editor suggests them as they are now.
	SendCompletions(/*bRefresh=*/true);

	// The line it stopped on, under the cursor - in the script, or in your functions file, shown for it.
	const bool bErrorInFunctions = !Result.ErrorFile.IsEmpty()
		&& FPaths::IsSamePath(Result.ErrorFile, WidgetAnimScripting::GetFunctionsFile(/*bCreate=*/false));
	if (Result.bFailed && Result.ErrorLine > 0 && (Result.ErrorFile.IsEmpty() || bErrorInFunctions))
	{
		SetEditingFunctions(bErrorInFunctions);
		if (CodeEditor.IsValid())
		{
			CodeEditor->ShowError(Result.ErrorLine, Result.Error);
		}
		else if (CodeBox.IsValid())
		{
			CodeBox->GoTo(FTextLocation(Result.ErrorLine - 1, 0));
			FSlateApplication::Get().SetKeyboardFocus(CodeBox, EFocusCause::SetDirectly);
		}
	}
}

FReply SWidgetAnimScriptPanel::OnCodeKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	using namespace WidgetAnimScriptPanelImpl;

	const FKey Key = InKeyEvent.GetKey();
	const bool bCommand = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();

	// Ctrl+Enter runs, as in the editor's own Python console.
	if (Key == EKeys::Enter && bCommand)
	{
		if (CanRun())
		{
			Run();
		}
		return FReply::Handled();
	}

	// Tab indents by four spaces, as Python is written, rather than moving focus away.
	if (Key == EKeys::Tab && !bCommand && !InKeyEvent.IsAltDown())
	{
		if (!InKeyEvent.IsShiftDown())
		{
			CodeBox->InsertTextAtCursor(FString(TEXT("    ")));
		}
		return FReply::Handled();
	}

	// Enter keeps the line's indent - and adds one after a colon, where a block starts.
	if (Key == EKeys::Enter && !InKeyEvent.IsShiftDown() && !InKeyEvent.IsAltDown())
	{
		FString Line;
		CodeBox->GetCurrentTextLine(Line);
		const int32 Column = FMath::Clamp(CodeBox->GetCursorLocation().GetOffset(), 0, Line.Len());

		FString Indent;
		for (const TCHAR Char : Line)
		{
			if (Char != TEXT(' ') && Char != TEXT('\t'))
			{
				break;
			}
			Indent.AppendChar(Char);
		}
		if (Indent.Len() > Column)
		{
			Indent = Indent.Left(Column);
		}

		FString Before = Line.Left(Column);
		int32 Hash = INDEX_NONE;
		if (Before.FindChar(TEXT('#'), Hash))
		{
			Before = Before.Left(Hash);
		}
		if (Before.TrimEnd().EndsWith(TEXT(":")))
		{
			Indent += TEXT("    ");
		}

		CodeBox->InsertTextAtCursor(FString(TEXT("\n")) + Indent);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SWidgetAnimScriptPanel::OnCodeChanged(const FText& NewText)
{
	using namespace WidgetAnimScriptPanelImpl;

	OnDocumentTextChanged(NewText.ToString());
}

TSharedRef<SWidget> SWidgetAnimScriptPanel::MakeSnippetMenu()
{
	using namespace WidgetAnimScriptPanelImpl;

	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	TArray<FString> Folders;
	WidgetAnimScripting::GetSnippetFolders(Folders);

	bool bAny = false;
	for (int32 FolderIndex = 0; FolderIndex < Folders.Num(); ++FolderIndex)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Folders[FolderIndex] / TEXT("*.py")), /*Files=*/true, /*Directories=*/false);
		if (Files.Num() == 0)
		{
			continue;
		}
		Files.Sort();

		Menu.BeginSection(NAME_None, FolderIndex == 0 ? LOCTEXT("PluginSnippets", "Snippets") : LOCTEXT("ProjectSnippets", "This Project's Snippets"));
		for (const FString& File : Files)
		{
			const FString FilePath = Folders[FolderIndex] / File;
			Menu.AddMenuEntry(
				FText::FromString(FPaths::GetBaseFilename(File)),
				ScriptTips::ScriptSnippetEntryTooltip(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SWidgetAnimScriptPanel::OpenSnippet, FilePath)));
		}
		Menu.EndSection();
		bAny = true;
	}

	if (!bAny)
	{
		Menu.AddMenuEntry(LOCTEXT("NoSnippets", "No snippets found"), ScriptTips::ScriptNoSnippets(), FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}

	// Your own: the script saved as one, the folder they are in, and taking one out.
	Menu.BeginSection(NAME_None, LOCTEXT("YourSnippets", "Your Snippets"));
	{
		const TSharedRef<SEditableTextBox> NameBox = SNew(SEditableTextBox)
			.MinDesiredWidth(180.f)
			.HintText(ScriptTips::ScriptSaveSnippetHint())
			.ToolTipText(ScriptTips::ScriptSaveSnippetTooltip())
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter && !Text.IsEmptyOrWhitespace())
				{
					FSlateApplication::Get().DismissAllMenus();
					SaveAsSnippet(Text.ToString());
				}
			});
		Menu.AddWidget(NameBox, LOCTEXT("SaveAsSnippet", "Save Script As"), /*bNoIndent=*/false, /*bSearchable=*/false);

		Menu.AddMenuEntry(
			LOCTEXT("OpenSnippetsFolder", "Open Snippets Folder"),
			ScriptTips::ScriptOpenSnippetsFolderTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				const FString Folder = WidgetAnimScripting::GetProjectSnippetFolder();
				IFileManager::Get().MakeDirectory(*Folder, /*Tree=*/true);
				FPlatformProcess::ExploreFolder(*Folder);
			})));

		TArray<FString> ProjectFiles;
		const FString ProjectFolder = WidgetAnimScripting::GetProjectSnippetFolder();
		IFileManager::Get().FindFiles(ProjectFiles, *(ProjectFolder / TEXT("*.py")), /*Files=*/true, /*Directories=*/false);
		ProjectFiles.Sort();
		if (ProjectFiles.Num() > 0)
		{
			Menu.AddSubMenu(
				LOCTEXT("DeleteSnippet", "Delete Snippet"),
				ScriptTips::ScriptDeleteSnippetTooltip(),
				FNewMenuDelegate::CreateLambda([this, ProjectFiles, ProjectFolder](FMenuBuilder& SubMenu)
				{
					for (const FString& File : ProjectFiles)
					{
						const FString FilePath = ProjectFolder / File;
						SubMenu.AddMenuEntry(
							FText::FromString(FPaths::GetBaseFilename(File)),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([this, FilePath]() { DeleteSnippet(FilePath); })));
					}
				}));
		}
	}
	Menu.EndSection();

	// Your functions: the file every run loads, and its folder.
	Menu.BeginSection(NAME_None, LOCTEXT("YourFunctions", "Your Functions"));
	Menu.AddMenuEntry(
		LOCTEXT("EditFunctions", "Edit My Functions"),
		ScriptTips::ScriptTabFunctionsTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { SetEditingFunctions(true); })));
	Menu.AddMenuEntry(
		LOCTEXT("OpenFunctionsFolder", "Open Functions Folder"),
		ScriptTips::ScriptOpenFunctionsFolderTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			const FString Folder = FPaths::GetPath(WidgetAnimScripting::GetFunctionsFile(/*bCreate=*/true));
			FPlatformProcess::ExploreFolder(*Folder);
		})));
	Menu.EndSection();

	if (!PreviousScript.IsEmpty())
	{
		Menu.AddSeparator();
		Menu.AddMenuEntry(
			LOCTEXT("RestorePrevious", "Restore Previous Script"),
			ScriptTips::ScriptRestorePreviousTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this]()
			{
				const FString Current = CodeText;
				SetCode(PreviousScript, /*bUndoable=*/true);
				PreviousScript = Current;
			})));
	}
	return Menu.MakeWidget();
}

void SWidgetAnimScriptPanel::OpenSnippet(FString FilePath)
{
	using namespace WidgetAnimScriptPanelImpl;

	FString Snippet;
	if (!FFileHelper::LoadFileToString(Snippet, *FilePath))
	{
		return;
	}

	// The script it replaces stays one click away, in the same menu.
	PreviousScript = CodeText;
	SetCode(Snippet.Replace(TEXT("\r\n"), TEXT("\n")), /*bUndoable=*/true);
}

void SWidgetAnimScriptPanel::ShowApiReference()
{
	using namespace WidgetAnimScriptPanelImpl;

	TArray<WidgetAnimScripting::FOutputLine> Lines;
	const bool bRan = WidgetAnimScripting::RunScriptingCall(TEXT("widget_anim_scripting.print_api_reference()"), Lines);
	ShowOutput(Lines, bRan ? FString() : LOCTEXT("ApiReferenceFailed", "The API reference couldn't be read: the widget_anim_scripting module didn't load.").ToString(), !bRan);
}

void SWidgetAnimScriptPanel::ClearOutput()
{
	using namespace WidgetAnimScriptPanelImpl;

	OutputMarshaller->LineKinds.Reset();
	OutputBox->SetText(FText::GetEmpty());
}

void SWidgetAnimScriptPanel::ShowOutput(const TArray<WidgetAnimScripting::FOutputLine>& Lines, const FString& Status, bool bFailed)
{
	using namespace WidgetAnimScriptPanelImpl;

	using EKind = FWidgetAnimScriptOutputMarshaller::EKind;

	TArray<FString> Text;
	TArray<EKind> Kinds;
	for (const WidgetAnimScripting::FOutputLine& Line : Lines)
	{
		const EKind Kind = Line.Kind == WidgetAnimScripting::FOutputLine::EKind::Error ? EKind::Error
			: (Line.Kind == WidgetAnimScripting::FOutputLine::EKind::Warning ? EKind::Warning : EKind::Info);

		// One entry may hold several lines - a traceback - each colored as the entry is.
		TArray<FString> Split;
		Line.Text.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Split, TEXT("\n"), /*bCullEmpty=*/false);
		while (Split.Num() > 0 && Split.Last().IsEmpty())
		{
			Split.Pop();
		}
		for (const FString& Part : Split)
		{
			Text.Add(Part);
			Kinds.Add(Kind);
		}
	}
	if (!Status.IsEmpty())
	{
		if (Text.Num() > 0)
		{
			Text.Add(FString());
			Kinds.Add(EKind::Info);
		}
		Text.Add(Status);
		Kinds.Add(bFailed ? EKind::Failed : EKind::Done);
	}

	OutputMarshaller->LineKinds = MoveTemp(Kinds);

	// Set twice when the text is the same as before - a run that printed exactly what the last one did - since the box only
	// lays its text out again when it changes, and the colors may have.
	const FText NewText = FText::FromString(FString::Join(Text, TEXT("\n")));
	if (OutputBox->GetText().EqualTo(NewText))
	{
		OutputBox->SetText(FText::GetEmpty());
	}
	OutputBox->SetText(NewText);
	OutputBox->ScrollTo(FTextLocation(FMath::Max(Text.Num() - 1, 0), 0));
}

void SWidgetAnimScriptPanel::SaveScript() const
{
	using namespace WidgetAnimScriptPanelImpl;

	// Only an area up to date with the session speaks for it: one hidden while another changed the script - closed with the
	// editor, say - would otherwise save what it last saw over it.
	if (SeenScriptRevision == GetSessionRevision(ScriptKey))
	{
		GetSessionScript(ScriptKey) = CodeText;
	}
	const FString Script = GetSessionScript(ScriptKey).Get(CodeText);

	const FString Path = GetScriptFilePath(ScriptKey);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	FFileHelper::SaveStringToFile(Script, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FText SWidgetAnimScriptPanel::GetCursorText() const
{
	using namespace WidgetAnimScriptPanelImpl;

	if (CodeEditor.IsValid())
	{
		return FText::Format(LOCTEXT("CursorLocation", "Ln {0}, Col {1}"), FText::AsNumber(CodeEditor->GetCursorLine()), FText::AsNumber(CodeEditor->GetCursorColumn()));
	}
	if (!CodeBox.IsValid())
	{
		return FText::GetEmpty();
	}
	const FTextLocation Location = CodeBox->GetCursorLocation();
	return FText::Format(LOCTEXT("CursorLocation", "Ln {0}, Col {1}"), FText::AsNumber(Location.GetLineIndex() + 1), FText::AsNumber(Location.GetOffset() + 1));
}

// -----------------------------------------------------------------------------------------------------
// The code: the code editor, or the plain box
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimScriptPanel::BuildCodeEditor()
{
	using namespace WidgetAnimScriptPanelImpl;

	if (!CodeHost.IsValid())
	{
		return;
	}

	const bool bUseCodeEditor = UWidgetAnimEditorSettings::Get().bUseCodeEditor && !bCodeEditorFailed && SWidgetAnimMonacoEditor::IsAvailable();
	if (!bUseCodeEditor)
	{
		CodeEditor.Reset();
		CodeHost->SetContent(MakePlainCodeBox());
		RefreshOutputStyles();
		return;
	}

	CodeBox.Reset();
	bCompletionsSent = false;
	WidgetNamesHash = 0;
	SAssignNew(CodeEditor, SWidgetAnimMonacoEditor)
		.InitialText(bEditingFunctions ? FunctionsText : CodeText)
		.OnTextChanged_Lambda([this](const FString& NewText)
		{
			OnDocumentTextChanged(NewText);
		})
		.OnRun_Lambda([this](const FString& NewText)
		{
			OnDocumentTextChanged(NewText);
			if (CanRun())
			{
				Run();
			}
		})
		.OnSave_Lambda([this]()
		{
			SaveScript();
			SaveFunctions();
		})
		.OnFailed(FSimpleDelegate::CreateSP(this, &SWidgetAnimScriptPanel::OnCodeEditorFailed));
	CodeHost->SetContent(CodeEditor.ToSharedRef());
	SendCompletions();
	RefreshOutputStyles();
}

TSharedRef<SWidget> SWidgetAnimScriptPanel::MakePlainCodeBox()
{
	using namespace WidgetAnimScriptPanelImpl;

	FWidgetAnimPythonMarshaller::FStyles Styles;
	Styles.Normal = MakeCodeStyle(ScriptPalette::ScriptPlain);
	Styles.Keyword = MakeCodeStyle(ScriptPalette::ScriptKeyword);
	Styles.Constant = MakeCodeStyle(ScriptPalette::ScriptConstant);
	Styles.String = MakeCodeStyle(ScriptPalette::ScriptString);
	Styles.Comment = MakeCodeStyle(ScriptPalette::ScriptComment);
	Styles.Number = MakeCodeStyle(ScriptPalette::ScriptNumber);
	Styles.Builtin = MakeCodeStyle(ScriptPalette::ScriptBuiltin);
	Styles.Api = MakeCodeStyle(ScriptPalette::ScriptApi);
	Styles.Definition = MakeCodeStyle(ScriptPalette::ScriptDefinition);
	Styles.Operator = MakeCodeStyle(ScriptPalette::ScriptOperator);
	const TSharedRef<FWidgetAnimPythonMarshaller> CodeMarshaller = MakeShared<FWidgetAnimPythonMarshaller>(Styles);

	const FSlateFontInfo MonoFont = FCoreStyle::GetDefaultFontStyle("Mono", 9);

	return SAssignNew(CodeBox, SMultiLineEditableTextBox)
		.Font(MonoFont)
		.Marshaller(CodeMarshaller)
		.Text(FText::FromString(bEditingFunctions ? FunctionsText : CodeText))
		.HintText(ScriptTips::ScriptCodeHint())
		.AutoWrapText(false)
		.BackgroundColor(ScriptPalette::ScriptCodeBackground)
		.OnKeyDownHandler(this, &SWidgetAnimScriptPanel::OnCodeKeyDown)
		.OnTextChanged(this, &SWidgetAnimScriptPanel::OnCodeChanged);
}

void SWidgetAnimScriptPanel::SetCode(const FString& NewCode, bool bUndoable)
{
	using namespace WidgetAnimScriptPanelImpl;

	// Always the script: a snippet picked on the Functions tab goes back to it.
	SetEditingFunctions(false);
	CodeText = NewCode;
	PublishScript(NewCode);
	if (CodeEditor.IsValid())
	{
		CodeEditor->SetText(NewCode, bUndoable);
	}
	else if (CodeBox.IsValid())
	{
		CodeBox->SetText(FText::FromString(NewCode));
	}
}

void SWidgetAnimScriptPanel::OnEditorSettingsChanged(UObject* Settings, FPropertyChangedEvent& Event)
{
	using namespace WidgetAnimScriptPanelImpl;

	// Use Code Editor turned on or off. The code editor follows the rest of the settings itself.
	const bool bWantCodeEditor = UWidgetAnimEditorSettings::Get().bUseCodeEditor && !bCodeEditorFailed && SWidgetAnimMonacoEditor::IsAvailable();
	if (bWantCodeEditor != CodeEditor.IsValid())
	{
		BuildCodeEditor();
	}

	// The output follows the theme.
	RefreshOutputStyles();
}

void SWidgetAnimScriptPanel::OnCodeEditorFailed()
{
	using namespace WidgetAnimScriptPanelImpl;

	bCodeEditorFailed = true;
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([WeakThis = TWeakPtr<SWidgetAnimScriptPanel>(SharedThis(this))](double, float)
	{
		if (const TSharedPtr<SWidgetAnimScriptPanel> Pinned = WeakThis.Pin())
		{
			Pinned->BuildCodeEditor();
			Pinned->ShowOutput({}, ScriptTips::ScriptEditorFailed().ToString(), /*bFailed=*/true);
		}
		return EActiveTimerReturnType::Stop;
	}));
}

void SWidgetAnimScriptPanel::SendCompletions(bool bRefresh)
{
	using namespace WidgetAnimScriptPanelImpl;

	// Worked out by asking the module to describe itself: once a session, and again after a run - your functions may
	// have changed.
	static FString Completions;
	static bool bAsked = false;
	if (bRefresh)
	{
		bAsked = false;
	}
	if (!bAsked && IsPythonAvailableCached())
	{
		bAsked = true;
		FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NameTBD/ScriptCompletions.json"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		Path.ReplaceInline(TEXT("'"), TEXT("\\'"));

		TArray<WidgetAnimScripting::FOutputLine> Ignored;
		if (WidgetAnimScripting::RunScriptingCall(FString::Printf(TEXT("widget_anim_scripting.write_editor_completions('%s')"), *Path), Ignored))
		{
			FFileHelper::LoadFileToString(Completions, *Path.Replace(TEXT("\\'"), TEXT("'")));
		}
	}
	if (CodeEditor.IsValid() && !Completions.IsEmpty())
	{
		CodeEditor->SetCompletionsJson(Completions);
		bCompletionsSent = true;
	}
}

EActiveTimerReturnType SWidgetAnimScriptPanel::SyncCodeEditor(double InCurrentTime, float InDeltaTime)
{
	using namespace WidgetAnimScriptPanelImpl;

	// Your functions, written as they are edited - like the script (see NoteSessionChanged).
	SaveFunctions();

	const TSharedPtr<SWidgetAnimTimeline> Pinned = GetPanel();
	IWidgetAnimPanelSource* Source = Pinned.IsValid() ? Pinned->GetSource() : nullptr;
	if (!CodeEditor.IsValid() || !Source)
	{
		return EActiveTimerReturnType::Continue;
	}

	// Python may have come up after the code editor did.
	if (!bCompletionsSent)
	{
		SendCompletions();
	}

	if (!WidgetReader.IsValid())
	{
		WidgetReader.Reset(NewObject<UWidgetAnimScriptAnimation>(GetTransientPackage()));
	}
	WidgetReader->InitForPanel(Source->AsShared(), Pinned);

	// Only told again when the widgets change.
	const TArray<FString> Paths = WidgetReader->GetWidgetPaths();
	uint32 Hash = GetTypeHash(Paths.Num());
	for (const FString& Path : Paths)
	{
		Hash = HashCombine(Hash, GetTypeHash(Path));
	}
	if (Hash == WidgetNamesHash)
	{
		return EActiveTimerReturnType::Continue;
	}
	WidgetNamesHash = Hash;

	TArray<SWidgetAnimMonacoEditor::FWidgetName> Names;
	Names.Reserve(Paths.Num());
	for (const FString& Path : Paths)
	{
		SWidgetAnimMonacoEditor::FWidgetName& Name = Names.AddDefaulted_GetRef();
		Name.Path = Path;
		int32 Slash = INDEX_NONE;
		Name.Name = Path.FindLastChar(TEXT('/'), Slash) ? Path.Mid(Slash + 1) : Path;
		Name.Class = WidgetReader->GetWidgetClass(Path);
	}
	CodeEditor->SetWidgetNames(Names);
	return EActiveTimerReturnType::Continue;
}

void SWidgetAnimScriptPanel::PublishScript(const FString& Script)
{
	using namespace WidgetAnimScriptPanelImpl;

	GetSessionScript(ScriptKey) = Script;
	SeenScriptRevision = ++GetSessionRevision(ScriptKey);
	NoteSessionChanged(ScriptKey);
}

EActiveTimerReturnType SWidgetAnimScriptPanel::SyncFromSession(double InCurrentTime, float InDeltaTime)
{
	using namespace WidgetAnimScriptPanelImpl;

	// Runs only while this area is on screen: one shown again takes up what was written elsewhere meanwhile.
	if (SeenScriptRevision == GetSessionRevision(ScriptKey))
	{
		return EActiveTimerReturnType::Continue;
	}
	SeenScriptRevision = GetSessionRevision(ScriptKey);

	const FString Script = GetSessionScript(ScriptKey).Get(FString());
	if (Script != CodeText)
	{
		CodeText = Script;
		if (bEditingFunctions)
		{
			return EActiveTimerReturnType::Continue;
		}
		if (CodeEditor.IsValid())
		{
			CodeEditor->SetText(Script, /*bUndoable=*/false);
		}
		else if (CodeBox.IsValid())
		{
			CodeBox->SetText(FText::FromString(Script));
		}
	}
	return EActiveTimerReturnType::Continue;
}

SWidgetAnimScriptPanel::FOutputColors SWidgetAnimScriptPanel::GetOutputColors() const
{
	using namespace WidgetAnimScriptPanelImpl;

	FOutputColors Colors;
	const EWidgetAnimScriptEditorTheme Theme = CodeEditor.IsValid() ? UWidgetAnimEditorSettings::Get().ScriptEditorTheme : EWidgetAnimScriptEditorTheme::Unreal;
	auto Set = [&Colors](const FColor& Background, const FColor& Info, const FColor& Warning, const FColor& Error, const FColor& Done)
	{
		Colors.Background = FLinearColor(Background);
		Colors.Kinds[0] = FLinearColor(Info);
		Colors.Kinds[1] = FLinearColor(Warning);
		Colors.Kinds[2] = FLinearColor(Error);
		Colors.Kinds[3] = FLinearColor(Done);
		Colors.Kinds[4] = FLinearColor(Error);
	};

	// VS Code's own panel colors for its themes, so the output reads as part of the same editor.
	switch (Theme)
	{
	case EWidgetAnimScriptEditorTheme::Dark:
		Set(FColor(0x18, 0x18, 0x18), FColor(0xcc, 0xcc, 0xcc), FColor(0xcc, 0xa7, 0x00), FColor(0xf1, 0x4c, 0x4c), FColor(0x89, 0xd1, 0x85));
		break;
	case EWidgetAnimScriptEditorTheme::Light:
		Set(FColor(0xf8, 0xf8, 0xf8), FColor(0x3b, 0x3b, 0x3b), FColor(0x89, 0x5d, 0x03), FColor(0xc7, 0x24, 0x24), FColor(0x38, 0x8a, 0x34));
		break;
	case EWidgetAnimScriptEditorTheme::HighContrast:
		Set(FColor(0x00, 0x00, 0x00), FColor(0xff, 0xff, 0xff), FColor(0xff, 0xd3, 0x70), FColor(0xf4, 0x87, 0x71), FColor(0x89, 0xd1, 0x85));
		break;
	default:
		Colors.Background = ScriptPalette::ScriptOutputBackground;
		Colors.Kinds[0] = ScriptPalette::ScriptOutputInfo;
		Colors.Kinds[1] = ScriptPalette::ScriptOutputWarning;
		Colors.Kinds[2] = ScriptPalette::ScriptOutputError;
		Colors.Kinds[3] = ScriptPalette::ScriptStatusDone;
		Colors.Kinds[4] = ScriptPalette::ScriptStatusFailed;
		break;
	}
	return Colors;
}

void SWidgetAnimScriptPanel::RefreshOutputStyles()
{
	using namespace WidgetAnimScriptPanelImpl;

	if (!OutputMarshaller.IsValid() || !OutputBox.IsValid())
	{
		return;
	}
	OutputMarshaller->SetKindColors(GetOutputColors().Kinds);

	// Laid out again, in the new colors.
	const FText Current = OutputBox->GetText();
	OutputBox->SetText(FText::GetEmpty());
	OutputBox->SetText(Current);
}

// -----------------------------------------------------------------------------------------------------
// Your snippets and functions
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimScriptPanel::SaveAsSnippet(const FString& Name)
{
	using namespace WidgetAnimScriptPanelImpl;

	// A file name from what was typed: nothing a file name can't hold.
	FString FileName = Name.TrimStartAndEnd();
	FileName.RemoveFromEnd(TEXT(".py"));
	for (const TCHAR Invalid : FString(TEXT("\\/:*?\"<>|")))
	{
		FileName.ReplaceCharInline(Invalid, TEXT(' '));
	}
	FileName = FileName.TrimStartAndEnd();
	if (FileName.IsEmpty())
	{
		return;
	}

	const FString Folder = WidgetAnimScripting::GetProjectSnippetFolder();
	const FString Path = Folder / (FileName + TEXT(".py"));
	if (IFileManager::Get().FileExists(*Path))
	{
		const FText Question = FText::Format(LOCTEXT("ReplaceSnippet", "There is a snippet called {0} already. Replace it with this script?"), FText::FromString(FileName));
		if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
		{
			return;
		}
	}

	IFileManager::Get().MakeDirectory(*Folder, /*Tree=*/true);
	const bool bSaved = FFileHelper::SaveStringToFile(CodeText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	ShowOutput({}, bSaved
		? FText::Format(LOCTEXT("SnippetSaved", "Saved as the snippet {0} - under This Project's Snippets."), FText::FromString(FileName)).ToString()
		: FText::Format(LOCTEXT("SnippetNotSaved", "Couldn't save {0}."), FText::FromString(Path)).ToString(), !bSaved);
}

void SWidgetAnimScriptPanel::DeleteSnippet(FString FilePath)
{
	using namespace WidgetAnimScriptPanelImpl;

	const FString Name = FPaths::GetBaseFilename(FilePath);
	const FText Question = FText::Format(LOCTEXT("DeleteSnippetQuestion", "Delete the snippet {0}?"), FText::FromString(Name));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
	{
		return;
	}
	const bool bDeleted = IFileManager::Get().Delete(*FilePath);
	ShowOutput({}, bDeleted
		? FText::Format(LOCTEXT("SnippetDeleted", "Deleted the snippet {0}."), FText::FromString(Name)).ToString()
		: FText::Format(LOCTEXT("SnippetNotDeleted", "Couldn't delete {0}."), FText::FromString(FilePath)).ToString(), !bDeleted);
}

void SWidgetAnimScriptPanel::SaveFunctions()
{
	using namespace WidgetAnimScriptPanelImpl;

	if (!bFunctionsDirty || FunctionsFile.IsEmpty())
	{
		return;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FunctionsFile), /*Tree=*/true);
	FFileHelper::SaveStringToFile(FunctionsText, *FunctionsFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	bFunctionsDirty = false;
}

void SWidgetAnimScriptPanel::SetEditingFunctions(bool bFunctions)
{
	using namespace WidgetAnimScriptPanelImpl;

	if (bFunctions == bEditingFunctions)
	{
		return;
	}

	if (bFunctions)
	{
		// Read afresh each time: the file may have been edited somewhere else meanwhile.
		SaveScript();
		FunctionsFile = WidgetAnimScripting::GetFunctionsFile(/*bCreate=*/true);
		FunctionsText.Reset();
		FFileHelper::LoadFileToString(FunctionsText, *FunctionsFile);
		FunctionsText.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		bFunctionsDirty = false;
	}
	else
	{
		SaveFunctions();
	}
	bEditingFunctions = bFunctions;

	const FString& Shown = bFunctions ? FunctionsText : CodeText;
	TGuardValue<bool> Switching(bSwitchingDocument, true);
	if (CodeEditor.IsValid())
	{
		CodeEditor->ClearError();
		CodeEditor->SetText(Shown, /*bUndoable=*/false);
	}
	else if (CodeBox.IsValid())
	{
		CodeBox->SetText(FText::FromString(Shown));
	}
}

void SWidgetAnimScriptPanel::OnDocumentTextChanged(const FString& NewText)
{
	using namespace WidgetAnimScriptPanelImpl;

	if (bSwitchingDocument)
	{
		return;
	}
	if (bEditingFunctions)
	{
		if (NewText != FunctionsText)
		{
			FunctionsText = NewText;
			bFunctionsDirty = true;
		}
		return;
	}
	CodeText = NewText;
	PublishScript(NewText);
}

TSharedRef<SWidget> SWidgetAnimScriptPanel::MakeDocumentTab(bool bFunctions)
{
	using namespace WidgetAnimScriptPanelImpl;

	return SNew(SWidgetAnimAnchoredTip)
		.Text(bFunctions ? ScriptTips::ScriptTabFunctionsTooltip() : ScriptTips::ScriptTabScriptTooltip())
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(6.f, 1.f))
			.OnClicked_Lambda([this, bFunctions]()
			{
				SetEditingFunctions(bFunctions);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.Text(bFunctions ? LOCTEXT("FunctionsTab", "Functions") : LOCTEXT("ScriptTab", "Script"))
				.ColorAndOpacity_Lambda([this, bFunctions]()
				{
					return bEditingFunctions == bFunctions ? FSlateColor(ScriptPalette::ScriptToggleOn) : FSlateColor::UseSubduedForeground();
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
