// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_ANDROID || PLATFORM_IOS

#include "ChromiumMobileJSScripting.h"
#include "Backends/JsonStructSerializerBackend.h"

class UObject;

/**
 * Implements a writer for UStruct serialization using JavaScript.
 *
 * Based on FJsonStructSerializerBackend, it adds support for certain object types not representable in pure JSON
 *
 */
class FChromiumMobileJSStructSerializerBackend
	: public FJsonStructSerializerBackend
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InScripting An instance of a web browser scripting obnject.
	 */
	FChromiumMobileJSStructSerializerBackend(FChromiumMobileJSScriptingRef InScripting);

public:
	virtual void WriteProperty(const FStructSerializerState& State, int32 ArrayIndex = 0) override;

	FString ToString();

private:
	void WriteUObject(const FStructSerializerState& State, UObject* Value);

	FChromiumMobileJSScriptingRef Scripting;
	TArray<uint8> ReturnBuffer;
	FMemoryWriter Writer;
};

#endif // PLATFORM_ANDROID || PLATFORM_IOS