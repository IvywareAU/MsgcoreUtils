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
//  PrintXML - XML projection of a P3PmsgItem tree, and the reader that takes
//  one back. See PrintXML.h for the mapping and for why each half of it is
//  what it is.
//
#include "stdafx.h"

#include "PrintXML.h"
#include "MsgValue.h"

#include "../Msgcore/Msgexception.h"

///////////////////////////////////////////////////////////////////////
//  The reserved vocabulary
//  NOTES: All of it under one prefix, and the prefix is one no item name can
//         carry: P3Pmsg_IsValidItemname refuses the colon outright. That is
//         what puts these names in a space of their own without a single
//         collision test anywhere in the file.
//       : A URN rather than an http URL. A namespace name is an identifier and
//         nothing fetches it, and an http one invites a reader to try -
//         earning a 404 that says nothing about whether the document is valid.
#define XMLNS_P2P       _T("urn:ivyware:msgcore:p2p")
#define XML_PREFIX      _T("p2p:")
#define XML_TAG_ITEM    _T("p2p:item")
#define XML_TAG_VALUE   _T("p2p:value")
#define XML_ATTR_TYPE   _T("p2p:type")
#define XML_ATTR_NULL   _T("p2p:null")
#define XML_ATTR_NAME   _T("p2p:name")
#define XML_ATTR_ATTR   _T("p2p:attr")
#define XML_ATTR_LIST   _T("p2p:list")
#define XML_ATTR_VECT   _T("p2p:vect")

///////////////////////////////////////////////////////////////////////
//  File-local helpers

//
//  Whether strText is a number as JSON spells one.
//  NOTES: JSON's grammar rather than a looser one, and the leading-zero rule is
//         the reason to borrow it: "007" is a part number, a postcode or an
//         account, and every one of them is ruined by coming back as 7. XML has
//         no types of its own to settle this, so the question only arises with
//         SetTypes(false) - with types on, p2p:type has already answered it.
//
static bool
PrintXML_IsNumber ( const CString& strText )
{
    const int nLength = strText.GetLength ( );
    int       nPos    = 0;

    if ( nLength == 0 )
      return false;
    if ( strText[nPos] == _T('-') )
      nPos++;

    const int nIntFrom = nPos;
    while ( nPos < nLength && strText[nPos] >= _T('0') && strText[nPos] <= _T('9') )
      nPos++;
    if ( nPos == nIntFrom )
      return false;
    //  A leading zero may only stand alone.
    if ( strText[nIntFrom] == _T('0') && nPos - nIntFrom > 1 )
      return false;

    if ( nPos < nLength && strText[nPos] == _T('.') )
    {
      nPos++;
      const int nFracFrom = nPos;
      while ( nPos < nLength && strText[nPos] >= _T('0') && strText[nPos] <= _T('9') )
        nPos++;
      if ( nPos == nFracFrom )
        return false;
    }

    if ( nPos < nLength && ( strText[nPos] == _T('e') || strText[nPos] == _T('E') ) )
    {
      nPos++;
      if ( nPos < nLength && ( strText[nPos] == _T('+') || strText[nPos] == _T('-') ) )
        nPos++;
      const int nExpFrom = nPos;
      while ( nPos < nLength && strText[nPos] >= _T('0') && strText[nPos] <= _T('9') )
        nPos++;
      if ( nPos == nExpFrom )
        return false;
    }

    return nPos == nLength;
}

static bool
PrintXML_IsSpace ( TCHAR c )
{
    //  The four XML calls whitespace, and no others.
    return c == _T(' ') || c == _T('\t') || c == _T('\r') || c == _T('\n');
}

static bool
PrintXML_IsBlank ( const CString& strText )
{
    for ( int i = 0; i < strText.GetLength ( ); i++ )
      if ( !PrintXML_IsSpace ( strText[i] ) )
        return false;
    return true;
}

//
//  Fills a scalar MsgValue from the text of an element.
//  NOTES: The KIND is inferred from the text and the TYPE is carried alongside
//         it, because ParseScalar prefers the type and falls back to the kind -
//         so the same tree is right whether or not the document had types. The
//         one thing the kind must get right when there IS a type is that it is
//         not Value_Null, which is what would make ParseScalar ignore the type.
//
static void
PrintXML_SetScalar ( MsgValue& oValue, const CString& strText,
                     LPCTSTR lpszType, bool bNull )
{
    if ( bNull )
    {
      oValue.SetScalar ( MsgPrint::Value_Null, _T("") );
      return;
    }

    MsgPrint::ValueKind_e eKind = MsgPrint::Value_String;
    if ( strText.Compare ( _T("true") ) == 0 || strText.Compare ( _T("false") ) == 0 )
      eKind = MsgPrint::Value_Bool;
    else if ( PrintXML_IsNumber ( strText ) )
      eKind = MsgPrint::Value_Number;

    oValue.SetScalar ( eKind, (LPCTSTR)strText );
    if ( lpszType )
      oValue.SetType ( lpszType );
}

///////////////////////////////////////////////////////////////////////
//  Constructors and destructor

PrintXML::PrintXML ( ) noexcept
{
}

PrintXML::PrintXML ( const PrintXML& rhs )
        : MsgPrint ( rhs )
{
    m_bDeclaration = rhs.m_bDeclaration;
    m_bTypes       = rhs.m_bTypes;
}

PrintXML::~PrintXML ( )
{
}

///////////////////////////////////////////////////////////////////////
//  Operators

