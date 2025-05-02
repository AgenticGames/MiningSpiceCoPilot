#pragma once

#include "CoreMinimal.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

/**
 * SerializationHelpers.h
 * Provides operator overloads for FMemoryWriter and FMemoryReader to handle 
 * serialization of primitive types that don't have built-in Unreal Engine operators.
 */

// FMemoryWriter operators for primitive types

// uint32 serialization
inline FMemoryWriter& operator<<(FMemoryWriter& Writer, const uint32 Value)
{
    Writer.Serialize((void*)&Value, sizeof(uint32));
    return Writer;
}

// int32 serialization
inline FMemoryWriter& operator<<(FMemoryWriter& Writer, const int32 Value)
{
    Writer.Serialize((void*)&Value, sizeof(int32));
    return Writer;
}

// uint64 serialization
inline FMemoryWriter& operator<<(FMemoryWriter& Writer, const uint64 Value)
{
    Writer.Serialize((void*)&Value, sizeof(uint64));
    return Writer;
}

// int64 serialization
inline FMemoryWriter& operator<<(FMemoryWriter& Writer, const int64 Value)
{
    Writer.Serialize((void*)&Value, sizeof(int64));
    return Writer;
}

// float serialization
inline FMemoryWriter& operator<<(FMemoryWriter& Writer, const float Value)
{
    Writer.Serialize((void*)&Value, sizeof(float));
    return Writer;
}

// double serialization
inline FMemoryWriter& operator<<(FMemoryWriter& Writer, const double Value)
{
    Writer.Serialize((void*)&Value, sizeof(double));
    return Writer;
}

// bool serialization
inline FMemoryWriter& operator<<(FMemoryWriter& Writer, const bool Value)
{
    uint8 Temp = Value ? 1 : 0;
    Writer.Serialize(&Temp, sizeof(uint8));
    return Writer;
}

// FString serialization - simplified version that avoids ambiguity with Archive operators
inline FMemoryWriter& WriteString(FMemoryWriter& Writer, const FString& Value)
{
    // First write the length
    int32 Length = Value.Len();
    Writer << Length;

    // Then write the data if there is any
    if (Length > 0)
    {
        Writer.Serialize((void*)*Value, Length * sizeof(TCHAR));
    }
    return Writer;
}

// FMemoryReader operators for primitive types

// uint32 deserialization
inline FMemoryReader& operator>>(FMemoryReader& Reader, uint32& Value)
{
    Reader.Serialize(&Value, sizeof(uint32));
    return Reader;
}

// int32 deserialization
inline FMemoryReader& operator>>(FMemoryReader& Reader, int32& Value)
{
    Reader.Serialize(&Value, sizeof(int32));
    return Reader;
}

// uint64 deserialization
inline FMemoryReader& operator>>(FMemoryReader& Reader, uint64& Value)
{
    Reader.Serialize(&Value, sizeof(uint64));
    return Reader;
}

// int64 deserialization
inline FMemoryReader& operator>>(FMemoryReader& Reader, int64& Value)
{
    Reader.Serialize(&Value, sizeof(int64));
    return Reader;
}

// float deserialization
inline FMemoryReader& operator>>(FMemoryReader& Reader, float& Value)
{
    Reader.Serialize(&Value, sizeof(float));
    return Reader;
}

// double deserialization
inline FMemoryReader& operator>>(FMemoryReader& Reader, double& Value)
{
    Reader.Serialize(&Value, sizeof(double));
    return Reader;
}

// bool deserialization
inline FMemoryReader& operator>>(FMemoryReader& Reader, bool& Value)
{
    uint8 Temp;
    Reader.Serialize(&Temp, sizeof(uint8));
    Value = (Temp != 0);
    return Reader;
}

// FString deserialization - simplified version that avoids ambiguity with Archive operators
inline FMemoryReader& ReadString(FMemoryReader& Reader, FString& Value)
{
    // First read the length
    int32 Length;
    Reader >> Length;

    // If there is a length, read the string data
    if (Length > 0)
    {
        TArray<TCHAR> CharArray;
        CharArray.SetNumUninitialized(Length + 1); // +1 for null terminator
        Reader.Serialize(CharArray.GetData(), Length * sizeof(TCHAR));
        CharArray[Length] = '\0'; // Ensure null termination
        Value = FString(CharArray.GetData());
    }
    else
    {
        Value.Empty();
    }
    return Reader;
}