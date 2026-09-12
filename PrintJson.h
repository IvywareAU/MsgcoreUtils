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
//  PrintJson - a Msgcore store as a JSON document.
//
//      P2PmsgMgr oMgr ( L"person.p2p" );
//      PrintJson oJson;
//      oMgr >> oJson;
//      _tprintf ( _T("%s"), oJson.c_str() );
//
//  THE MAPPING, and why each half of it is what it is.
//
//  A P3PmsgItem is not a JSON value. It is a name, a value, a set of
//  attributes and a set of descendants, ALL AT ONCE and any of them optional -
//  so the rendering has to decide, per node, whether the node is a JSON scalar
//  or a JSON object. The rule is:
//
//    * A plain field - no attributes, no descendants - renders as its VALUE.
//
//        "Surname": "Mann"
//
//    * A field that carries attributes or descendants renders as an OBJECT.
//      Its attributes appear one per member under their name prefixed with @;
//      its descendants appear one per member under their own names; its own
//      value, IF IT HAS ONE, appears under ".value". A pure container has no
//      ".value" member at all - writing null for every one of them would treble
//      the size of a deep document to say nothing.
//
//        "Address": { "@type": "postal", "Street": "Smith St" }
//
//    * A list or a vector renders as an ARRAY - or, if it also carries
//      attributes or descendants, as an object whose ".items" member is that
//      array. A list holds data entries and nothing else; a vector holds
//      elements that may themselves be items, lists or vectors, and those
//      recurse through the same rule.
//
//  THE THREE KEY SPACES CANNOT COLLIDE, and that is a property of the message
//  format rather than of this renderer. P3Pmsg_IsValidItemname rejects both
//  T_DescDelim and T_AttrDelim in an item name, so a descendant can never be
//  called .value or .items and can never be spelled the way an attribute is.
//  A JSON object emitted here therefore has no duplicate keys unless the store
//  itself holds two descendants of one parent under one name.
//
//  VALUES follow MsgPrint::RenderScalar - integers and reals bare, strings
//  quoted and escaped, bool as true/false, NULL as null. Two cases are worth
//  naming here because JSON has no form for either:
//
//    * A BLOB becomes a base64 string. JSON has no binary literal, base64 is
//      what every JSON consumer already has a decoder for, and the alternative
//      - a length and an ellipsis, which is what the debug Print emits - is not
//      something a reader can turn back into bytes.
//    * A TIME becomes an ISO-8601 local-time string, not a number. The stored
//      value is seconds and would have rendered as a bare integer, which is
//      indistinguishable from a count.
//
//  NON-FINITE DOUBLES become null. JSON has no NaN and no Infinity - emitting
//  either produces a document that a strict parser rejects outright, so the
//  cell is reported as empty instead. Msgcore_NULL_DBLE, the library's own
//  pseudo-null double, is treated the same way for the same reason.
//
#pragma once

#include "MsgPrint.h"

class MsgcoreUtils_EXT PrintJson : public MsgPrint
{
    // Constructors and destructor
    public:
        PrintJson ( ) noexcept;

        //  Seeds the renderer with a DOCUMENT, exactly as SetText does, so a
        //  parse needs no second statement:
        //
        //      PrintJson oJson ( LR"({ "Settings": { "x": 1240 } })" );
        //      oMgr << oJson;
        //
        //  TEXT, NOT A FILENAME, and explicit for that reason. Load() takes the
        //  filename, and both take an LPCTSTR, so an implicit conversion here
        //  would silently accept a path where a document belongs and parse the
        //  path as JSON. Explicit costs the spelling above nothing - direct
        //  initialisation is what the caller writes anyway - and it closes the
        //  one confusion the two signatures can create between them.
        //
        //  Not noexcept, unlike the default constructor: holding the text is an
        //  allocation.
      explicit
        PrintJson ( LPCTSTR lpszText );

        PrintJson ( const PrintJson& rhs );

      virtual
       ~PrintJson ( );

    // Operators
    public:
      PrintJson&
        operator = ( const PrintJson& rhs );

    // Rendering
    public:
      virtual MsgPrint&
        Render ( P3PmsgItem& oItem );

    // Parsing
    //  NOTES: Reads the JSON this renderer HOLDS into oItem, replacing what
    //         oItem held. The text comes from a Render, from the constructor
    //         above, from Load() or from SetText(); nothing else puts any
    //         there.
    //       : GetRooted() has to say what the document actually is - see the
    //         definition, and SetRooted below.
    public:
      virtual MsgPrint&
        Parse ( P3PmsgItem& oItem );

