// Copyright © 2026 Ivyware Pty Ltd, Khrustal & Mann
//              MELBOURNE, VICTORIA, AUSTRALIA, 3000
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
//
//  MsgcoreUtils - linkage control.
//
//  NOTES: MsgcoreUtils is a SEPARATE component that sits ON TOP of Msgcore. It
//         consumes the C++ object model (P2PmsgMgr / P3PmsgItem / P3PmsgAttr /
//         P3PmsgDesc / P3PmsgList / P3PmsgVect) through Msgcore's exported
//         surface and adds nothing to Msgcore itself. Nothing in Msgcore knows
//         this component exists, and that is deliberate: a renderer is a
//         consumer of the store, not part of it.
//       : What it provides is text projection of a message store -
//           P2PmsgMgr >> PrintJson   -> the store as a JSON document
//           P2PmsgMgr >> PrintPHP    -> the store as PHP class declarations
//         and both work on any P3PmsgItem, not just a manager, because
//         P2PmsgMgr IS a P3PmsgItem (it derives from it) and a subtree is a
//         perfectly good thing to render.
//
//  Linkage
//       : Either the MsgcoreUtils project or its stdafx.h MUST define
//         MsgcoreUtils_EXPORTS when BUILDING the DLL; a consumer defines
//         nothing and gets dllimport. MsgcoreUtils_STATIC selects the archive
//         form (the DebugLib/ReleaseLib configurations), where the symbols are
//         neither imported nor exported. This mirrors Msgcore.h exactly - and
//         it has to, because a consumer that links the static Msgcore must also
//         link the static MsgcoreUtils or the two disagree about who owns the
//         heap. See LINKAGE.md.
//       : Spelled through P2P_EXPORT / P2P_IMPORT (Platform/p2pexport.h) rather
//         than __declspec directly, so the header is compilable on the Linux
//         port for the same reason Msgcore's own headers are.
//
#pragma once

#include "../Msgcore/Msgcore.h"                 // Msgcore_EXT, the P2Pmsg scalar model
#include "../Msgcore/Platform/p2pexport.h"      // P2P_EXPORT / P2P_IMPORT

#if defined(MsgcoreUtils_STATIC)
  #define MsgcoreUtils_EXT
  #define MsgcoreUtils_API
#elif defined(MsgcoreUtils_EXPORTS)
  #define MsgcoreUtils_EXT P2P_EXPORT
  #define MsgcoreUtils_API P2P_EXPORT
#else
  #define MsgcoreUtils_EXT P2P_IMPORT
  #define MsgcoreUtils_API P2P_IMPORT
#endif

//  Version identity is Msgcore's, read from Msgcore_version.h, and this
//  component does not mint a second one. The two ship from one repository
//  under one tag, so a MsgcoreUtils.dll reporting a version no Msgcore.dll
//  matches would be describing a pairing that cannot occur.
#include "../Msgcore/Msgcore_version.h"

//  THIS HEADER PULLS IN NO RENDERER, and that is not an omission. Every
//  renderer header includes this one for MsgcoreUtils_EXT, so an umbrella
//  include here would close a cycle: MsgPrint.h -> MsgcoreUtils.h ->
//  PrintJson.h, which #pragma once resolves by SKIPPING the MsgPrint.h that is
//  still part-way through being parsed - leaving PrintJson to derive from a
//  class the compiler has not seen. Include the renderer you want:
//
//      #include "MsgcoreUtils/PrintJson.h"
//      #include "MsgcoreUtils/PrintPHP.h"
//
//  Each is self-contained and each brings MsgPrint with it.
