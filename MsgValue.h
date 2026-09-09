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
//  MsgValue - the dialect-neutral value tree every MsgcoreUtils parser builds.
//
//  NOTES: This is the parse-side counterpart of MsgPrint's split, and it falls
//         the OTHER WAY. On the render side the traversal cannot be shared,
//         because a renderer has to punctuate and the two dialects punctuate
//         differently. On the parse side the punctuation is gone by the time
//         there is a tree - so from here to the store there is one walk, and
//         both dialects get it. JSON and PHP therefore differ ONLY in the
//         reader that fills this, which is the whole of what makes them
//         different documents.
//       : Six kinds - MsgPrint::ValueKind_e, which is what a scalar leaf
//         carries, plus the two containers a document can nest. A reader
//         hands back the kind it read; nothing here re-derives it from the
//         text, because "42" as a JSON number and "42" as a JSON string are
//         different documents and only the reader knows which it saw.
//       : AN OBJECT KEEPS ITS MEMBERS IN DOCUMENT ORDER AND DOES NOT INDEX
//         THEM. Order is what the store preserves for descendants, and a
//         parent holding two descendants of one name is something the store
//         can express - so de-duplicating here would silently drop data the
//         format allows. Find() exists for the sentinel keys, which are the
//         only ones looked up rather than walked, and it returns the FIRST.
//       : NOT COPYABLE, deliberately. Children are owned by raw pointer so a
//         reference handed out by Add() stays valid while the reader descends
//         into it and adds siblings behind it - which a std::vector<MsgValue>
//         would invalidate on its next growth. Nothing needs to copy a tree,
//         so the deep copy that would make it safe is not written.
//
#pragma once

#include <vector>                       // ahead of the Msgcore headers - see MsgPrint.h

#include "MsgPrint.h"

class MsgValue
{
    // Constructors and destructor
    public:
        MsgValue ( ) noexcept;

        explicit MsgValue ( MsgPrint::ValueKind_e eKind ) noexcept;

       ~MsgValue ( );

        MsgValue ( const MsgValue& rhs )            = delete;
      MsgValue&
        operator = ( const MsgValue& rhs )          = delete;

    // Kind
    public:
      MsgPrint::ValueKind_e
        GetKind ( ) const noexcept;
      void
        SetKind ( MsgPrint::ValueKind_e eKind ) noexcept;
      //  Sets kind and text together, which is what a reader has when it
      //  finishes a scalar and the only way the two can disagree.
      void
        SetScalar ( MsgPrint::ValueKind_e eKind, LPCTSTR lpszText );
      bool
        IsScalar ( ) const noexcept;
      bool
        IsArray ( ) const noexcept;
      bool
        IsObject ( ) const noexcept;

    // Scalar text
    //  NOTES: UNESCAPED and undecorated - the string without its quotes, the
    //         number as it was written. The dialect's escapes are the
    //         reader's problem and are already gone.
    public:
      LPCTSTR
        c_text ( ) const noexcept;
      const CString&
        r_text ( ) const noexcept;

    // Type hint
    //  NOTES: Msgcore's own name for the type the cell HAD - "int32", "WSTR16"
    //         - where the dialect carried one, and empty where it did not. Only
    //         PHP has one to give: its render writes the name into the docblock
    //         above each property, so a PHP round trip restores the exact type
    //         byte where a JSON one can only infer a width from the digits.
    //       : A HINT and not an instruction. ParseScalar falls back to the
    //         inferred type when the text does not actually parse as the named
    //         one, because the comment it came from is the one part of a
    //         generated file a person can edit without the code noticing.
    public:
      LPCTSTR
        c_type ( ) const noexcept;
      void
        SetType ( LPCTSTR lpszTypeName );

    // Children
    public:
      size_t
        GetCount ( ) const noexcept;
      const MsgValue&
        r_child ( size_t nIndex ) const;
      LPCTSTR
        c_key ( size_t nIndex ) const;
      //  Appends a child and returns it. lpszKey is the member name for an
      //  object and is ignored for an array, whose elements have no names.
      MsgValue&
        Add ( LPCTSTR lpszKey = nullptr );
      //  The FIRST child under lpszKey, or nullptr. For the sentinel keys.
      const MsgValue*
        Find ( LPCTSTR lpszKey ) const;
      //  Writable, for a member whose value is only known later - the PHP
      //  reader adds a child object before it has read the constructor that
      //  says which class fills it.
      MsgValue*
        p_child ( size_t nIndex );
      //  MOVES every child of oFrom in here, prefixing each key. Moves rather
      //  than copies because a MsgValue owns its children by pointer and does
      //  not copy at all; oFrom is left empty. For the one place a dialect has
      //  to re-key a subtree it has already read - PHP's class-level
      //  $_attributes, whose keys are bare where every other attribute key
      //  carries the delimiter.
      void
        AdoptChildren ( MsgValue& oFrom, LPCTSTR lpszKeyPrefix );

    // Vector hint
    //  NOTES: An array says nothing about whether the store held a list or a
    //         vector, so the writer decides from the contents - and gets it
    //         wrong for a vector of plain scalars, which comes back a list.
    //         The PHP render is the one dialect that says outright which it
    //         was, in the docblock over $_items, so it sets this and the
    //         guess is not needed. Set on the node that HOLDS the elements.
    public:
      void
        SetVectorHint ( bool bVector ) noexcept;
      bool
        GetVectorHint ( ) const noexcept;

    // Attributes
    private:
      MsgPrint::ValueKind_e   m_eKind{MsgPrint::Value_Null};
      CString                 m_strText;
      CString                 m_strType;
      bool                    m_bVectorHint{false};
      std::vector<CString>    m_oKeys;
      std::vector<MsgValue *> m_oChildren;
};