    // Properties
    public:
      //  Whether the document is wrapped in an outer object keyed by the root
      //  item name, which is what makes a rendered manager self-identifying:
      //
      //      { "Person": { "Surname": "Mann" } }      bRooted (the default)
      //      { "Surname": "Mann" }                    !bRooted
      //
      //  Off is the form to use when the document is about to be assigned to a
      //  named member of something larger, where the name would appear twice.
      //
      //  IT MEANS THE SAME THING TO Parse, which believes it rather than
      //  sniffing the document: a rooted parse consumes the outer object and
      //  writes what is inside it. Guessing instead would unwrap
      //  { "Surname": "Mann" } - a legitimate unrooted document that happens to
      //  have one member - into the string "Mann".
      bool
        SetRooted ( bool bRooted ) noexcept;
      bool
        GetRooted ( ) const noexcept;

    // Implementation
    private:
      //  Emits the VALUE of a node - a scalar, an array or an object - with no
      //  key in front of it. The key, where there is one, is the caller's.
      void
        RenderNode ( P3PmsgItem& oItem, int nDepth );
      void
        RenderScalarNode ( P3PmsgData& oData );
      void
        RenderList ( P3PmsgList& oList, int nDepth );
      void
        RenderVect ( P3PmsgVect& oVect, int nDepth );
      //  Members of the object form. Each returns having updated bFirst, which
      //  is what decides whether a comma precedes the next member.
      void
        RenderAttrMembers ( P3PmsgAttr& oAttr, int nDepth, bool& bFirst );
      void
        RenderDescMembers ( P3PmsgDesc& oDesc, int nDepth, bool& bFirst );
      void
        RenderKey ( LPCTSTR lpszKey, int nDepth, bool& bFirst );

      static CString
        Escape ( LPCTSTR lpszText );

    // Reading
    //  NOTES: A recursive descent over the held text by character offset. Each
    //         one leaves nPos on the first character it did not consume and
    //         returns false having called SetError.
    private:
      bool
        ReadValue ( MsgValue& oValue, int& nPos, int nDepth );
      bool
        ReadObject ( MsgValue& oValue, int& nPos, int nDepth );
      bool
        ReadArray ( MsgValue& oValue, int& nPos, int nDepth );
      bool
        ReadString ( CString& strOut, int& nPos );
      bool
        ReadNumber ( MsgValue& oValue, int& nPos );
      bool
        ReadHex4 ( unsigned int& uCode, int& nPos );
      bool
        ReadLiteral ( LPCTSTR lpszWord, int& nPos );
      void
        SkipSpace ( int& nPos ) const;

    // Attributes
    private:
      bool     m_bRooted{true};
};

///////////////////////////////////////////////////////////////////////
//  Rendering operator
//  NOTES: Declared on P3PmsgItem rather than on P2PmsgMgr, and that ONE
//         declaration is what makes the documented usage work: P2PmsgMgr
//         derives from P3PmsgItem, so a manager binds to it directly, and so
//         does any subtree, list or vector inside one.
//       : Returns the renderer, so the result can be read straight out of the
//         expression - _tprintf ( L"%s", (oMgr >> oJson).c_str() ).
MsgcoreUtils_EXT PrintJson&
operator >> ( P3PmsgItem& oItem, PrintJson& oJson );

//  NOTES: The same render written the other way round - oJson << oMgr renders
//         oMgr into oJson, exactly as oMgr >> oJson does. Both spellings say
//         the store goes INTO the renderer; which one reads better depends on
//         which side the caller is thinking about, so both are here rather
//         than one of them being a mistake the compiler happens to reject.
//       : It REPLACES for the reason the class does, so it does NOT chain the
//         way a stream's << does: oJson << oItem1 << oItem2 compiles, because
//         this returns the renderer, and leaves oItem2 alone in it.
MsgcoreUtils_EXT PrintJson&
operator << ( PrintJson& oJson, P3PmsgItem& oItem );

///////////////////////////////////////////////////////////////////////
//  Parsing operators
//  NOTES: The same two spellings pointing the other way, and the arrow is the
//         whole of the difference - it points at the thing being written:
//
//             oMgr >> oJson;   oJson << oMgr;    render - store into text
//             oMgr << oJson;   oJson >> oMgr;    parse  - text into store
//
//       : ALL FOUR RETURN THE RENDERER, whichever side it is written on, which
//         is what makes the outcome reachable from the expression -
//         (oMgr << oJson).GetError(). Returning the store from the two parsing
//         forms would have hidden the one thing a parse has to be asked.
//       : Parsing reads the text the renderer HOLDS, so a document from
//         anywhere but a preceding Render has to be put there first with Load()
//         or SetText().
MsgcoreUtils_EXT PrintJson&
operator << ( P3PmsgItem& oItem, PrintJson& oJson );

MsgcoreUtils_EXT PrintJson&
operator >> ( PrintJson& oJson, P3PmsgItem& oItem );
