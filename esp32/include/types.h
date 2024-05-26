#pragma once

#include <cstddef>
#include <cstdint>
#include <WString.h>

struct EmbeddedFile
{
    // Embedded file size in bytes
    const size_t length;
    // Contents as bytes
    const uint8_t *const contents;

    EmbeddedFile(const uint8_t start[], const uint8_t end[]) :
        length(end - start),
        contents(start)
    {}
};

struct SettingSpec
{
    // Note that if this enum is expanded, ToName() must be also!
    enum class SettingType : int
    {
        Integer,
        PositiveBigInteger,
        Float,
        Boolean,
        String,
        Palette
    };

    String Name;
    String FriendlyName;
    String Description;
    SettingType Type;

    SettingSpec(const String& name, const String& friendlyName, const String& description, SettingType type)
      : Name(name),
        FriendlyName(friendlyName),
        Description(description),
        Type(type)
    {}

    SettingSpec(const String& name, const String& friendlyName, SettingType type) : SettingSpec(name, friendlyName, "", type)
    {}

    SettingSpec()
    {}

    String static ToName(SettingType type)
    {
        String names[] = { "Integer", "PositiveBigInteger", "Float", "Boolean", "String", "Palette" };
        return names[(int)type];
    }
};