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
//  MsgPrint - the accumulated-text base shared by every MsgcoreUtils renderer,
//             and the store writer shared by every parser.
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
//  BOTH DIRECTIONS, and the >> points the way the data moves
//
//             oMgr >> oJson;   oJson << oMgr;    render - store into text
//             oMgr << oJson;   oJson >> oMgr;    parse  - text into store
//
//         Parsing reads the text this object HOLDS, so a document from
//         elsewhere is put there first with Load() or SetText(). It replaces
//         the target for the same reason a render replaces the buffer: half a
//         document written over a live node is worse than none.
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
//       : THE PARSE SIDE SPLITS THE OTHER WAY. A reader has thrown the
//         punctuation away by the time it has a MsgValue, so from a tree to a
//         store there is ONE walk - BuildItem, here - and both dialects get
//         it. What is not shared there is only the reader that fills the tree.
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
#include <vector>

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

//  The tree both parsers build, and the only thing the store writer below
//  reads. Declared and not defined here: MsgValue.h is internal to this
//  library, and a consumer that only renders should not have to parse it.
class MsgValue;

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

    // Parsing
    //  NOTES: The other direction: reads the text this object HOLDS and writes
    //         it into oItem, REPLACING everything oItem held. Symmetrical with
    //         Render in both senses - it replaces, and it takes any P3PmsgItem
    //         rather than only a manager.
    //       : The text is whatever the object holds, so a document that came
    //         from somewhere else has to be put there first - by the LPCTSTR
    //         constructor each parsing dialect carries, with Load() from a
    //         file, or SetText() from memory. Parsing straight after a Render
    //         is a round trip and nothing more.
    //       : NOTHING IS WRITTEN UNTIL THE WHOLE DOCUMENT HAS PARSED. A parse
    //         failure leaves oItem exactly as it was, because the alternative -
    //         a truncated node and half a document in it - destroys the data
    //         the caller still had. Ask GetError() whether it worked.
    public:
      virtual MsgPrint&
        Parse ( P3PmsgItem& oItem ) = 0;

    // Parse status
    //  NOTES: Empty means the last Parse succeeded. Render has no equivalent
    //         because a render cannot be given something it cannot read - it
    //         is handed an object, not text.
    public:
      LPCTSTR
        GetError ( ) const noexcept;
      //  Character offset into the text where the parse stopped, or -1.
      int
        GetErrorPos ( ) const noexcept;
      //  1-based line number for the same position, or 0. Kept beside the
      //  offset because an offset into a 40KB document tells a reader nothing.
      int
        GetErrorLine ( ) const noexcept;

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
      //  Replaces the held text, for a document that is about to be Parse()d
      //  rather than one that was rendered.
      void
        SetText ( LPCTSTR lpszText );

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

    // Input
    public:
      //  Reads a file into the held text as UTF-8, DISCARDING a leading BOM if
      //  the file carries one - which is the exact inverse of Save, and the
      //  reason the flag Save takes has no counterpart here: a BOM is
      //  recognised and dropped whether or not the caller expected one.
      virtual BOOL
        Load ( LPCTSTR lpszFilename );

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

    // The kinds a document can carry
    //  NOTES: The kind is what the dialect punctuates on. Value_String covers
    //         every stored form that reaches the document AS TEXT - strings,
    //         wide characters, times, GUIDs and the base64 of a blob - because
    //         a dialect quotes all five identically, and separating them would
    //         only give a caller five ways to make one decision.
    //       : The last two are not scalars and RenderScalar never returns one.
    //         They are here rather than in a second enum because MsgValue - the
    //         tree both PARSERS build - needs exactly these four plus those
    //         two, and a scalar leaf in that tree carries the kind the renderer
    //         would have stamped on it. One enum is what keeps the two
    //         directions describing the same six things.
    public:
      typedef enum
      {
        Value_Null   = 0,                // Untyped, or flagged NULL
        Value_Bool   = 1,                // true / false - both dialects agree
        Value_Number = 2,                // Emitted bare, never quoted
        Value_String = 3,                // Emitted quoted, escaped by the dialect
        Value_Array  = 4,                // A container of unnamed elements
        Value_Object = 5,                // A container of named members
      } ValueKind_e;

    // Scalar rendering
    protected:
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

      //  The inverse of TypeName: the VBLockData type byte a name stands for,
      //  or VBLockData_NULL for one this build does not place. Only the PHP
      //  dialect has anything to feed it - JSON carries no type names - but it
      //  belongs beside TypeName rather than in PrintPHP, because a table and
      //  its inverse drifting apart in two files is exactly how a round trip
      //  starts returning the wrong type.
      static UCHAR
        TypeByte ( LPCTSTR lpszTypeName );

    // Store writing
    //  NOTES: The parse-side counterpart of the render-side traversal, and
    //         SHARED where that one is not. Both dialects parse into a MsgValue
    //         and stop there; everything from a tree to a store is written once
    //         and both of them get it. See MsgValue.h.
    protected:
      //  Writes oValue into oItem, REPLACING oItem's value, attributes and
      //  descendants. Throws a CString on a shape the store cannot hold, which
      //  is what Parse turns into GetError().
      void
        BuildItem ( P3PmsgItem& oItem, const MsgValue& oValue, int nDepth );

      //  The scalar inverse of RenderScalar: the P3PmsgData a scalar MsgValue
      //  describes. A value carrying a TYPE NAME - which only the PHP dialect
      //  has one to carry - is built as that exact VBLockData byte, and that is
      //  what makes the PHP round trip type-exact; one without is inferred from
      //  the kind, which is all JSON can offer.
      static P3PmsgData
        ParseScalar ( const MsgValue& oValue );

      static bool
        Base64Decode ( LPCTSTR lpszText, std::vector<unsigned char>& oBytes );

      //  Records the FIRST failure of a parse and ignores every one after it -
      //  see the definition for why last would be the wrong one to keep.
      void
        SetError ( LPCTSTR lpszError, int nPos );
      void
        ClearError ( ) noexcept;

    // Store writing - implementation
    private:
      typedef enum
      {
        Node_Item = 0,                 // A field, whatever else it carries
        Node_List = 1,                 // An array of nothing but scalars
        Node_Vect = 2,                 // An array with a container in it
      } NodeKind_e;

      //  Which of the three shapes oValue describes.
      NodeKind_e
        Classify ( const MsgValue& oValue ) const;
      //  Everything a node carries EXCEPT the elements of a list or vector.
      void
        FillNode ( P3PmsgItem& oNode, const MsgValue& oValue, int nDepth );
      void
        FillList ( P3PmsgList& oList, const MsgValue& oValue, int nDepth );
      void
        FillVect ( P3PmsgVect& oVect, const MsgValue& oValue, int nDepth );
      //  Builds oValue DETACHED and hands the whole subtree to the container in
      //  one operation. Three sinks, and only where each one puts the result
      //  differs; a vector's elements have no names, which is the third
      //  signature.
      void
        Attach ( P3PmsgDesc& oDesc, LPCTNAM lpszName, const MsgValue& oValue, int nDepth );
      void
        Attach ( P3PmsgAttr& oAttr, LPCTNAM lpszName, const MsgValue& oValue, int nDepth );
      void
        Attach ( P3PmsgVect& oVect, const MsgValue& oValue, int nDepth );
      //  The array an elements-carrying node holds - oValue itself when it is a
      //  bare array, its .items member when it is the object form.
      static const MsgValue*
        ItemsOf ( const MsgValue& oValue );

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
      //  Last parse outcome. Empty text is success, and is what a fresh object
      //  reports - a renderer that has never parsed has not failed to.
      CString  m_strError;
      int      m_nErrorPos{-1};
      int      m_nErrorLine{0};
};
