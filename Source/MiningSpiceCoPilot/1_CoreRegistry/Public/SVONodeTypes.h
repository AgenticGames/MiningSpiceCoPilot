// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Using a more robust approach to prevent redefinition issues
#ifndef SVO_NODE_TYPES_H
#define SVO_NODE_TYPES_H

// Ensure ESVONodeClass is only defined once
#ifndef ESVO_NODE_CLASS_DEFINED
#define ESVO_NODE_CLASS_DEFINED

/**
 * SVO node class types for classification in the registry
 */
enum class ESVONodeClass : uint8
{
    /** Homogeneous nodes with a single material throughout */
    Homogeneous,
    
    /** Interface nodes that contain multiple materials */
    Interface,
    
    /** Empty nodes with no material */
    Empty,
    
    /** Custom node type for specialized behavior */
    Custom
};

#endif // ESVO_NODE_CLASS_DEFINED 

#endif // SVO_NODE_TYPES_H 