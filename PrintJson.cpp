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
//  PrintJson - JSON projection of a P3PmsgItem tree. See PrintJson.h for the
//  mapping and for why each half of it is what it is.
//
#include "stdafx.h"

#include "PrintJson.h"

#include "../Msgcore/Msgexception.h"

///////////////////////////////////////////////////////////////////////
//  Constructors and destructor

PrintJson::PrintJson ( ) noexcept
{
}

PrintJson::PrintJson ( const PrintJson& rhs )
        : MsgPrint ( rhs )
{
    m_bRooted = rhs.m_bRooted;
}

PrintJson::~PrintJson ( )
{
}

///////////////////////////////////////////////////////////////////////
//  Operators

PrintJson&
PrintJson::operator = ( const PrintJson& rhs )
{
    if ( this == &rhs )
      return *this;
    MsgPrint::operator = ( rhs );
    m_bRooted = rhs.m_bRooted;
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Rendering

//
//  Renders oItem and everything beneath it as a JSON document, REPLACING
//  whatever this renderer held.
//
//  Parameters:  P3PmsgItem& oItem
//               Manager, item, list or vector to render
//
//  Returns:     MsgPrint&
//               This renderer, so the text can be read from the expression
//
MsgPrint&
PrintJson::Render ( P3PmsgItem& oItem )
{
    m_strText.Empty ( );
    try
    {
      if ( m_bRooted )
      {
        //  The outer object exists so the document names the thing it came
        //  from. Without it a rendered manager is a bare object and the store
        //  root name - which is the only name the manager has - is nowhere in
        //  the output.
        bool bFirst = true;
        Append   ( _T("{") );
        RenderKey ( oItem.c_name(), 1, bFirst );
        RenderNode ( oItem, 1 );
        AppendBreak ( 0 );
        Append ( _T("}") );
      }
      else
        RenderNode ( oItem, 0 );
      return *this;
    }
    //  A throw from the traversal - not from a cell, which RenderScalar absorbs
    //  by itself - leaves a half-built document, and half a JSON document is
    //  worse than none: it parses as far as the truncation and then reports an
    //  error that names the wrong place. Empty it instead, so IsEmpty() is the
    //  answer to "did this work".
    catch_pP2Pevent_Cancel
    catch_ALL_Cancel
    m_strText.Empty ( );
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Properties

bool
PrintJson::SetRooted ( bool bRooted ) noexcept
{
    const bool bPrevious = m_bRooted;
    m_bRooted = bRooted;
    return bPrevious;
}

bool
PrintJson::GetRooted ( ) const noexcept
{
    return m_bRooted;
}

///////////////////////////////////////////////////////////////////////
//  Implementation

//
//  Emits the VALUE of one node - a scalar, an array or an object - with no key
//  in front of it.
//
void
PrintJson::RenderNode ( P3PmsgItem& oItem, int nDepth )
{
    if ( nDepth > m_nDepthMax )
    {
      //  The ceiling, not a structural case. A tree cannot reach it; a store
      //  whose links were corrupted can, and an unbounded descent on one is a
      //  stack overflow rather than a diagnostic.
      Append ( _T("null") );
      return;
    }

    const bool bList = oItem.r_Object().IsList ( );
    const bool bVect = oItem.r_Object().IsVect ( );
    const bool bDesc = !oItem.r_Desc().IsEmpty ( );
    const bool bAttr = m_bAttributes && !oItem.r_Attr().IsEmpty ( );

    //  The two plain forms, which is what the great majority of nodes are.
    if ( !bDesc && !bAttr )
    {
      if ( bList )
      {
        RenderList ( dynamic_cast<P3PmsgList&>(oItem), nDepth );
        return;
      }
      if ( bVect )
      {
        RenderVect ( dynamic_cast<P3PmsgVect&>(oItem), nDepth );
        return;
      }
      RenderScalarNode ( oItem.r_data() );
      return;
    }

    //  The object form: everything the node carries, under keys that cannot
    //  collide (see MsgPrint.h).
    bool bFirst = true;
    Append ( _T("{") );

    if ( bList || bVect )
    {
      RenderKey ( MSGPRINT_KEY_ITEMS, nDepth + 1, bFirst );
      if ( bList )
        RenderList ( dynamic_cast<P3PmsgList&>(oItem), nDepth + 1 );
      else
        RenderVect ( dynamic_cast<P3PmsgVect&>(oItem), nDepth + 1 );
    }
    else
    {
      //  A node with attributes or descendants may still hold a value of its
      //  own. It is emitted only when it HAS one: an absent .value says the
      //  node is a pure container, which is the common case, and writing
      //  "null" for every one of them would treble the size of a deep document
      //  to say nothing.
      CString strValue;
      if ( RenderScalar ( oItem.r_data(), strValue ) != Value_Null )
      {
        RenderKey ( MSGPRINT_KEY_VALUE, nDepth + 1, bFirst );
        RenderScalarNode ( oItem.r_data() );
      }
    }

    if ( bAttr )
      RenderAttrMembers ( oItem.r_Attr(), nDepth + 1, bFirst );
    if ( bDesc )
      RenderDescMembers ( oItem.r_Desc(), nDepth + 1, bFirst );

    if ( !bFirst )
      AppendBreak ( nDepth );
    Append ( _T("}") );
}

void
PrintJson::RenderScalarNode ( P3PmsgData& oData )
{
    CString strValue;
    switch ( RenderScalar ( oData, strValue ) )
    {
      case Value_Bool:
      case Value_Number:
        Append ( (LPCTSTR)strValue );
        break;
      case Value_String:
        Append ( _T("\"") );
        Append ( (LPCTSTR)Escape ( (LPCTSTR)strValue ) );
        Append ( _T("\"") );
        break;
      case Value_Null:
      default:
        Append ( _T("null") );
        break;
    }
}

//
//  A list is a chain of data entries and nothing else, so it is always a JSON
//  array of scalars.
//
void
PrintJson::RenderList ( P3PmsgList& oList, int nDepth )
{
    Append ( _T("[") );
    bool    bFirst = true;
    VBLaddr aEntry = oList.GetHeadPos ( );
    while ( aEntry )
    {
      P3PmsgData& oEntry = oList.GetNext ( aEntry );
      if ( !bFirst )
        Append ( _T(",") );
      AppendBreak ( nDepth + 1 );
      bFirst = false;
      RenderScalarNode ( oEntry );
    }
    if ( !bFirst )
      AppendBreak ( nDepth );
    Append ( _T("]") );
}

//
//  A vector holds ELEMENTS, which may themselves be items, lists or vectors,
//  so each one goes back through RenderNode.
//  NOTES: r_item(i) walks the cursor to element i and the reference it returns
//         is invalidated by the next walk - which is why nothing here holds one
//         across an iteration.
//
void
PrintJson::RenderVect ( P3PmsgVect& oVect, int nDepth )
{
    Append ( _T("[") );
    const VBLelem nCount = oVect.GetCount ( );
    bool          bFirst = true;
    for ( VBLelem i = 0; i < nCount; i++ )
    {
      if ( !bFirst )
        Append ( _T(",") );
      AppendBreak ( nDepth + 1 );
      bFirst = false;
      try
      {
        RenderNode ( oVect.r_item ( (int)i ), nDepth + 1 );
      }
      //  An element the vector cannot instantiate is one element, not one
      //  document. Report it as null and carry on, the same call RenderScalar
      //  makes for a cell that disagrees with its own type byte.
      catch_pP2Pevent_Cancel
      catch_ALL_Cancel
    }
    if ( !bFirst )
      AppendBreak ( nDepth );
    Append ( _T("]") );
}

//
//  Attributes, one member each, under their name prefixed with the attribute
//  delimiter - which no item name may contain, so these keys are in a space of
//  their own.
//
void
PrintJson::RenderAttrMembers ( P3PmsgAttr& oAttr, int nDepth, bool& bFirst )
{
    P3PmsgCurs& oCurs = oAttr.r_Curs ( );
    for ( int i = 0; oCurs.Goto ( i ); i++ )
    {
      P3PmsgItem& oItem = oCurs.r_item ( );
      CString     strKey;
      strKey += MSGPRINT_KEY_ATTR;
      strKey += oItem.c_name ( );
      RenderKey  ( (LPCTSTR)strKey, nDepth, bFirst );
      RenderNode ( oItem, nDepth );
    }
}

//
//  Descendants, one member each, under their own names.
//
void
PrintJson::RenderDescMembers ( P3PmsgDesc& oDesc, int nDepth, bool& bFirst )
{
    P3PmsgCurs& oCurs = oDesc.r_Curs ( );
    for ( int i = 0; oCurs.Goto ( i ); i++ )
    {
      P3PmsgItem& oItem = oCurs.r_item ( );
      RenderKey  ( oItem.c_name(), nDepth, bFirst );
      RenderNode ( oItem, nDepth );
    }
}

void
PrintJson::RenderKey ( LPCTSTR lpszKey, int nDepth, bool& bFirst )
{
    if ( !bFirst )
      Append ( _T(",") );
    AppendBreak ( nDepth );
    bFirst = false;
    Append ( _T("\"") );
    Append ( (LPCTSTR)Escape ( lpszKey ) );
    Append ( _T("\":") );
    if ( m_nIndent > 0 )
      Append ( _T(" ") );
}

//
//  RFC 8259 string escaping. The two-character forms are used where they exist
//  because they are what a reader expects to see; everything else below U+0020
//  takes the \u form, which is the only escape JSON has for it.
//  NOTES: The solidus is NOT escaped. It is legal either way, and escaping it
//         is a convention from embedding JSON in HTML script elements, which is
//         not what this output is for.
//
CString
PrintJson::Escape ( LPCTSTR lpszText )
{
    CString strOut;
    if ( lpszText == nullptr )
      return strOut;

    for ( const TCHAR *p = lpszText; *p; ++p )
    {
      switch ( *p )
      {
        case _T('\"'): strOut += _T("\\\""); break;
        case _T('\\'): strOut += _T("\\\\"); break;
        case _T('\b'): strOut += _T("\\b");  break;
        case _T('\f'): strOut += _T("\\f");  break;
        case _T('\n'): strOut += _T("\\n");  break;
        case _T('\r'): strOut += _T("\\r");  break;
        case _T('\t'): strOut += _T("\\t");  break;
        default:
        {
          const unsigned int uCode = (unsigned int)*p;
          if ( uCode < 0x20u )
          {
            CString strEscape;
            strEscape.Format ( _T("\\u%04X"), uCode );
            strOut += strEscape;
          }
          else
            strOut += *p;
          break;
        }
      }
    }
    return strOut;
}

///////////////////////////////////////////////////////////////////////
//  Rendering operator

PrintJson&
operator >> ( P3PmsgItem& oItem, PrintJson& oJson )
{
    oJson.Render ( oItem );
    return oJson;
}
