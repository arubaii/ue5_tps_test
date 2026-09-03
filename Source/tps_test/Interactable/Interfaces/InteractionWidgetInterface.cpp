// Copyright arubaii. All rights reserved.

#include "InteractionWidgetInterface.h"
#include "Blueprint/UserWidget.h"

// The one default with a body: routing opacity to the widget's own render opacity means
// a prompt gets distance fading for free, which is what you want ~always.
void IInteractionWidgetInterface::SetInteractionOpacity_Implementation(float Opacity)
{
	if (UUserWidget* AsWidget = Cast<UUserWidget>(_getUObject()))
	{
		AsWidget->SetRenderOpacity(Opacity);
	}
}
