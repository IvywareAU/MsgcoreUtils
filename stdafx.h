// stdafx.h : precompiled header for MsgcoreUtils.
//
//  It is the Msgcore repository root's stdafx.h and nothing else, ON PURPOSE.
//  MsgcoreUtils compiles against Msgcore's headers, and those headers are
//  written against exactly the environment that file establishes - MFC, the
//  Platform shim, the include ORDER (WinSock2 before windows.h), the warning
//  suppressions and the C++ standard-library set. A second, similar-looking
//  preamble here would be a second thing to keep in step, and the first time
//  it drifted the symptom would be a redefinition or an include-order error
//  inside Msgcore's headers rather than inside this project.
//
//  A quoted include resolves relative to THIS file first, so "../Msgcore/stdafx.h"
//  reaches the copy in the Msgcore checkout BESIDE this one, and its own
//  "Platform/platform.h" then resolves relative to THAT root - which is where
//  Platform/ is. No project-level include directory is needed for either,
//  exactly as in Msgcore(2026).vcxproj.
//
#pragma once

#include "../Msgcore/stdafx.h"
