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
//  MsgPrint - the accumulated-text base shared by every MsgcoreUtils renderer.
//
//  NOTES: A renderer is a SINK, not a stream. It holds one complete document,
//         it is rewritten from scratch by each render, and it is read back with
//         c_str(). That is what makes
//
//             P2PmsgMgr oMgr ( L"store.p2p" );
//             PrintJson oJson;
//             oMgr >> oJson;
//             LPCTSTR lpszDoc = oJson.c_str ( );
//
//         mean what it looks like it means. The >> REPLACES the content rather
//         than appending to it, because neither dialect has a concatenation
//         that stays valid: two JSON documents joined end to end are not a JSON
//         document, and two renders of the same store as PHP would declare
//         every class twice. Appending would have produced text that looked
//         right and would not parse.
//
//  What the renderers share, and what they do not
//       : Shared here - the text buffer, the layout options, and SCALAR
//         formatting. Scalar formatting is shared because it is the part that
//         reads the store: the VBLockData type byte selects the accessor, the
//         accessors throw on a mismatch, and getting that wrong once is enough.
//         Both dialects agree on the VALUE (42, the decoded string, the base64
//         of a blob) and disagree only on how to punctuate it, which is why
//         RenderScalar hands back a kind alongside the text.
//       : Not shared - the traversal. JSON nests objects; PHP hoists a class
//         per node and instantiates it from a constructor. Those are different
//         walks over the same tree, and a common walk with virtual hooks needs
//         a hook for every punctuation decision - which reads worse than the
//         two recursive descents it would replace.
//
//  What is rendered
//       : Name, value, ATTRIBUTES (@) and DESCENDANTS (.) - the whole of the
//         current state of a node.
//       : STACKS (^) are NOT rendered, in either dialect. A stack is the
//         superseded history of a field, not part of its value, and a document
//         that mixed the two would be describing several states of the store at
//         once. P3PmsgField::Print does print them, because a debug dump is
//         exactly where the history is wanted.
//
#pragma once

//  Before the Msgcore headers, deliberately. Every one of them opens with
//  #define new DEBUG_NEW, and a standard header parsed after that sees the
//  macro. Inside this project stdafx.h has already pulled the standard library
//  in, so the guards make it moot - but this header is also included by
//  consumers, who have made no such promise.
#include <stdio.h>

#include "MsgcoreUtils.h"

#include "../Msgcore/P2Pmsg.h"
#include "../Msgcore/MsgAttr.h"
#include "../Msgcore/MsgDesc.h"
#include "../Msgcore/MsgList.h"
#include "../Msgcore/MsgVect.h"
#include "../Msgcore/MsgCurs.h"

///////////////////////////////////////////////////////////////////////
//  Sentinel keys
//  NOTES: A rendered node needs keys of its own for the parts of a P3PmsgItem
//         that are not descendants - its own data, and the elements of a list
//         or vector. Those keys MUST be unable to collide with a real item
//         name, and the period is what guarantees it: P3Pmsg_IsValidItemname
//         rejects T_DescDelim outright, so no item can ever be named .value
//       : Attributes take the T_AttrDelim prefix for the same reason - it is
//         equally illegal in a name - which also means an attribute key can
//         never collide with a descendant key, and neither can collide with
//         the two below.
#define MSGPRINT_KEY_VALUE  _T(".value")
#define MSGPRINT_KEY_ITEMS  _T(".items")
#define MSGPRINT_KEY_ATTR   T_AttrDelim

class MsgcoreUtils_EXT MsgPrint
{
    // Constructors and destructor
    public:
        MsgPrint ( ) noexcept;

        MsgPrint ( const MsgPrint& rhs );

      virtual
       ~MsgPrint ( );

    // Operators
    public:
      MsgPrint&
        operator = ( const MsgPrint& rhs );

        operator LPCTSTR ( ) const noexcept;

    // Rendering
    //  NOTES: Renders oItem and everything beneath it, REPLACING whatever this
    //         renderer held. oItem may be a P2PmsgMgr (the whole store), or any
    //         P3PmsgItem, P3PmsgList or P3PmsgVect within one.
    public:
      virtual MsgPrint&
        Render ( P3PmsgItem& oItem ) = 0;

    // Text exposure
    public:
      LPCTSTR
        c_str ( ) const noexcept;
      const CString&
        r_text ( ) const noexcept;
      int
        GetLength ( ) const noexcept;
      bool
        IsEmpty ( ) const noexcept;
      void
        Clear ( ) noexcept;

    // Output
    public:
      //  Writes the rendered text to an already-open stream. Named and shaped
      //  after P3PmsgItem::Print so the two are reachable the same way.
      virtual void
        Print ( FILE *fd ) const;
      //  Writes the rendered text to a file as UTF-8. A BOM is written by
      //  default: PHP echoes one to the client if the file is included, so pass
      //  bBOM false for a .php file that will be served.
      virtual BOOL
        Save ( LPCTSTR lpszFilename, bool bBOM = true ) const;

    // Properties
    public:
      //  Spaces per nesting level. 0 renders the document on ONE line with no
      //  padding, which is what a wire payload wants and a reader does not.
      int
        SetIndent ( int nSpaces ) noexcept;
      int
        GetIndent ( ) const noexcept;
      //  Whether attributes are rendered at all. On by default: they carry the
      //  qualifications of a node, and dropping them silently loses data.
      bool
        SetAttributes ( bool bRender ) noexcept;
      bool
        GetAttributes ( ) const noexcept;
      //  Recursion ceiling. The object graph is a tree and cannot loop, so this
      //  never fires on a well-formed store; it is here because a renderer can
      //  be pointed at a store that was read off a wire, and an unbounded
      //  descent on a corrupt one is a stack overflow rather than a diagnostic.
      int
        SetDepthMax ( int nDepthMax ) noexcept;
      int
        GetDepthMax ( ) const noexcept;

    // Scalar rendering
    //  NOTES: The kind is what the dialect punctuates on. Value_String covers
    //         every stored form that reaches the document AS TEXT - strings,
    //         wide characters, times, GUIDs and the base64 of a blob - because
    //         a dialect quotes all five identically, and separating them would
    //         only give a caller five ways to make one decision.
    protected:
      typedef enum
      {
        Value_Null   = 0,                // Untyped, or flagged NULL
        Value_Bool   = 1,                // true / false - both dialects agree
        Value_Number = 2,                // Emitted bare, never quoted
        Value_String = 3,                // Emitted quoted, escaped by the dialect
      } ValueKind_e;

      //  Reads oData through the accessor its type byte selects and writes the
      //  UNESCAPED text into strOut. Never throws for a type it does not
      //  recognise: an unknown type byte renders as Value_Null, because a
      //  renderer that abandons a whole document over one cell is less useful
      //  than one that reports that cell as empty.
      ValueKind_e
        RenderScalar ( P3PmsgData& oData, CString& strOut ) const;

      //  Msgcore's own name for a type (int32, wstr16, ...), for the comments a
      //  dialect chooses to carry. Never fails.
      static CString
        TypeName ( P3PmsgData& oData );

    // Text assembly
    protected:
      void
        Append ( LPCTSTR lpszText );
      //  One newline plus nDepth levels of padding - and NOTHING AT ALL when
      //  the indent is 0, which is what collapses the document to one line.
      void
        AppendBreak ( int nDepth );

      static CString
        Base64 ( const unsigned char *pcBytes, size_t nBytes );

    // Attributes
    protected:
      CString  m_strText;
      int      m_nIndent{2};
      bool     m_bAttributes{true};
      int      m_nDepthMax{64};
};
