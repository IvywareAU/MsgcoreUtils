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
#include "MsgValue.h"

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
//  Parsing

//
//  Reads the JSON document this renderer holds into oItem, REPLACING
//  everything oItem held.
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
//       : GetRooted() is believed rather than sniffed. It says what the render
//         that produced this document did, and a reader guessing instead would
//         unwrap { "Surname": "Mann" } - a legitimate single-member unrooted
//         document - into the string "Mann".
//
MsgPrint&
PrintJson::Parse ( P3PmsgItem& oItem )
{
    ClearError ( );
    try
    {
      MsgValue oRoot;
      int      nPos = 0;
      if ( !ReadValue ( oRoot, nPos, 0 ) )
        return *this;

      SkipSpace ( nPos );
      if ( nPos < m_strText.GetLength ( ) )
      {
        SetError ( _T("Trailing text after the end of the document"), nPos );
        return *this;
      }

      //  The outer object the rooted form wraps the document in exists only to
      //  name the node, and the name is not ours to apply: a document
      //  identifies a node, it does not rename one, and the root name of a
      //  store is the store's own.
      const MsgValue *pDoc = &oRoot;
      if ( m_bRooted )
      {
        if ( !oRoot.IsObject ( ) || oRoot.GetCount ( ) != 1 )
        {
          SetError ( _T("A rooted document must be an object with exactly one ")
                     _T("member - call SetRooted(false) for a bare one"), 0 );
          return *this;
        }
        pDoc = &oRoot.r_child ( 0 );
      }

      BuildItem ( oItem, *pDoc, 0 );
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
//  Reading
//  NOTES: A recursive descent over m_strText, by character offset. Each Read
//         leaves nPos on the first character it did not consume, and returns
//         false having called SetError - which keeps the FIRST message, so the
//         unwind does not overwrite a missing comma on line 40 with "unexpected
//         end of document".

void
PrintJson::SkipSpace ( int& nPos ) const
{
    const int nLength = m_strText.GetLength ( );
    while ( nPos < nLength )
    {
      const TCHAR c = m_strText[nPos];
      //  The four RFC 8259 allows and nothing else. A comment is not
      //  whitespace, and accepting one here would make this reader take
      //  documents no other JSON reader will.
      if ( c != _T(' ') && c != _T('\t') && c != _T('\r') && c != _T('\n') )
        return;
      nPos++;
    }
}

bool
PrintJson::ReadValue ( MsgValue& oValue, int& nPos, int nDepth )
{
    if ( nDepth > m_nDepthMax )
    {
      SetError ( _T("Document nests deeper than the depth ceiling"), nPos );
      return false;
    }

    SkipSpace ( nPos );
    if ( nPos >= m_strText.GetLength ( ) )
    {
      SetError ( _T("Unexpected end of document, expecting a value"), nPos );
      return false;
    }

    const TCHAR c = m_strText[nPos];
    switch ( c )
    {
      case _T('{'):
        return ReadObject ( oValue, nPos, nDepth );

      case _T('['):
        return ReadArray ( oValue, nPos, nDepth );

      case _T('"'):
      {
        CString strText;
        if ( !ReadString ( strText, nPos ) )
          return false;
        oValue.SetScalar ( Value_String, (LPCTSTR)strText );
        return true;
      }

      default:
        break;
    }

    if ( ReadLiteral ( _T("true"), nPos ) )
    {
      oValue.SetScalar ( Value_Bool, _T("true") );
      return true;
    }
    if ( ReadLiteral ( _T("false"), nPos ) )
    {
      oValue.SetScalar ( Value_Bool, _T("false") );
      return true;
    }
    if ( ReadLiteral ( _T("null"), nPos ) )
    {
      oValue.SetScalar ( Value_Null, _T("") );
      return true;
    }
    if ( c == _T('-') || ( c >= _T('0') && c <= _T('9') ) )
      return ReadNumber ( oValue, nPos );

    SetError ( _T("Expecting a value"), nPos );
    return false;
}

//
//  NOTES: The members go in IN DOCUMENT ORDER and are not de-duplicated, which
//         is what MsgValue promises. Order is what the store preserves for
//         descendants, and two members of one name is something the store can
//         express even though the renderer never writes it.
//
bool
PrintJson::ReadObject ( MsgValue& oValue, int& nPos, int nDepth )
{
    oValue.SetKind ( Value_Object );
    nPos++;                            // The '{'

    SkipSpace ( nPos );
    if ( nPos < m_strText.GetLength ( ) && m_strText[nPos] == _T('}') )
    {
      nPos++;
      return true;
    }

    for ( ;; )
    {
      SkipSpace ( nPos );
      if ( nPos >= m_strText.GetLength ( ) || m_strText[nPos] != _T('"') )
      {
        SetError ( _T("Expecting a member name"), nPos );
        return false;
      }

      CString strKey;
      if ( !ReadString ( strKey, nPos ) )
        return false;

      SkipSpace ( nPos );
      if ( nPos >= m_strText.GetLength ( ) || m_strText[nPos] != _T(':') )
      {
        SetError ( _T("Expecting ':' after a member name"), nPos );
        return false;
      }
      nPos++;

      if ( !ReadValue ( oValue.Add ( (LPCTSTR)strKey ), nPos, nDepth + 1 ) )
        return false;

      SkipSpace ( nPos );
      if ( nPos >= m_strText.GetLength ( ) )
      {
        SetError ( _T("Unexpected end of document inside an object"), nPos );
        return false;
      }
      if ( m_strText[nPos] == _T(',') )
      {
        nPos++;
        continue;
      }
      if ( m_strText[nPos] == _T('}') )
      {
        nPos++;
        return true;
      }
      SetError ( _T("Expecting ',' or '}'"), nPos );
      return false;
    }
}

bool
PrintJson::ReadArray ( MsgValue& oValue, int& nPos, int nDepth )
{
    oValue.SetKind ( Value_Array );
    nPos++;                            // The '['

    SkipSpace ( nPos );
    if ( nPos < m_strText.GetLength ( ) && m_strText[nPos] == _T(']') )
    {
      nPos++;
      return true;
    }

    for ( ;; )
    {
      if ( !ReadValue ( oValue.Add ( ), nPos, nDepth + 1 ) )
        return false;

      SkipSpace ( nPos );
      if ( nPos >= m_strText.GetLength ( ) )
      {
        SetError ( _T("Unexpected end of document inside an array"), nPos );
        return false;
      }
      if ( m_strText[nPos] == _T(',') )
      {
        nPos++;
        continue;
      }
      if ( m_strText[nPos] == _T(']') )
      {
        nPos++;
        return true;
      }
      SetError ( _T("Expecting ',' or ']'"), nPos );
      return false;
    }
}

//
//  A quoted string, with its escapes resolved - the inverse of Escape.
//  NOTES: \u takes a surrogate PAIR where the two halves are adjacent, because
//         a code point above the BMP is written as two \u escapes and reading
//         them separately would put two unpaired surrogates in the store. An
//         unpaired one that survives becomes U+FFFD rather than being kept:
//         a lone surrogate is not a character and every later encode of it
//         would have to invent one anyway.
//
bool
PrintJson::ReadString ( CString& strOut, int& nPos )
{
    strOut.Empty ( );
    const int nLength = m_strText.GetLength ( );
    nPos++;                            // The opening quote

    while ( nPos < nLength )
    {
      const TCHAR c = m_strText[nPos];

      if ( c == _T('"') )
      {
        nPos++;
        return true;
      }
      if ( c != _T('\\') )
      {
        //  A raw control character is invalid JSON, and letting one through
        //  would mean this reader accepts documents it would not itself write.
        if ( (unsigned int)c < 0x20u )
        {
          SetError ( _T("Unescaped control character in a string"), nPos );
          return false;
        }
        strOut += c;
        nPos++;
        continue;
      }

      nPos++;                          // The backslash
      if ( nPos >= nLength )
        break;

      const TCHAR cEscape = m_strText[nPos++];
      switch ( cEscape )
      {
        case _T('"'):  strOut += _T('"');  break;
        case _T('\\'): strOut += _T('\\'); break;
        case _T('/'):  strOut += _T('/');  break;
        case _T('b'):  strOut += _T('\b'); break;
        case _T('f'):  strOut += _T('\f'); break;
        case _T('n'):  strOut += _T('\n'); break;
        case _T('r'):  strOut += _T('\r'); break;
        case _T('t'):  strOut += _T('\t'); break;
        case _T('u'):
        {
          unsigned int uCode = 0;
          if ( !ReadHex4 ( uCode, nPos ) )
            return false;

          if ( uCode >= 0xD800u && uCode <= 0xDBFFu )
          {
            //  A high surrogate, so look for the low one that completes it.
            if ( nPos + 1 < nLength
              && m_strText[nPos]     == _T('\\')
              && m_strText[nPos + 1] == _T('u') )
            {
              const int    nSave = nPos;
              unsigned int uLow  = 0;
              nPos += 2;
              if ( !ReadHex4 ( uLow, nPos ) )
                return false;
              if ( uLow >= 0xDC00u && uLow <= 0xDFFFu )
                uCode = 0x10000u + ((uCode - 0xD800u) << 10) + (uLow - 0xDC00u);
              else
              {
                uCode = 0xFFFDu;       // Not a pair after all
                nPos  = nSave;
              }
            }
            else
              uCode = 0xFFFDu;
          }
          else if ( uCode >= 0xDC00u && uCode <= 0xDFFFu )
            uCode = 0xFFFDu;           // A low surrogate with nothing before it

          if ( uCode >= 0x10000u && sizeof(TCHAR) == 2 )
          {
            const unsigned int uRel = uCode - 0x10000u;
            strOut += (TCHAR)(0xD800u + (uRel >> 10));
            strOut += (TCHAR)(0xDC00u + (uRel & 0x3FFu));
          }
          else
            strOut += (TCHAR)uCode;
          break;
        }
        default:
          SetError ( _T("Unknown escape in a string"), nPos - 1 );
          return false;
      }
    }

    SetError ( _T("Unterminated string"), nPos );
    return false;
}

bool
PrintJson::ReadHex4 ( unsigned int& uCode, int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    if ( nPos + 4 > nLength )
    {
      SetError ( _T("Truncated \\u escape"), nPos );
      return false;
    }

    uCode = 0;
    for ( int i = 0; i < 4; i++ )
    {
      const TCHAR c     = m_strText[nPos + i];
      unsigned int nDigit = 0;
      if      ( c >= _T('0') && c <= _T('9') ) nDigit = (unsigned int)(c - _T('0'));
      else if ( c >= _T('a') && c <= _T('f') ) nDigit = (unsigned int)(c - _T('a')) + 10;
      else if ( c >= _T('A') && c <= _T('F') ) nDigit = (unsigned int)(c - _T('A')) + 10;
      else
      {
        SetError ( _T("Bad hex digit in a \\u escape"), nPos + i );
        return false;
      }
      uCode = (uCode << 4) | nDigit;
    }
    nPos += 4;
    return true;
}

//
//  NOTES: The RUN is taken here and the VALUE is worked out in ParseScalar,
//         which is where the store's types live. What this has to get right is
//         only where the number ends, and the grammar is checked so that a
//         document saying 1.2.3 is refused rather than quietly read as 1.2.
//
bool
PrintJson::ReadNumber ( MsgValue& oValue, int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    const int nStart  = nPos;

    if ( nPos < nLength && m_strText[nPos] == _T('-') )
      nPos++;

    const int nIntStart = nPos;
    while ( nPos < nLength && m_strText[nPos] >= _T('0') && m_strText[nPos] <= _T('9') )
      nPos++;
    if ( nPos == nIntStart )
    {
      SetError ( _T("Expecting a digit in a number"), nPos );
      return false;
    }

    if ( nPos < nLength && m_strText[nPos] == _T('.') )
    {
      nPos++;
      const int nFracStart = nPos;
      while ( nPos < nLength && m_strText[nPos] >= _T('0') && m_strText[nPos] <= _T('9') )
        nPos++;
      if ( nPos == nFracStart )
      {
        SetError ( _T("Expecting a digit after the decimal point"), nPos );
        return false;
      }
    }

    if ( nPos < nLength && ( m_strText[nPos] == _T('e') || m_strText[nPos] == _T('E') ) )
    {
      nPos++;
      if ( nPos < nLength && ( m_strText[nPos] == _T('+') || m_strText[nPos] == _T('-') ) )
        nPos++;
      const int nExpStart = nPos;
      while ( nPos < nLength && m_strText[nPos] >= _T('0') && m_strText[nPos] <= _T('9') )
        nPos++;
      if ( nPos == nExpStart )
      {
        SetError ( _T("Expecting a digit in the exponent"), nPos );
        return false;
      }
    }

    oValue.SetScalar ( Value_Number, (LPCTSTR)m_strText.Mid ( nStart, nPos - nStart ) );
    return true;
}

//
//  Consumes lpszWord when it is what stands at nPos. NOT a prefix match: the
//  character after it has to be one that cannot continue a literal, or
//  "nullify" would read as null followed by rubbish.
//
bool
PrintJson::ReadLiteral ( LPCTSTR lpszWord, int& nPos )
{
    const int nLength = m_strText.GetLength ( );
    int       nWord   = 0;
    while ( lpszWord[nWord] )
    {
      if ( nPos + nWord >= nLength || m_strText[nPos + nWord] != lpszWord[nWord] )
        return false;
      nWord++;
    }
    if ( nPos + nWord < nLength )
    {
      const TCHAR c = m_strText[nPos + nWord];
      if ( ( c >= _T('a') && c <= _T('z') )
        || ( c >= _T('A') && c <= _T('Z') )
        || ( c >= _T('0') && c <= _T('9') )
        ||   c == _T('_') )
        return false;
    }
    nPos += nWord;
    return true;
}

///////////////////////////////////////////////////////////////////////
//  Rendering operator

PrintJson&
operator >> ( P3PmsgItem& oItem, PrintJson& oJson )
{
    oJson.Render ( oItem );
    return oJson;
}

PrintJson&
operator << ( PrintJson& oJson, P3PmsgItem& oItem )
{
    oJson.Render ( oItem );
    return oJson;
}

///////////////////////////////////////////////////////////////////////
//  Parsing operators

PrintJson&
operator << ( P3PmsgItem& oItem, PrintJson& oJson )
{
    oJson.Parse ( oItem );
    return oJson;
}

PrintJson&
operator >> ( PrintJson& oJson, P3PmsgItem& oItem )
{
    oJson.Parse ( oItem );
    return oJson;
}
