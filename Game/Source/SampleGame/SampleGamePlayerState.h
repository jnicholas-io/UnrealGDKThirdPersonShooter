// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Teams/SampleGameTeams.h"
#include "SampleGamePlayerState.generated.h"


// Delegate to allow Character class to register a listener to the OnRep_SelectedTeam event
DECLARE_DELEGATE(CharacterListenerDelegate);

UCLASS()
class SAMPLEGAME_API ASampleGamePlayerState : public APlayerState
{
	GENERATED_BODY()

protected:
	UPROPERTY(Transient, ReplicatedUsing = OnRep_SelectedTeam)
	ESampleGameTeam SelectedTeam;

	// Delegate to allow Character class to register a listener to the OnRep_SelectedTeam event
	CharacterListenerDelegate CharacterListenerSelectedTeam;
	
public:
	void SetSelectedTeam(const ESampleGameTeam& NewSelectedTeam) { SelectedTeam = NewSelectedTeam; }
	const ESampleGameTeam GetSelectedTeam() const { return SelectedTeam; }

	UFUNCTION()
	void OnRep_SelectedTeam();

	// TODO: UNR-356 - jamescrowder - Replace the below functionality with something better, or remove if no longer relevant, once additional login features are implemented
	//-----------------------------------//
	void RegisterCharacterListenerForSelectedTeam(class ASampleGameCharacter* CharacterInstance);
	void UnregisterCharacterListenerForSelectedTeam();
	//-----------------------------------//
};
