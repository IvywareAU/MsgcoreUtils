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
//  PrintHTML - a Msgcore store as a page someone can read.
//
//      P2PmsgMgr oMgr ( L"person.p2p" );
//      PrintHTML oHTML;
//      oMgr >> oHTML;
//      oHTML.Save ( L"person.html" );
//
//  A VIEW, NOT A FORMAT, and that is the whole of what separates this class
//  from the other three. JSON, XML and PHP each describe the store to another
//  program; this one describes it to a person. Everything below follows from
//  that one difference:
//
//    * IT IS ONE-DIRECTIONAL. There are no parsing operators, so oMgr << oHTML
//      does not compile - which is where that mistake is worth finding. Parse()
//      exists because MsgPrint declares it, and reports the refusal through
//      GetError() for a caller holding a MsgPrint& that cannot know which
//      dialect it has. A page is a rendering of a store the way a printed table
//      is a rendering of a spreadsheet: complete enough to read, and not the
//      thing itself. Use PrintXML or PrintJson for the return direction.
//    * IT IS SELF-CONTAINED. One file, with the stylesheet inside it and no
//      script at all. It opens from a filesystem, survives being mailed, and
//      needs nothing served alongside it. The collapsing is <details> and
//      <summary>, which is why there is no JavaScript to be missing.
//    * IT PRESENTS RATHER THAN PRESERVES. A blob is shown as base64 because
//      that is what it is; a null is shown as the word null in a colour that
//      says it is not a string. Nothing is truncated and nothing is rounded -
//      a view that quietly shortened a value would be worse than no view.
//
//  THE PAGE. A header naming the store and counting what is in it, then the
//  tree: one row per plain field, and a collapsible <details> per node that
//  carries anything. Attributes are rows like any other, marked with the @
//  their names carry everywhere else in this library, and the entries of a
//  list or vector are rows numbered by position.
//
//  Values are coloured by KIND rather than by type - strings, numbers, bools
//  and nulls are four different things to a reader, and int32 against int64 is
//  not. The type name is there for anyone who wants it, in a badge at the end
//  of the row, and SetTypes(false) puts it away.
//
//  THE STYLESHEET follows the reader's system theme. There is no toggle,
//  because a toggle needs script and this page has none, and no fixed choice,
//  because a white page in a dark room is a decision no renderer should make
//  on someone's behalf.
//
#pragma once

#include "MsgPrint.h"

class MsgcoreUtils_EXT PrintHTML : public MsgPrint
{
    // Constructors and destructor
    public:
        PrintHTML ( ) noexcept;

        PrintHTML ( const PrintHTML& rhs );

      virtual
       ~PrintHTML ( );

    // Operators
    public:
      PrintHTML&
        operator = ( const PrintHTML& rhs );

    // Rendering
    public:
      virtual MsgPrint&
        Render ( P3PmsgItem& oItem );

    // Parsing
    //  NOTES: THERE IS NO HTML PARSE. This reports that through GetError() and
    //         leaves oItem untouched - which is the same promise every other
    //         dialect's failed parse makes, and is the only thing a caller
    //         holding a MsgPrint& can be told. Reached by that route only: the
    //         parsing OPERATORS are not declared for this class, so the
    //         spelling a caller would reach for first is a compile error.
    public:
      virtual MsgPrint&
        Parse ( P3PmsgItem& oItem );

    // Properties
    public:
      //  Whether a whole page is written - <!DOCTYPE html> through </html> -
      //  or only the <main> element that carries the tree. Off is the form to
      //  embed in a page of your own; the stylesheet still comes with it
      //  unless SetStyle(false) says otherwise, because a fragment styled by
      //  nothing is a bulleted list.
      bool
        SetDocument ( bool bDocument ) noexcept;
      bool
        GetDocument ( ) const noexcept;

