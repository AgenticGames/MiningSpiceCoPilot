// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"

/**
 * Helper function to convert from a string to an SDF operation type
 */
inline ESDFOperationType GetSDFOperationTypeFromString(const FString& OperationTypeName)
{
    if (OperationTypeName.Equals(TEXT("Union"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::Union;
    }
    else if (OperationTypeName.Equals(TEXT("Subtraction"), ESearchCase::IgnoreCase) || 
             OperationTypeName.Equals(TEXT("Subtract"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::Subtraction;
    }
    else if (OperationTypeName.Equals(TEXT("Intersection"), ESearchCase::IgnoreCase) ||
             OperationTypeName.Equals(TEXT("Intersect"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::Intersection;
    }
    else if (OperationTypeName.Equals(TEXT("SmoothUnion"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::SmoothUnion;
    }
    else if (OperationTypeName.Equals(TEXT("SmoothSubtraction"), ESearchCase::IgnoreCase) ||
             OperationTypeName.Equals(TEXT("SmoothSubtract"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::SmoothSubtraction;
    }
    else if (OperationTypeName.Equals(TEXT("SmoothIntersection"), ESearchCase::IgnoreCase) ||
             OperationTypeName.Equals(TEXT("SmoothIntersect"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::SmoothIntersection;
    }
    else if (OperationTypeName.Equals(TEXT("Smoothing"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::Smoothing;
    }
    else if (OperationTypeName.Equals(TEXT("Evaluation"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::Evaluation;
    }
    else if (OperationTypeName.Equals(TEXT("Gradient"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::Gradient;
    }
    else if (OperationTypeName.Equals(TEXT("NarrowBandUpdate"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::NarrowBandUpdate;
    }
    else if (OperationTypeName.Equals(TEXT("MaterialTransition"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::MaterialTransition;
    }
    else if (OperationTypeName.Equals(TEXT("VolumeRender"), ESearchCase::IgnoreCase))
    {
        return ESDFOperationType::VolumeRender;
    }
    
    return ESDFOperationType::Custom;
} 