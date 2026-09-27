// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Components/SlateWrapperTypes.h"
#include "UObject/Interface.h"

#include "WidgetAnimScreenTransitions.generated.h"

class UCommonActivatableWidget;
class UWidget;
class UWidgetAnimationComponent;

/** What a screen plays each time it is activated. */
UENUM(BlueprintType)
enum class EWidgetAnimScreenActivateAnim : uint8
{
	AnimIn UMETA(DisplayName = "Anim In", ToolTip = "Replays the screen's Anim In, cascading into every Widget Animation Component inside it."),
	AnimIds UMETA(DisplayName = "Anim IDs", ToolTip = "Plays only the entries with these Anim IDs - a lighter 'welcome back' than the full entrance."),
	None UMETA(ToolTip = "Plays nothing on activation. The screen still animates however its components are set to, such as Anim In on construct.")
};

/**
 * How a screen on a Common UI stack animates as it opens, returns and closes.
 *
 * The contract, and why it is shaped like this:
 *  - Open. A pushed screen is constructed and activated in the same frame. Its Anim In is the one it already
 *    plays on construct, so activation never plays it a second time on top.
 *  - Activate. Every other activation - returning after the screen above it was popped, or a first activation
 *    long after construct (a screen that sat in a switcher) - plays On Activate.
 *  - Close. Common UI removes a screen the moment it deactivates, and the stack's handling of that is neither
 *    public nor virtual, so an exit can never play after deactivation. Close Screen therefore plays Anim Out
 *    first and deactivates the screen only once every exit inside it has finished; the stack then pops it.
 *  - Covered. Pushing a screen on top deactivates this one and the stack's switcher hides it at once, so there
 *    is nothing to see and nothing is played. Being covered part-way through an exit cancels the close.
 *
 * Set the stack's Transition Duration to 0 for screens that use this, or Common UI's own fade runs on top.
 */
USTRUCT(BlueprintType)
struct NAMETBDCOMMONUI_API FWidgetAnimScreenTransitionSettings
{
	GENERATED_BODY()

	/** What plays when the screen is activated, other than the push that opens it (which is its Anim In on construct). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Transitions")
	EWidgetAnimScreenActivateAnim OnActivate = EWidgetAnimScreenActivateAnim::AnimIn;

	/** The Anim IDs played on activation when On Activate is Anim IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Transitions", meta = (EditCondition = "OnActivate == EWidgetAnimScreenActivateAnim::AnimIds", EditConditionHides))
	TArray<FName> ActivateAnimIds;

	/**
	 * Close Screen (and the back action) plays Anim Out on the screen and everything inside it before the screen
	 * deactivates. Off: it deactivates at once, as a plain Common UI screen does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Transitions")
	bool bAnimateClose = true;

	/**
	 * Makes the screen ignore the mouse while its exit plays, so nothing on a screen that is already leaving can be
	 * clicked. Further back presses are swallowed either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Transitions", meta = (EditCondition = "bAnimateClose"))
	bool bBlockInputWhileClosing = true;

	/**
	 * The longest a close waits for its exit animations before deactivating the screen anyway. A safety net: an
	 * exit normally ends on its own, but a component that has stopped ticking would otherwise hold the screen
	 * open forever.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Transitions", meta = (EditCondition = "bAnimateClose", ClampMin = "0.1", UIMin = "0.1", Units = "s"))
	float MaxCloseSeconds = 5.f;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UWidgetAnimScreen : public UInterface
{
	GENERATED_BODY()
};

/**
 * A Common UI screen that closes with an exit animation. Implemented by UWidgetAnimActivatableWidget, and by any
 * activatable widget of your own that embeds FWidgetAnimScreenTransitionDriver; the screen library's Close / Pop /
 * Remove nodes use it to tell such screens apart from plain ones, which they simply deactivate.
 */
class NAMETBDCOMMONUI_API IWidgetAnimScreen
{
	GENERATED_BODY()

public:

	/** Plays the exit, then deactivates. Returns true if the close is animating, false if it happened at once (or there was nothing to close). */
	virtual bool RequestAnimatedClose() = 0;