      //  Whether the stylesheet is embedded. Off leaves the markup and its
      //  class names, for a host page that has its own rules for them - see
      //  the class names in the header comment of the .cpp, which are stable.
      bool
        SetStyle ( bool bStyle ) noexcept;
      bool
        GetStyle ( ) const noexcept;

      //  The <title> and the heading. Empty - the default - takes the name of
      //  the node being rendered, which for a manager is the store's own root
      //  name and is what makes the page identify itself.
      void
        SetTitle ( LPCTSTR lpszTitle );
      LPCTSTR
        GetTitle ( ) const noexcept;

      //  Whether the type badge is shown on each value. On by default.
      bool
        SetTypes ( bool bTypes ) noexcept;
      bool
        GetTypes ( ) const noexcept;

      //  Whether the collapsible nodes start closed. Open by default: a page
      //  that has to be unfolded before it says anything is a worse first
      //  impression than a long one.
      bool
        SetCollapsed ( bool bCollapsed ) noexcept;
      bool
        GetCollapsed ( ) const noexcept;

    // Implementation
    private:
      //  One <li>. lpszName is the node's name; lpszIndex numbers an unnamed
      //  entry of a list or vector; bAttr marks it as one of its parent's
      //  attributes. Exactly one of the first two is used.
      void
        RenderNode ( P3PmsgItem& oItem, LPCTSTR lpszName, LPCTSTR lpszIndex,
                     bool bAttr, int nDepth );
      void
        RenderList ( P3PmsgList& oList, int nDepth );
      void
        RenderVect ( P3PmsgVect& oVect, int nDepth );
      void
        RenderChildren ( P3PmsgItem& oItem, int nDepth );
      //  The <li> of a node with no children - a key, a value and a badge.
      void
        RenderLeaf ( P3PmsgData& oData, const CString& strKey, bool bAttr,
                     int nDepth );
      //  The value span, coloured by kind. Returns the kind it rendered, which
      //  is what decides whether a badge follows: a null cell still HAS a type
      //  byte, and "null null" on a row says the second word twice.
      ValueKind_e
        RenderValue ( P3PmsgData& oData );
      void
        RenderBadge ( P3PmsgData& oData );
      //  "3 fields · 1 attribute", the line under a <summary> that says what
      //  is inside without opening it.
      CString
        Summarise ( P3PmsgItem& oItem ) const;

      //  The document around the tree, assembled once the tree is rendered and
      //  the tallies are therefore known.
      void
        RenderHead ( const CString& strTitle );
      void
        RenderFoot ( );

      static CString
        Escape ( LPCTSTR lpszText );
      //  "1 field" / "2 fields", because "1 fields" in a generated page is the
      //  detail that makes a reader distrust the rest of it.
      static CString
        Plural ( int nCount, LPCTSTR lpszOne, LPCTSTR lpszMany );

    // Attributes
    private:
      bool     m_bDocument{true};
      bool     m_bStyle{true};
      bool     m_bTypes{true};
      bool     m_bCollapsed{false};
      CString  m_strTitle;
      //  What the last render found, for the header's count line. Reset by
      //  Render and true of the text this object holds, which is why the copy
      //  constructor carries them: they describe that text.
      int      m_nNodes{0};
      int      m_nAttrs{0};
      int      m_nArrays{0};
};

///////////////////////////////////////////////////////////////////////
//  Rendering operators
//  NOTES: Declared on P3PmsgItem rather than on P2PmsgMgr, so a manager binds
//         to them directly and so does any subtree, list or vector inside one.
//       : THERE ARE NO PARSING COUNTERPARTS, and their absence is the design
//         rather than an omission - see the class comment. oMgr << oHTML and
//         oHTML >> oMgr do not compile, which is the earliest and cheapest
//         place to discover that a page is not a source.
MsgcoreUtils_EXT PrintHTML&
operator >> ( P3PmsgItem& oItem, PrintHTML& oHTML );

MsgcoreUtils_EXT PrintHTML&
operator << ( PrintHTML& oHTML, P3PmsgItem& oItem );
