// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Core includes
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/Interface.h"
#include "UObject/ScriptInterface.h"

// Platform-specific includes
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/CriticalSection.h"

// Container includes
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "Templates/SharedPointer.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

// Misc includes
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"

// Serialization includes
#include "Serialization/ArchiveUObject.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

// Logging includes
#include "Logging/LogMining.h"

// Core registry includes
#include "../../1_CoreRegistry/Public/CommonServiceTypes.h"

// Module specific includes
#include "ServiceManager.h"
#include "DependencyResolver.h"
#include "ServiceHealthMonitor.h"
#include "ServiceDebugVisualizer.h"
#include "Interfaces/IMemoryAwareService.h"
#include "Interfaces/ISaveableService.h" 