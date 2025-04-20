#pragma once
#include "CoreMinimal.h"

// Forward declarations
struct FAsyncProgress;

/** Delegate for progress updates on async operations */
DECLARE_DELEGATE_OneParam(FAsyncProgressDelegate, const FAsyncProgress&);

/** Delegate for completion notifications on async operations */
DECLARE_DELEGATE_OneParam(FTypeRegistrationCompletionDelegate, bool); 