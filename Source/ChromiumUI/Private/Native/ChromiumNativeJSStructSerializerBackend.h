// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChromiumNativeJSScripting.h"
#include "Backends/JsonStructSerializerBackend.h"

class UObject;

/**
 * Implements a writer for UStruct serialization using JavaScript.
 *
 * Based on FJsonStructSerializerBackend, it adds support for certain object types not representable in pure JSON
 *
 */
class FChromiumNativeJSStructSerializerBackend
	: public FJsonStructSerializerBackend
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InScripting An instance of a web browser scripting obnject.
	 */
	FChromiumNativeJSStructSerializerBackend(FChromiumNativeJSScriptingRef InScripting, FMemoryWriter& Writer);

public:
	virtual void WriteProperty(const FStructSerializerState& State, int32 ArrayIndex = 0) override;

private:
	void WriteUObject(const FStructSerializerState& State, UObject* Value);

	FChromiumNativeJSScriptingRef Scripting;
};
