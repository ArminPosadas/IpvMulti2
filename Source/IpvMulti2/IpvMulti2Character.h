// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/OnlineSessionDelegates.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Logging/LogMacros.h"
#include "IpvMulti2Character.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(config=Game)
class AIpvMulti2Character : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* FireAction;

public:
	AIpvMulti2Character();
	
	void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    

protected:
    
    void Move(const FInputActionValue& Value);
    
    void Look(const FInputActionValue& Value);
            

protected:

    virtual void NotifyControllerChanged() override;

    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
    /** Getter for Max Health.*/
    UFUNCTION(BlueprintPure, Category="Health")
    FORCEINLINE float GetMaxHealth() const { return MaxHealth; }

    /** Getter for Current Health.*/
    UFUNCTION(BlueprintPure, Category="Health")
    FORCEINLINE float GetCurrentHealth() const { return CurrentHealth; }

    /** Setter for Current Health. Clamps the value between 0 and MaxHealth and calls OnHealthUpdate. Should only be called on the server.*/
    UFUNCTION(BlueprintCallable, Category="Health")
    void SetCurrentHealth(float healthValue);

    /** Event for taking damage. Overridden from APawn.*/
    UFUNCTION(BlueprintCallable, Category = "Health")
    float TakeDamage( float DamageTaken, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser ) override;
    
    FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
    FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

    /** Getter for Current Ammo.*/
    UFUNCTION(BlueprintPure, Category="Ammo")
    FORCEINLINE int32 GetCurrentAmmo() const { return CurrentAmmo; }

    /** Getter for Max Ammo.*/
    UFUNCTION(BlueprintPure, Category="Ammo")
    FORCEINLINE int32 GetMaxAmmo() const { return MaxAmmo; }

    /** Function to add ammo (used by pickup)*/
    UFUNCTION(BlueprintCallable, Category="Ammo")
    void AddAmmo(int32 Amount);

    UPROPERTY(BlueprintReadOnly, Category="Gameplay")
    bool bIsCarryingObjective;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Respawn")
    float RespawnDuration = 3.0f;  // Default value, editable in Blueprints

protected:
    
    UPROPERTY(EditDefaultsOnly, Category = "Health")
    float MaxHealth;
    
    UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth)
    float CurrentHealth;
    
    UPROPERTY(EditDefaultsOnly, Category = "Ammo")
    int32 MaxAmmo;
    
    UPROPERTY(ReplicatedUsing = OnRep_CurrentAmmo)
    int32 CurrentAmmo;
    
    UFUNCTION()
    void OnRep_CurrentHealth();
    
    UFUNCTION()
    void OnRep_CurrentAmmo();
    
    UFUNCTION(BlueprintImplementableEvent, Category="Ammo")
    void OnAmmoUpdated();
    
    UFUNCTION(BlueprintNativeEvent, Category="Health")
    void OnHealthUpdate();

    UFUNCTION()
    void StartRagdoll();
    void DisableCharacterCollision();

    UPROPERTY(ReplicatedUsing = OnRep_IsRagdoll)
    bool bIsRagdoll;
    
    UFUNCTION()
    void OnRep_IsRagdoll();
    
    UFUNCTION(Server, Reliable)
    void ServerStartRagdoll();

    UFUNCTION(BlueprintCallable, Category = "UI")
    void HideUI();

    UFUNCTION(Server, Reliable)
    void ServerRespawn();

    void UpdateTimerDisplay();
    
    UPROPERTY(BlueprintReadOnly, Category = "UI")
    float RespawnTimeRemaining;

    FTimerHandle TimerUpdateHandle;
    FTimerHandle RespawnTimerHandle;

public:
    IOnlineSessionPtr OnlineSessionInterface;

protected:
    
    UFUNCTION(BlueprintCallable)
    void CreateGameSession();

    void OnFindSessionsComplete(bool bWasSuccess);
    
    //Callbacks
    void OnCreateSessionComplete(FName SessionName, bool bWasSuccess);

    UFUNCTION(BlueprintCallable)
    void JoinGameSession();

private:

    //Delegate
    FOnCreateSessionCompleteDelegate CreateSessionCompleteDelegate;
    
    FOnFindSessionsCompleteDelegate FindSessionsCompleteDelegate;

    //SessionSearch
    TSharedPtr<FOnlineSessionSearch> SessionSearch;
};
