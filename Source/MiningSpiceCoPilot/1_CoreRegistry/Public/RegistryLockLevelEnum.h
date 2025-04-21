#pragma once

// Use file-level include guard
#ifndef MINING_SPICE_REGISTRY_LOCK_LEVEL_ENUM_H
#define MINING_SPICE_REGISTRY_LOCK_LEVEL_ENUM_H

#include "CoreMinimal.h"
#include "../../3_ThreadingTaskSystem/Public/ThreadSafety.h"
#include "../../3_ThreadingTaskSystem/Public/TaskSystem/TaskTypes.h"

// Registry Section enumeration for cache population
#if !defined(REGISTRY_SECTION_ENUM_DEFINED)
#define REGISTRY_SECTION_ENUM_DEFINED

enum class ERegistrySection : uint8
{
    All,
    MaterialTypes,
    MaterialRelationships,
    MaterialProperties,
    ZoneTypes,
    ProcessTypes,
    Services
};
#endif // REGISTRY_SECTION_ENUM_DEFINED

#endif // MINING_SPICE_REGISTRY_LOCK_LEVEL_ENUM_H 