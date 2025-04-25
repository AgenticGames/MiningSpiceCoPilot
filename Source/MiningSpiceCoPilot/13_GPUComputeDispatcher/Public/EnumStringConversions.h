// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "HardwareProfileManager.h"

/**
 * Utility functions for converting enums to strings.
 * This is used instead of UEnum::GetValueAsString which requires reflection setup
 * that may not be available in this context.
 */

/**
 * Helper function to convert from ESDFOperationType to string
 */
inline FString GetSDFOperationTypeString(ESDFOperationType OperationType)
{
    switch (OperationType)
    {
        case ESDFOperationType::Union: return TEXT("Union");
        case ESDFOperationType::Subtraction: return TEXT("Subtraction");
        case ESDFOperationType::Intersection: return TEXT("Intersection");
        case ESDFOperationType::SmoothUnion: return TEXT("SmoothUnion");
        case ESDFOperationType::SmoothSubtraction: return TEXT("SmoothSubtraction");
        case ESDFOperationType::SmoothIntersection: return TEXT("SmoothIntersection");
        case ESDFOperationType::Custom: return TEXT("Custom");
        case ESDFOperationType::Smoothing: return TEXT("Smoothing");
        case ESDFOperationType::Evaluation: return TEXT("Evaluation");
        case ESDFOperationType::Gradient: return TEXT("Gradient");
        case ESDFOperationType::NarrowBandUpdate: return TEXT("NarrowBandUpdate");
        case ESDFOperationType::MaterialTransition: return TEXT("MaterialTransition");
        case ESDFOperationType::VolumeRender: return TEXT("VolumeRender");
        default: return TEXT("Unknown");
    }
}

/**
 * Helper function to convert from EMemoryStrategy to string
 */
inline FString GetMemoryStrategyString(EMemoryStrategy Strategy)
{
    switch (Strategy)
    {
        case EMemoryStrategy::Unified: return TEXT("Unified");
        case EMemoryStrategy::Dedicated: return TEXT("Dedicated");
        case EMemoryStrategy::Staged: return TEXT("Staged");
        case EMemoryStrategy::Tiled: return TEXT("Tiled");
        case EMemoryStrategy::Adaptive: return TEXT("Adaptive");
        default: return TEXT("Unknown");
    }
}

/**
 * Helper function to convert from EComputePrecision to string
 */
inline FString GetComputePrecisionString(EComputePrecision Precision)
{
    switch (Precision)
    {
        case EComputePrecision::Full: return TEXT("Full");
        case EComputePrecision::Half: return TEXT("Half");
        case EComputePrecision::Mixed: return TEXT("Mixed");
        case EComputePrecision::Variable: return TEXT("Variable");
        default: return TEXT("Unknown");
    }
}

/**
 * Helper function to convert from EGPUVendor to string
 */
inline FString GetGPUVendorString(EGPUVendor Vendor)
{
    switch (Vendor)
    {
        case EGPUVendor::Unknown: return TEXT("Unknown");
        case EGPUVendor::NVIDIA: return TEXT("NVIDIA");
        case EGPUVendor::AMD: return TEXT("AMD");
        case EGPUVendor::Intel: return TEXT("Intel");
        case EGPUVendor::Apple: return TEXT("Apple");
        case EGPUVendor::ARM: return TEXT("ARM");
        case EGPUVendor::ImgTec: return TEXT("ImgTec");
        case EGPUVendor::Qualcomm: return TEXT("Qualcomm");
        case EGPUVendor::Other: return TEXT("Other");
        default: return TEXT("Unknown");
    }
} 