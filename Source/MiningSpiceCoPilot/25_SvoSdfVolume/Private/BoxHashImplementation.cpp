#include "CoreMinimal.h"
#include "Math/Box.h"
#include "Templates/TypeHash.h"
#include "../Public/BoxHash.h"

// Implementation of GetTypeHash for FBox
uint32 GetTypeHash(const FBox& Box)
{
    // Create a hash by combining the hash values of Min and Max vectors
    uint32 MinHash = GetTypeHash(Box.Min);
    uint32 MaxHash = GetTypeHash(Box.Max);
    return HashCombine(MinHash, MaxHash);
}