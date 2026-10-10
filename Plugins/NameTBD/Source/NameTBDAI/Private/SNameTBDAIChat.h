// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FNameTBDAIChatSession;
class SMultiLineEditableTextBox;
class SScrollBox;
class SVerticalBox;
struct FNameTBDAIChatItem;

/**
 * The chat panel (Window > NameTBD AI): the conversation, a Build / Ask switch, Attach Selection, the input, and per-request
 * tokens and cost. Changes a request made end on a card with Apply / Discard. The chat itself lives in the module, so closing
 * the tab keeps it.
 */
class SNameTBDAIChat : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNameTBDAIChat) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SNameTBDAIChat() override;

private:
	void HandleSessionChanged();
	void RebuildList();
	TSharedRef<SWidget> MakeItemWidget(const FNameTBDAIChatItem& Item);
	TSharedRef<SWidget> MakeRichText(const FString& Text, const FSlateColor& Color);
	TSharedRef<SWidget> MakeWelcome();
	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeFooter();

	void SendInput();
	FReply OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);
	FText GetModelText() const;
	FText GetSetupWarning() const;

	TSharedPtr<FNameTBDAIChatSession> Session;
	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SVerticalBox> ItemList;
	TSharedPtr<SMultiLineEditableTextBox> InputBox;
	FDelegateHandle ChangedHandle;

	bool bAskMode = false;
	bool bAttachSelection = false;

	/** What the list was last built from, to rebuild only when items or card states change. */
	int32 BuiltCount = INDEX_NONE;
	FString BuiltSignature;
};
