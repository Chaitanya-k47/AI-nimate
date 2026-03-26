// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "AInimatePluginStyle.h"

class FAInimatePluginCommands : public TCommands<FAInimatePluginCommands>
{
public:

	FAInimatePluginCommands()
		: TCommands<FAInimatePluginCommands>(TEXT("AInimatePlugin"), NSLOCTEXT("Contexts", "AInimatePlugin", "AInimatePlugin Plugin"), NAME_None, FAInimatePluginStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};