	/** True while an exit is playing and the screen is waiting to deactivate. */
	virtual bool IsClosingAnimated() const = 0;
};

/**
 * The transition contract as plain C++, so it is not tied to one base class.
 *
 * UWidgetAnimActivatableWidget is built on it. To give an activatable widget class of your own (a Lyra-style base,
 * say) the same behaviour, embed one of these and forward four calls to it:
 *  - NativeOnActivated   -> NotifyActivated
 *  - NativeOnDeactivated -> NotifyDeactivated
 *  - NativeDestruct      -> NotifyDestructed (before Super)
 *  - NativeOnHandleBackAction and your close function -> RequestClose
 * and implement IWidgetAnimScreen by forwarding to RequestClose / IsClosing. Skip NotifyActivated for an
 * activation that happens inside Super::NativeConstruct (Auto Activate): that is the screen opening, whose entrance
 * is the Anim In its components start on construct.
 *
 * The screen's animations are whatever Widget Animation Components it has: its own, if one is bound to the screen
 * widget itself, otherwise the nearest ones inside it. Holds only weak references, so it is safe as a plain member
 * of a UObject.
 */
class NAMETBDCOMMONUI_API FWidgetAnimScreenTransitionDriver
{
public:

	FWidgetAnimScreenTransitionDriver() = default;
	~FWidgetAnimScreenTransitionDriver();

	FWidgetAnimScreenTransitionDriver(const FWidgetAnimScreenTransitionDriver&) = delete;
	FWidgetAnimScreenTransitionDriver& operator=(const FWidgetAnimScreenTransitionDriver&) = delete;

	/** Plays Settings.OnActivate, unless the screen is still playing the entrance it started on construct. Cancels a close in progress. */
	void NotifyActivated(UCommonActivatableWidget& Screen, const FWidgetAnimScreenTransitionSettings& Settings);

	/** A deactivation that this driver did not ask for (covered by a push, removed from its stack) cancels a close in progress. */
	void NotifyDeactivated(UCommonActivatableWidget& Screen);

	/** The screen left the UI: any close in progress is dropped. */
	void NotifyDestructed(UCommonActivatableWidget& Screen);

	/**
	 * Plays the exit and deactivates the screen once it has finished. Returns true if the close is animating (or was
	 * already), false if the screen deactivated at once or was not active to begin with.
	 */
	bool RequestClose(UCommonActivatableWidget& Screen, const FWidgetAnimScreenTransitionSettings& Settings);

	bool IsClosing() const { return bClosing; }

	/** The components that play the screen's own In / Out: the one bound to Screen, else the nearest ones inside it. */
	static void GetRootComponents(UWidget& Screen, TArray<UWidgetAnimationComponent*>& OutComponents);

	/** The root components plus every component their cascade reaches - everything a close has to wait for. */
	static void GetAllComponents(UWidget& Screen, TArray<UWidgetAnimationComponent*>& OutComponents);

private:

	/** Core ticker callback while closing. Returns false once the close is over, which unregisters it. */
	bool TickClose(float DeltaTime);

	/** Every exit is done (or the time limit passed): deactivate the screen. */
	void FinishClose();

	/** Stops waiting without deactivating, and gives the screen its input back. */
	void CancelClose();

	void ResetCloseState();
	void RestoreInput();

	TWeakObjectPtr<UCommonActivatableWidget> ClosingScreen;
	TArray<TWeakObjectPtr<UWidgetAnimationComponent>> WaitingOn;
	FTSTicker::FDelegateHandle CloseTickHandle;

	float CloseElapsed = 0.f;
	float CloseTimeLimit = 0.f;

	/** The visibility the screen had before the close made it hit-test invisible, when it did. */
	TOptional<ESlateVisibility> VisibilityBeforeClose;

	bool bClosing = false;

	/** True while FinishClose itself deactivates the screen, so NotifyDeactivated knows the deactivation is ours. */
	bool bFinishingClose = false;
};
