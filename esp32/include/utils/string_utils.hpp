#pragma once
#include <string>

// Source - https://stackoverflow.com/a
// Posted by Joseph, modified by community. See post 'Timeline' for change history
// Retrieved 2026-01-26, License - CC BY-SA 3.0

inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}
