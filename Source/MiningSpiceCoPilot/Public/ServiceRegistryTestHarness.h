// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Test harness for demonstrating the ServiceRegistry and Dependency system
 * Provides concrete examples of services running through the system with
 * cross-cutting concerns like dependency resolution, memory management and state preservation
 */
class MININGSPICECOPILOT_API FServiceRegistryTestHarness
{
public:
    /**
     * Runs a comprehensive set of tests demonstrating the ServiceRegistry system
     * @return True if all tests pass
     */
    static bool RunTests();
}; 