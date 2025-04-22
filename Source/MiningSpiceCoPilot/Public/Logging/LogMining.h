// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Logging categories for the Mining system
 */
DECLARE_LOG_CATEGORY_EXTERN(LogMining, Log, All);

// Core system categories
DECLARE_LOG_CATEGORY_EXTERN(LogMiningZones, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMiningRegistry, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMiningMaterials, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMiningMemory, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMiningThreading, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMiningTaskSystem, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMiningAsyncOps, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMiningSVO, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMiningSDF, Log, All);

// Registry-specific categories
DECLARE_LOG_CATEGORY_EXTERN(LogZoneTypeRegistry, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMaterialRegistry, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSDFTypeRegistry, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogSVOTypeRegistry, Log, All); 