PrintXML&
PrintXML::operator = ( const PrintXML& rhs )
{
    if ( this == &rhs )
      return *this;
    MsgPrint::operator = ( rhs );
    m_bDeclaration = rhs.m_bDeclaration;
    m_bTypes       = rhs.m_bTypes;
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Rendering

//
//  Renders oItem and everything beneath it as an XML document, REPLACING
//  whatever this renderer held.
//
//  Parameters:  P3PmsgItem& oItem
//               Manager, item, list or vector to render
//
//  Returns:     MsgPrint&
//               This renderer, so the text can be read from the expression
//
MsgPrint&
PrintXML::Render ( P3PmsgItem& oItem )
{
    m_strText.Empty ( );
    try
    {
      if ( m_bDeclaration )
      {
        Append      ( _T("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") );
        AppendBreak ( 0 );
      }
      //  The root element names the node and carries the namespace
      //  declaration. There is no unrooted form to offer: an XML document IS
      //  one element, and one without a name is not a document.
      RenderNode ( oItem, oItem.c_name ( ), false, true, 0 );
      return *this;
    }
    //  A throw from the traversal - not from a cell, which RenderScalar absorbs
    //  by itself - leaves a half-built document, and half an XML document is
    //  worse than none: every unclosed element reports as an error at the end
    //  of the file rather than where the trouble was. Empty it instead, so
    //  IsEmpty() is the answer to "did this work".
    catch_pP2Pevent_Cancel
    catch_ALL_Cancel
    m_strText.Empty ( );
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Properties

bool
PrintXML::SetDeclaration ( bool bDeclaration ) noexcept
{
    const bool bPrevious = m_bDeclaration;
    m_bDeclaration = bDeclaration;
    return bPrevious;
}

bool
PrintXML::GetDeclaration ( ) const noexcept
{
    return m_bDeclaration;
}

bool
PrintXML::SetTypes ( bool bTypes ) noexcept
{
    const bool bPrevious = m_bTypes;
    m_bTypes = bTypes;
    return bPrevious;
}

bool
PrintXML::GetTypes ( ) const noexcept
{
    return m_bTypes;
}

///////////////////////////////////////////////////////////////////////
//  Implementation

//
//  Emits one complete element - its start tag, its content and its end tag.
//
//  Parameters:  P3PmsgItem& oItem
//               Node to render
//
//               LPCTSTR lpszName
//               Its Msgcore name, or nullptr for an unnamed entry of a vector
//
//               bool bAttr
//               Whether it is one of its parent's attributes
//
//               bool bRoot
//               Whether it carries the namespace declaration
//
//               int nDepth
//               Nesting level, for the indent
//
void
PrintXML::RenderNode ( P3PmsgItem& oItem, LPCTSTR lpszName, bool bAttr,
                       bool bRoot, int nDepth )
{
    const CString strTag = lpszName ? XmlName ( lpszName ) : CString ( XML_TAG_ITEM );

    //  The start tag, up to but not including its closing angle bracket -
    //  which of the three ways it closes is not known until the content is.
    CString strOpen ( _T("<") );
    strOpen += strTag;
    if ( bRoot )
    {
      strOpen += _T(" xmlns:p2p=\"");
      strOpen += XMLNS_P2P;
      strOpen += _T("\"");
    }
    //  Only when the two differ, which is the common case not happening: an
    //  ordinary name is its own tag and says so by the attribute's absence.
    if ( lpszName && strTag.Compare ( lpszName ) != 0 )
    {
      strOpen += _T(" ");
      strOpen += XML_ATTR_NAME;
      strOpen += _T("=\"");
      strOpen += EscapeAttr ( lpszName );
      strOpen += _T("\"");
    }
    if ( bAttr )
    {
      strOpen += _T(" ");
      strOpen += XML_ATTR_ATTR;
      strOpen += _T("=\"true\"");
    }

    if ( nDepth > m_nDepthMax )
    {
      //  The ceiling, not a structural case. A tree cannot reach it; a store
      //  whose links were corrupted can, and an unbounded descent on one is a
      //  stack overflow rather than a diagnostic.
      Append ( (LPCTSTR)strOpen );
      Append ( _T(" ") );
      Append ( XML_ATTR_NULL );
      Append ( _T("=\"true\"/>") );
      return;
    }

    const bool bList  = oItem.r_Object().IsList ( );
    const bool bVect  = oItem.r_Object().IsVect ( );
    const bool bDesc  = !oItem.r_Desc().IsEmpty ( );
    const bool bAttrs = m_bAttributes && !oItem.r_Attr().IsEmpty ( );

    if ( bList )
    {
      strOpen += _T(" ");
      strOpen += XML_ATTR_LIST;
      strOpen += _T("=\"true\"");
    }
    else if ( bVect )
    {
      strOpen += _T(" ");
      strOpen += XML_ATTR_VECT;
      strOpen += _T("=\"true\"");
    }

    //  A LEAF, which is what the great majority of nodes are: its value is its
    //  own text, and no p2p:value is needed to say so.
    if ( !bList && !bVect && !bDesc && !bAttrs )
    {
      CString strValue;
      if ( RenderScalar ( oItem.r_data(), strValue ) == Value_Null )
      {
        //  Empty is a present, empty string, so a NULL cell has to say more
        //  than nothing - otherwise the two come back the same.
        Append ( (LPCTSTR)strOpen );
        Append ( _T(" ") );
        Append ( XML_ATTR_NULL );
        Append ( _T("=\"true\"/>") );
        return;
      }
      Append ( (LPCTSTR)strOpen );
      Append ( (LPCTSTR)TypeAttr ( oItem.r_data() ) );
      Append ( _T(">") );
      Append ( (LPCTSTR)EscapeText ( (LPCTSTR)strValue ) );
      Append ( _T("</") );
      Append ( (LPCTSTR)strTag );
      Append ( _T(">") );
      return;
    }

    //  A node with children. Its own value, if it has one, goes in a
    //  <p2p:value> element rather than beside them: an element that holds both
    //  text and elements is mixed content, and a reader that has to work out
    //  which whitespace was meant is a reader that gets it wrong.
    Append ( (LPCTSTR)strOpen );
    Append ( _T(">") );

    const int nBefore = m_strText.GetLength ( );
    if ( bList )
      RenderList ( dynamic_cast<P3PmsgList&>(oItem), nDepth );
    else if ( bVect )
      RenderVect ( dynamic_cast<P3PmsgVect&>(oItem), nDepth );
    else
      RenderOwnValue ( oItem.r_data(), nDepth + 1 );

    if ( bAttrs )
      RenderAttrChildren ( oItem.r_Attr(), nDepth + 1 );
    if ( bDesc )
      RenderDescChildren ( oItem.r_Desc(), nDepth + 1 );

    //  The break before the end tag belongs to the children, so an element
    //  that emitted none stays on one line: <Flat p2p:list="true"></Flat>.
    if ( m_strText.GetLength ( ) != nBefore )
      AppendBreak ( nDepth );
    Append ( _T("</") );
    Append ( (LPCTSTR)strTag );
    Append ( _T(">") );
}

//
//  A list is a chain of data entries and nothing else, so every entry is a
//  <p2p:item> holding one scalar.
//
void
PrintXML::RenderList ( P3PmsgList& oList, int nDepth )
{
    VBLaddr aEntry = oList.GetHeadPos ( );
    while ( aEntry )
    {
      P3PmsgData& oEntry = oList.GetNext ( aEntry );
      AppendBreak ( nDepth + 1 );

      CString strValue;
      if ( RenderScalar ( oEntry, strValue ) == Value_Null )
      {
        Append ( _T("<") );
        Append ( XML_TAG_ITEM );
        Append ( _T(" ") );
        Append ( XML_ATTR_NULL );
        Append ( _T("=\"true\"/>") );
        continue;
      }
      Append ( _T("<") );
      Append ( XML_TAG_ITEM );
      Append ( (LPCTSTR)TypeAttr ( oEntry ) );
      Append ( _T(">") );
      Append ( (LPCTSTR)EscapeText ( (LPCTSTR)strValue ) );
      Append ( _T("</") );
      Append ( XML_TAG_ITEM );
      Append ( _T(">") );
    }
}

//
//  A vector holds ELEMENTS, which may themselves be items, lists or vectors,
//  so each one goes back through RenderNode under the unnamed tag.
//  NOTES: r_item(i) walks the cursor to element i and the reference it returns
//         is invalidated by the next walk - which is why nothing here holds one
//         across an iteration.
//
void
PrintXML::RenderVect ( P3PmsgVect& oVect, int nDepth )
{
    const VBLelem nCount = oVect.GetCount ( );
    for ( VBLelem i = 0; i < nCount; i++ )
    {
      AppendBreak ( nDepth + 1 );
      try
      {
        RenderNode ( oVect.r_item ( (int)i ), nullptr, false, false, nDepth + 1 );
      }
      //  An element the vector cannot instantiate is one element, not one
      //  document. Report it as null and carry on, the same call RenderScalar
      //  makes for a cell that disagrees with its own type byte.
      catch_pP2Pevent_Cancel
      catch_ALL_Cancel
    }
}

//
//  Attributes, one element each, marked p2p:attr so the reader puts them back
//  on the attribute branch rather than the descendant one.
//
void
PrintXML::RenderAttrChildren ( P3PmsgAttr& oAttr, int nDepth )
{
    P3PmsgCurs& oCurs = oAttr.r_Curs ( );
    for ( int i = 0; oCurs.Goto ( i ); i++ )
    {
      P3PmsgItem& oItem = oCurs.r_item ( );
      AppendBreak ( nDepth );
      RenderNode ( oItem, oItem.c_name ( ), true, false, nDepth );
    }
}

//
//  Descendants, one element each, under their own names.
//
void
PrintXML::RenderDescChildren ( P3PmsgDesc& oDesc, int nDepth )
{
    P3PmsgCurs& oCurs = oDesc.r_Curs ( );
    for ( int i = 0; oCurs.Goto ( i ); i++ )
    {
      P3PmsgItem& oItem = oCurs.r_item ( );
      AppendBreak ( nDepth );
      RenderNode ( oItem, oItem.c_name ( ), false, false, nDepth );
    }
}

//
//  The <p2p:value> of a node that carries children, and NOTHING AT ALL when it
//  has no value of its own - which is the common case for a pure container,
//  and writing an empty one for every one of them would say nothing at length.
//
void
PrintXML::RenderOwnValue ( P3PmsgData& oData, int nDepth )
{
    CString strValue;
    if ( RenderScalar ( oData, strValue ) == Value_Null )
      return;

    AppendBreak ( nDepth );
    Append ( _T("<") );
    Append ( XML_TAG_VALUE );
    Append ( (LPCTSTR)TypeAttr ( oData ) );
    Append ( _T(">") );
    Append ( (LPCTSTR)EscapeText ( (LPCTSTR)strValue ) );
    Append ( _T("</") );
    Append ( XML_TAG_VALUE );
    Append ( _T(">") );
}

CString
PrintXML::TypeAttr ( P3PmsgData& oData ) const
{
    CString strOut;
    if ( !m_bTypes )
      return strOut;

    const CString strType = TypeName ( oData );
    if ( strType.IsEmpty ( ) )
      return strOut;

    strOut += _T(" ");
    strOut += XML_ATTR_TYPE;
    strOut += _T("=\"");
    strOut += EscapeAttr ( (LPCTSTR)strType );
    strOut += _T("\"");
    return strOut;
}

//
//  Character data escaping.
//  NOTES: The three that must be escaped, plus the carriage return, which must
//         be escaped for a reason nothing about XML syntax suggests: a reader
//         NORMALISES line endings before the application sees them, turning a
//         raw CR or CRLF into a single LF. A store holding "\r\n" would come
//         back holding "\n", and &#13; is what survives.
//       : A control character below U+0020 other than tab, CR and LF is
//         DROPPED. XML 1.0 has no representation for one - not a raw character
//         and not a reference either - so the choice is between losing the
//         character and emitting a document no parser will read.
//       : The apostrophe is not escaped. It is legal as itself in character
//         data, and &apos; is the one predefined entity a reader is most
//         likely to be surprised by.
//
CString
PrintXML::EscapeText ( LPCTSTR lpszText )
{
    CString strOut;
    if ( lpszText == nullptr )
      return strOut;

    for ( const TCHAR *p = lpszText; *p; ++p )
    {
      switch ( *p )
      {
        case _T('&'):  strOut += _T("&amp;");  break;
        case _T('<'):  strOut += _T("&lt;");   break;
        case _T('>'):  strOut += _T("&gt;");   break;
        case _T('\r'): strOut += _T("&#13;");  break;
        default:
          if ( (unsigned int)*p < 0x20u && *p != _T('\t') && *p != _T('\n') )
            break;                     // Unrepresentable; dropped
          strOut += *p;
          break;
      }
    }
    return strOut;
}

//
//  Attribute-value escaping, which is stricter than character data for one
//  reason: a reader normalises every whitespace character inside an attribute
//  to a space, so a tab or a newline that is written literally does not come
//  back. All three go out as references.
//
CString
PrintXML::EscapeAttr ( LPCTSTR lpszText )
{
    CString strOut;
    if ( lpszText == nullptr )
      return strOut;

    for ( const TCHAR *p = lpszText; *p; ++p )
    {
      switch ( *p )
      {
        case _T('&'):  strOut += _T("&amp;");  break;
        case _T('<'):  strOut += _T("&lt;");   break;
        case _T('>'):  strOut += _T("&gt;");   break;
        case _T('\"'): strOut += _T("&quot;"); break;
        case _T('\t'): strOut += _T("&#9;");   break;
        case _T('\n'): strOut += _T("&#10;");  break;
        case _T('\r'): strOut += _T("&#13;");  break;
        default:
          if ( (unsigned int)*p < 0x20u )
            break;                     // Unrepresentable; dropped
          strOut += *p;
          break;
      }
    }
    return strOut;
}

//
//  Reduces a Msgcore item name to an XML tag name.
//  NOTES: Everything outside [A-Za-z0-9_-] becomes an underscore, and a name
//         that cannot START one takes an underscore prefix. Non-ASCII letters
//         fold too, which XML would in fact accept - see the header for why a
//         tag whose legality depends on which XML version the reader
//         implements is worse than an obviously-ASCII one.
//       : Names beginning "xml" in any case are reserved by the specification
//         and take the same prefix.
//       : THE ORIGINAL NAME IS NOT LOST. RenderNode writes p2p:name whenever
//         this returns something other than what it was given.
//
CString
PrintXML::XmlName ( LPCTSTR lpszName )
{
    CString strOut;
    if ( lpszName )
    {
      for ( const TCHAR *p = lpszName; *p; ++p )
      {
        const unsigned int uCode = (unsigned int)*p;
        if ( ( uCode >= (unsigned int)_T('A') && uCode <= (unsigned int)_T('Z') ) ||
             ( uCode >= (unsigned int)_T('a') && uCode <= (unsigned int)_T('z') ) ||
             ( uCode >= (unsigned int)_T('0') && uCode <= (unsigned int)_T('9') ) ||
               uCode == (unsigned int)_T('_') || uCode == (unsigned int)_T('-')     )
          strOut += *p;
        else
          strOut += _T('_');
      }
    }
    if ( strOut.IsEmpty ( ) )
      return CString ( _T("item") );

    const TCHAR chFirst = strOut[0];
    const bool  bLetter = ( chFirst >= _T('A') && chFirst <= _T('Z') )
                       || ( chFirst >= _T('a') && chFirst <= _T('z') )
                       ||   chFirst == _T('_');
    if ( !bLetter || strOut.Left ( 3 ).CompareNoCase ( _T("xml") ) == 0 )
    {
      CString strPrefixed ( _T("_") );
      strPrefixed += strOut;
      return strPrefixed;
    }
    return strOut;
}

///////////////////////////////////////////////////////////////////////
//  Parsing

//
//  Reads the XML document this renderer holds into oItem, REPLACING everything
//  oItem held.
//
//  Parameters:  P3PmsgItem& oItem
//               Manager, item, list or vector to write into
//
//  Returns:     MsgPrint&
//               This renderer, so GetError() can be read from the expression
//
//  NOTES: TWO PASSES, and the first one touches nothing. The document becomes a
//         MsgValue tree first and only a tree that parsed to its end is written
//         to the store - because the write REPLACES, and a truncated node
//         holding half a document is worse than the node the caller still had.
//       : The root element's NAME is discarded. A document identifies a node,
//         it does not rename one, and the name of the node being written into
//         is the store's own - exactly what the JSON dialect does with the key
//         of its rooted wrapper.
//
MsgPrint&
PrintXML::Parse ( P3PmsgItem& oItem )
{
    ClearError ( );
    try
    {
      int nPos = 0;
      if ( !ReadProlog ( nPos ) )
        return *this;

      CString  strTag;
      XmlAttrs oAttrs;
      bool     bEmpty = false;
      if ( !ReadStartTag ( strTag, oAttrs, bEmpty, nPos ) )
        return *this;

      MsgValue oRoot;
      if ( !ReadElementBody ( oRoot, strTag, oAttrs, bEmpty, nPos, 0 ) )
        return *this;

      //  Comments and processing instructions are legal AFTER the root element
      //  as well as before it. Anything else at this point is a second root,
      //  which is the one thing an XML document may not have.
      for ( ;; )
      {
        SkipSpace ( nPos );
        if ( LooksLike ( _T("<!--"), nPos ) )
        {
          if ( !SkipComment ( nPos ) )
            return *this;
          continue;
        }
        if ( LooksLike ( _T("<?"), nPos ) )
        {
          if ( !SkipPI ( nPos ) )
            return *this;
          continue;
        }
        break;
      }
      if ( nPos < m_strText.GetLength ( ) )
      {
        SetError ( _T("Trailing text after the root element"), nPos );
        return *this;
      }

      BuildItem ( oItem, oRoot, 0 );
      return *this;
    }
    //  A throw from the store side - a name the format refuses, a heap that
    //  cannot grow. The document was legal and the store would not take it,
    //  which is a different failure from a syntax error and says so.
    catch_pP2Pevent_Cancel
    catch_ALL_Cancel
    SetError ( _T("The store refused the document"), -1 );
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Reading - the start tag's attributes

LPCTSTR
PrintXML::XmlAttrs::Find ( LPCTSTR lpszName ) const
{
    for ( size_t i = 0; i < oNames.size ( ); i++ )
      if ( oNames[i].Compare ( lpszName ) == 0 )
        return (LPCTSTR)oValues[i];
    return nullptr;
}

bool
PrintXML::XmlAttrs::Flag ( LPCTSTR lpszName ) const
{
    LPCTSTR lpszValue = Find ( lpszName );
    return lpszValue != nullptr && CString ( lpszValue ).Compare ( _T("true") ) == 0;
}

///////////////////////////////////////////////////////////////////////
//  Reading
//  NOTES: A recursive descent over m_strText, by character offset. Each Read
//         leaves nPos on the first character it did not consume, and returns
//         false having called SetError - which keeps the FIRST message, so the
//         unwind does not overwrite a mismatched end tag on line 40 with
//         "unexpected end of document".

void
PrintXML::SkipSpace ( int& nPos ) const
{
    const int nLength = m_strText.GetLength ( );
    while ( nPos < nLength && PrintXML_IsSpace ( m_strText[nPos] ) )
      nPos++;
}

bool
PrintXML::LooksLike ( LPCTSTR lpszWord, int nPos ) const
{
    const int nLength = m_strText.GetLength ( );
    for ( int i = 0; lpszWord[i]; i++ )
      if ( nPos + i >= nLength || m_strText[nPos + i] != lpszWord[i] )
        return false;
    return true;
}

//
//  Everything before the root element: the XML declaration, comments,
//  processing instructions and a document type declaration, every one of them
//  optional and every one of them skipped.
//  NOTES: The declaration is not BELIEVED, only skipped - its encoding names
//         the bytes of a file, and by the time a document is in this object it
//         has already been decoded. GetDeclaration() says what a RENDER writes
//         and nothing about what a parse will accept.
//
bool
PrintXML::ReadProlog ( int& nPos )
{
    for ( ;; )
    {
      SkipSpace ( nPos );
      if ( LooksLike ( _T("<!--"), nPos ) )
      {
        if ( !SkipComment ( nPos ) )
          return false;
        continue;
      }
      if ( LooksLike ( _T("<?"), nPos ) )
      {
        if ( !SkipPI ( nPos ) )
          return false;
        continue;
      }
      if ( LooksLike ( _T("<!DOCTYPE"), nPos ) )
      {
        //  Skipped rather than read, INTERNAL SUBSET AND ALL. This reader
        //  resolves the five predefined entities and nothing else, so a
        //  declared one would be reported as unknown wherever it was used -
        //  which is a clearer failure than silently taking half a DTD's word
        //  for the shape of the document.
        const int nLength = m_strText.GetLength ( );
        while ( nPos < nLength && m_strText[nPos] != _T('>') && m_strText[nPos] != _T('[') )
          nPos++;
        if ( nPos < nLength && m_strText[nPos] == _T('[') )
        {
          while ( nPos < nLength && m_strText[nPos] != _T(']') )
            nPos++;
          while ( nPos < nLength && m_strText[nPos] != _T('>') )
            nPos++;
        }
        if ( nPos >= nLength )
        {
          SetError ( _T("Unterminated document type declaration"), nPos );
          return false;
        }
        nPos++;
        continue;
      }
      break;
    }

    if ( nPos >= m_strText.GetLength ( ) || m_strText[nPos] != _T('<') )
    {
      SetError ( _T("No root element - an XML document is exactly one"), nPos );
      return false;
    }
    return true;
}

bool
PrintXML::SkipComment ( int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    const int nFrom   = nPos;
    nPos += 4;                         // <!--
    while ( nPos < nLength && !LooksLike ( _T("-->"), nPos ) )
      nPos++;
    if ( nPos >= nLength )
    {
      SetError ( _T("Unterminated comment"), nFrom );
      return false;
    }
    nPos += 3;
    return true;
}

bool
PrintXML::SkipPI ( int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    const int nFrom   = nPos;
    nPos += 2;                         // <?
    while ( nPos < nLength && !LooksLike ( _T("?>"), nPos ) )
      nPos++;
    if ( nPos >= nLength )
    {
      SetError ( _T("Unterminated processing instruction"), nFrom );
      return false;
    }
    nPos += 2;
    return true;
}

//
//  An XML Name. Deliberately WIDER than what XmlName writes: this reader takes
//  documents from elsewhere, and refusing a tag it would never itself have
//  produced would make it useless for exactly the case an XML dialect exists
//  for. The colon is accepted because the reserved prefix needs it.
//
bool
PrintXML::ReadName ( CString& strOut, int& nPos ) const
{
    const int nLength = m_strText.GetLength ( );
    strOut.Empty ( );
    while ( nPos < nLength )
    {
      const TCHAR        c     = m_strText[nPos];
      const unsigned int uCode = (unsigned int)c;
      if ( ( uCode >= (unsigned int)_T('A') && uCode <= (unsigned int)_T('Z') ) ||
           ( uCode >= (unsigned int)_T('a') && uCode <= (unsigned int)_T('z') ) ||
           ( uCode >= (unsigned int)_T('0') && uCode <= (unsigned int)_T('9') ) ||
             c == _T('_') || c == _T('-') || c == _T('.') || c == _T(':')       ||
             uCode >= 0x80u                                                       )
      {
        strOut += c;
        nPos++;
        continue;
      }
      break;
    }
    return !strOut.IsEmpty ( );
}

//
//  Consumes one start tag, '<' through '>', and every attribute in it.
//
bool
PrintXML::ReadStartTag ( CString& strTag, XmlAttrs& oAttrs, bool& bEmpty, int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    bEmpty = false;
    oAttrs.oNames.clear ( );
    oAttrs.oValues.clear ( );

    if ( nPos >= nLength || m_strText[nPos] != _T('<') )
    {
      SetError ( _T("Expecting an element"), nPos );
      return false;
    }
    nPos++;
    if ( !ReadName ( strTag, nPos ) )
    {
      SetError ( _T("Expecting a tag name after '<'"), nPos );
      return false;
    }

    for ( ;; )
    {
      SkipSpace ( nPos );
      if ( nPos >= nLength )
      {
        SetError ( _T("Unterminated start tag"), nPos );
        return false;
      }
      if ( m_strText[nPos] == _T('>') )
      {
        nPos++;
        return true;
      }
      if ( m_strText[nPos] == _T('/') )
      {
        nPos++;
        if ( nPos >= nLength || m_strText[nPos] != _T('>') )
        {
          SetError ( _T("Expecting '>' after '/' in an empty element"), nPos );
          return false;
        }
        nPos++;
        bEmpty = true;
        return true;
      }

      CString strName;
      if ( !ReadName ( strName, nPos ) )
      {
        SetError ( _T("Expecting an attribute name, '>' or '/>'"), nPos );
        return false;
      }
      SkipSpace ( nPos );
      if ( nPos >= nLength || m_strText[nPos] != _T('=') )
      {
        SetError ( _T("Expecting '=' after an attribute name"), nPos );
        return false;
      }
      nPos++;
      SkipSpace ( nPos );

      CString strValue;
      if ( !ReadAttrValue ( strValue, nPos ) )
        return false;
      oAttrs.oNames.push_back  ( strName );
      oAttrs.oValues.push_back ( strValue );
    }
}

bool
PrintXML::ReadAttrValue ( CString& strOut, int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    strOut.Empty ( );

    if ( nPos >= nLength
      || ( m_strText[nPos] != _T('\"') && m_strText[nPos] != _T('\'') ) )
    {
      SetError ( _T("An attribute value must be quoted"), nPos );
      return false;
    }
    const TCHAR chDelim = m_strText[nPos];
    nPos++;

    while ( nPos < nLength )
    {
      const TCHAR c = m_strText[nPos];
      if ( c == chDelim )
      {
        nPos++;
        return true;
      }
      if ( c == _T('<') )
      {
        SetError ( _T("'<' is not allowed in an attribute value"), nPos );
        return false;
      }
      if ( c == _T('&') )
      {
        if ( !ReadReference ( strOut, nPos ) )
          return false;
        continue;
      }
      //  Attribute-value normalisation: a literal tab, CR or LF inside an
      //  attribute is a space. Anything that had to survive was written as a
      //  reference, and ReadReference has already put it in.
      strOut += ( c == _T('\t') || c == _T('\n') || c == _T('\r') ) ? _T(' ') : c;
      nPos++;
    }

    SetError ( _T("Unterminated attribute value"), nPos );
    return false;
}

//
//  Character data up to the next '<' that begins markup, with references
//  resolved and CDATA sections taken verbatim.
//  NOTES: NOT TRIMMED. An element that holds only text holds exactly what is
//         between its tags, spaces included, because a store may legitimately
//         hold " Mann " and a reader that tidied it would corrupt the cell it
//         was meant to restore. Whitespace BETWEEN elements is discarded, but
//         that decision belongs to ReadElementBody, which is the only place
//         that knows whether there were any.
//
bool
PrintXML::ReadCharData ( CString& strOut, int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    strOut.Empty ( );

    while ( nPos < nLength )
    {
      const TCHAR c = m_strText[nPos];
      if ( c == _T('&') )
      {
        if ( !ReadReference ( strOut, nPos ) )
          return false;
        continue;
      }
      if ( c == _T('<') )
      {
        if ( !LooksLike ( _T("<![CDATA["), nPos ) )
          return true;                 // Markup; the caller decides which kind
        const int nFrom = nPos;
        nPos += 9;
        while ( nPos < nLength && !LooksLike ( _T("]]>"), nPos ) )
        {
          strOut += m_strText[nPos];
          nPos++;
        }
        if ( nPos >= nLength )
        {
          SetError ( _T("Unterminated CDATA section"), nFrom );
          return false;
        }
        nPos += 3;
        continue;
      }
      strOut += c;
      nPos++;
    }
    return true;
}

//
//  One entity or character reference.
//  NOTES: The five XML predefines and the two numeric forms, and nothing else.
//         A reference this does not know is an ERROR rather than a passed-
//         through literal: a document that declared its own entities in a DTD
//         would otherwise come back with &myco; sitting in the middle of a
//         string, which is not the value the document was describing.
//
bool
PrintXML::ReadReference ( CString& strOut, int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    const int nFrom   = nPos;
    nPos++;                            // &

    CString strWord;
    if ( nPos < nLength && m_strText[nPos] == _T('#') )
    {
      nPos++;
      unsigned int uCode  = 0;
      int          nRead  = 0;
      const bool   bHex   = ( nPos < nLength
                           && ( m_strText[nPos] == _T('x') || m_strText[nPos] == _T('X') ) );
      if ( bHex )
        nPos++;

      while ( nPos < nLength && m_strText[nPos] != _T(';') )
      {
        const TCHAR  c     = m_strText[nPos];
        unsigned int uThis = 0;
        if ( c >= _T('0') && c <= _T('9') )
          uThis = (unsigned int)( c - _T('0') );
        else if ( bHex && c >= _T('a') && c <= _T('f') )
          uThis = (unsigned int)( c - _T('a') ) + 10u;
        else if ( bHex && c >= _T('A') && c <= _T('F') )
          uThis = (unsigned int)( c - _T('A') ) + 10u;
        else
        {
          SetError ( _T("A character reference must be decimal or &#x hexadecimal"), nFrom );
          return false;
        }
        //  Bounded before it can wrap. Anything above the BMP cannot be one
        //  TCHAR on a build where TCHAR is 16 bits, and this reader does not
        //  compose surrogate pairs from numeric references.
        if ( uCode > 0x10FFFFu )
        {
          SetError ( _T("A character reference outside Unicode"), nFrom );
          return false;
        }
        uCode = uCode * ( bHex ? 16u : 10u ) + uThis;
        nRead++;
        nPos++;
      }
      if ( nRead == 0 || nPos >= nLength )
      {
        SetError ( _T("Unterminated character reference"), nFrom );
        return false;
      }
      nPos++;                          // ;
      if ( uCode == 0 || uCode > 0xFFFFu )
      {
        SetError ( _T("A character reference this build cannot represent"), nFrom );
        return false;
      }
      strOut += (TCHAR)uCode;
      return true;
    }

    while ( nPos < nLength && m_strText[nPos] != _T(';') )
    {
      strWord += m_strText[nPos];
      nPos++;
    }
    if ( nPos >= nLength )
    {
      SetError ( _T("Unterminated entity reference"), nFrom );
      return false;
    }
    nPos++;                            // ;

    if      ( strWord.Compare ( _T("amp")  ) == 0 ) strOut += _T('&');
    else if ( strWord.Compare ( _T("lt")   ) == 0 ) strOut += _T('<');
    else if ( strWord.Compare ( _T("gt")   ) == 0 ) strOut += _T('>');
    else if ( strWord.Compare ( _T("quot") ) == 0 ) strOut += _T('\"');
    else if ( strWord.Compare ( _T("apos") ) == 0 ) strOut += _T('\'');
    else
    {
      CString strError ( _T("Unknown entity reference &") );
      strError += strWord;
      strError += _T(";");
      SetError ( (LPCTSTR)strError, nFrom );
      return false;
    }
    return true;
}

//
//  Everything after a start tag: the content, the matching end tag, and the
//  assembly of the MsgValue the two describe.
//
//  Parameters:  MsgValue& oOut
//               Node to fill - fresh, and owned by the caller
//
//               const CString& strTag
//               The start tag's name, which the end tag has to match
//
//               const XmlAttrs& oAttrs
//               The start tag's attributes, reserved ones included
//
//               bool bEmpty
//               Whether the start tag closed itself, so there is no content
//
//  Returns:     bool
//               false having called SetError
//
//  NOTES: The children are read into two STAGING nodes rather than straight
//         into oOut, because what oOut is - a scalar, an array, or an object
//         with an .items member - is not known until the last child has been
//         read. AdoptChildren then moves them in one operation; MsgValue owns
//         its children by pointer and does not copy at all.
//
bool
PrintXML::ReadElementBody ( MsgValue& oOut, const CString& strTag,
                            const XmlAttrs& oAttrs, bool bEmpty,
                            int& nPos, int nDepth )
{
    if ( nDepth > m_nDepthMax )
    {
      SetError ( _T("Document nests deeper than the depth ceiling"), nPos );
      return false;
    }

    const int     nLength   = m_strText.GetLength ( );
    const bool    bList     = oAttrs.Flag ( XML_ATTR_LIST );
    const bool    bVect     = oAttrs.Flag ( XML_ATTR_VECT );
    const bool    bNull     = oAttrs.Flag ( XML_ATTR_NULL );
    const LPCTSTR lpszType  = oAttrs.Find ( XML_ATTR_TYPE );

    MsgValue oItems   ( MsgPrint::Value_Array );
    MsgValue oMembers ( MsgPrint::Value_Object );
    CString  strText;

    while ( !bEmpty )
    {
      CString strRun;
      if ( !ReadCharData ( strRun, nPos ) )
        return false;
      strText += strRun;

      if ( nPos >= nLength )
      {
        CString strError ( _T("Unexpected end of document, expecting </") );
        strError += strTag;
        strError += _T(">");
        SetError ( (LPCTSTR)strError, nPos );
        return false;
      }

      if ( LooksLike ( _T("</"), nPos ) )
      {
        const int nFrom = nPos;
        nPos += 2;
        CString strEnd;
        if ( !ReadName ( strEnd, nPos ) )
        {
          SetError ( _T("Expecting a tag name after '</'"), nPos );
          return false;
        }
        SkipSpace ( nPos );
        if ( nPos >= nLength || m_strText[nPos] != _T('>') )
        {
          SetError ( _T("Expecting '>' at the end of an end tag"), nPos );
          return false;
        }
        nPos++;
        if ( strEnd.Compare ( (LPCTSTR)strTag ) != 0 )
        {
          CString strError ( _T("End tag </") );
          strError += strEnd;
          strError += _T("> does not match <");
          strError += strTag;
          strError += _T(">");
          SetError ( (LPCTSTR)strError, nFrom );
          return false;
        }
        break;
      }

      if ( LooksLike ( _T("<!--"), nPos ) )
      {
        if ( !SkipComment ( nPos ) )
          return false;
        continue;
      }
      if ( LooksLike ( _T("<?"), nPos ) )
      {
        if ( !SkipPI ( nPos ) )
          return false;
        continue;
      }
      if ( LooksLike ( _T("<!"), nPos ) )
      {
        SetError ( _T("A declaration is not allowed inside an element"), nPos );
        return false;
      }

      //  A child element. Its key is decided from its own start tag, which is
      //  why the tag is read HERE and the body below - the staging node it
      //  belongs in is not known until its attributes have been seen.
      CString  strChildTag;
      XmlAttrs oChildAttrs;
      bool     bChildEmpty = false;
      const int nChildFrom = nPos;
      if ( !ReadStartTag ( strChildTag, oChildAttrs, bChildEmpty, nPos ) )
        return false;

      bool    bItemChild = false;
      CString strKey;
      if ( strChildTag.Compare ( XML_TAG_ITEM ) == 0 )
        bItemChild = true;
      else if ( strChildTag.Compare ( XML_TAG_VALUE ) == 0 )
        strKey = MSGPRINT_KEY_VALUE;
      else if ( strChildTag.Left ( 4 ).Compare ( XML_PREFIX ) == 0 )
      {
        //  Reserved, and not one of the two this dialect defines. Refused
        //  rather than treated as an ordinary member: the prefix is the one
        //  thing in the document that promises what an element means.
        CString strError ( _T("Unknown reserved element <") );
        strError += strChildTag;
        strError += _T(">");
        SetError ( (LPCTSTR)strError, nChildFrom );
        return false;
      }
      else
      {
        //  p2p:name when XML could not spell the real one, and the tag itself
        //  when it could - which is what the render's silence means.
        LPCTSTR lpszReal = oChildAttrs.Find ( XML_ATTR_NAME );
        if ( lpszReal )
          strKey = lpszReal;
        else
          strKey = strChildTag;
        if ( oChildAttrs.Flag ( XML_ATTR_ATTR ) )
        {
          CString strAttrKey;
          strAttrKey += MSGPRINT_KEY_ATTR;
          strAttrKey += strKey;
          strKey = strAttrKey;
        }
      }

      MsgValue& oChild = bItemChild ? oItems.Add ( )
                                    : oMembers.Add ( (LPCTSTR)strKey );
      if ( !ReadElementBody ( oChild, strChildTag, oChildAttrs, bChildEmpty,
                              nPos, nDepth + 1 ) )
        return false;
    }

    //  ASSEMBLY. An element that carries <p2p:item> children is an array; one
    //  that carries named children too is the object form, whose .items member
    //  is that array - which is exactly the shape the JSON dialect writes, and
    //  the shape BuildItem already knows how to put in a store.
    const bool bHasItems   = oItems.GetCount ( ) > 0 || bList || bVect;
    const bool bHasMembers = oMembers.GetCount ( ) > 0;

    if ( bHasItems )
    {
      if ( !bHasMembers )
      {
        oOut.SetKind ( MsgPrint::Value_Array );
        oOut.SetVectorHint ( bVect );
        oOut.AdoptChildren ( oItems, nullptr );
        return true;
      }
      oOut.SetKind ( MsgPrint::Value_Object );
      MsgValue& oArray = oOut.Add ( MSGPRINT_KEY_ITEMS );
      oArray.SetKind ( MsgPrint::Value_Array );
      oArray.SetVectorHint ( bVect );
      oArray.AdoptChildren ( oItems, nullptr );
      oOut.AdoptChildren ( oMembers, nullptr );
      return true;
    }

    if ( bHasMembers )
    {
      oOut.SetKind ( MsgPrint::Value_Object );
      //  TEXT BESIDE ELEMENTS is taken as the node's own value, which is what
      //  a hand-written <Address>Sydney<Street>..</Street></Address> means by
      //  it. A render never produces that shape - it writes <p2p:value> - so
      //  this is leniency towards a document nothing here wrote, and an
      //  explicit p2p:value wins over it.
      if ( !PrintXML_IsBlank ( strText )
        && oMembers.Find ( MSGPRINT_KEY_VALUE ) == nullptr )
      {
        MsgValue& oOwn = oOut.Add ( MSGPRINT_KEY_VALUE );
        PrintXML_SetScalar ( oOwn, strText, lpszType, bNull );
      }
      oOut.AdoptChildren ( oMembers, nullptr );
      return true;
    }

    PrintXML_SetScalar ( oOut, strText, lpszType, bNull );
    return true;
}

///////////////////////////////////////////////////////////////////////
//  Rendering operators

PrintXML&
operator >> ( P3PmsgItem& oItem, PrintXML& oXML )
{
    oXML.Render ( oItem );
    return oXML;
}

PrintXML&
operator << ( PrintXML& oXML, P3PmsgItem& oItem )
{
    oXML.Render ( oItem );
    return oXML;
}

///////////////////////////////////////////////////////////////////////
//  Parsing operators

PrintXML&
operator << ( P3PmsgItem& oItem, PrintXML& oXML )
{
    oXML.Parse ( oItem );
    return oXML;
}

PrintXML&
operator >> ( PrintXML& oXML, P3PmsgItem& oItem )
{
    oXML.Parse ( oItem );
    return oXML;
}
