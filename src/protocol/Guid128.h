// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jochona project contributors
//
// Portable helpers for JochonaGuid128 (see include/jochona/display_adapter_abi.h).
// No Windows dependency: usable from the driver, from Host, and from the
// cross-platform unit tests.
#pragma once

#include <cstring>

#include "jochona/display_adapter_abi.h"

namespace jochona::protocol {

inline bool IsZero(const JochonaGuid128& guid) noexcept
{
    for (unsigned char byte : guid.Bytes)
    {
        if (byte != 0)
        {
            return false;
        }
    }
    return true;
}

inline bool Equals(const JochonaGuid128& a, const JochonaGuid128& b) noexcept
{
    return std::memcmp(a.Bytes, b.Bytes, sizeof(a.Bytes)) == 0;
}

inline JochonaGuid128 ZeroGuid() noexcept
{
    JochonaGuid128 guid{};
    std::memset(guid.Bytes, 0, sizeof(guid.Bytes));
    return guid;
}

} // namespace jochona::protocol
