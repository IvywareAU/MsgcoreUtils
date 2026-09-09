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
//  MsgPrint - text accumulation and scalar rendering.
//
#include "stdafx.h"

#include <float.h>
#include <time.h>
#include <vector>

#include "MsgPrint.h"

#include "../Msgcore/Msgexception.h"
#include "../Msgcore/P2PmsgVBLock.h"

///////////////////////////////////////////////////////////////////////
//  File-local helpers

//
//  Formats a double so that reading it back yields the same double, WITHOUT
//  spending seventeen digits on a value that does not need them.
//  NOTES: %.17g always round-trips and always looks wrong - 0.1 comes back as
//         0.10000000000000001. %.15g looks right and does not always round
//         trip. Printing at 15 and checking is the only way to get both, and
//         the check is a strtod against the value that produced it.
//       : Returns false for anything JSON has no literal for - NaN, either
//         infinity, and Msgcore's own pseudo-null double - so the caller can
//         render the cell as null rather than emit a document that will not
//         parse.
//
static bool
MsgPrint_FormatReal ( double dValue, bool bSingle, CString& strOut )
{
    if ( !(dValue == dValue) )         // NaN: the only value unequal to itself
      return false;
    if ( dValue > DBL_MAX || dValue < -DBL_MAX )
      return false;                    // +/- infinity
    if ( MsgcoreIsNULL_DBLE(dValue) )
      return false;                    // Msgcore_NULL_DBLE, the pseudo-null

    if ( bSingle )
    {
      //  A float carries at most 9 significant decimal digits, so there is no
      //  shorter/longer decision to make here.
      strOut.Format ( _T("%.9g"), dValue );
    }
    else
    {
      strOut.Format ( _T("%.15g"), dValue );
      if ( _tcstod ( (LPCTSTR)strOut, nullptr ) != dValue )
        strOut.Format ( _T("%.17g"), dValue );
    }

    //  The decimal point is the C runtime's, and a host that has called
    //  setlocale with a comma locale would otherwise emit 1,5 - which is two
    //  JSON tokens and one PHP parse error.
    strOut.Replace ( _T(','), _T('.') );
    return true;
}

//
//  Formats epoch seconds as ISO-8601 local time. Returns false when the value
//  is not a time the platform can break down, which is a real case: the stored
//  cell is whatever was written into it.
//
static bool
MsgPrint_FormatEpoch ( INT64 iSeconds, CString& strOut )
{
    struct tm oTM;
    ::memset ( &oTM, 0, sizeof(oTM) );
#ifdef _WIN32
    const __time64_t tValue = (__time64_t)iSeconds;
    if ( _localtime64_s ( &oTM, &tValue ) != 0 )
      return false;
#else
    const time_t tValue = (time_t)iSeconds;
    if ( ::localtime_r ( &tValue, &oTM ) == nullptr )
      return false;
#endif
    strOut.Format ( _T("%04d-%02d-%02dT%02d:%02d:%02d")
                  , oTM.tm_year + 1900, oTM.tm_mon + 1, oTM.tm_mday
                  , oTM.tm_hour, oTM.tm_min, oTM.tm_sec );
    return true;
}

//
//  Formats the sixteen bytes of a GUID in the canonical braced form.
//  NOTES: The bytes are COPIED out before they are read as a GUID. The payload
//         sits inside a #pragma pack(1) image at whatever offset the block walk
//         landed on, so reading it through a GUID* asserts an alignment the
//         address does not have - the same undefined behaviour c_vBlob()
//         documents at length in P2Pmsg.h.
//
static void
MsgPrint_FormatGUID ( const void *pvGUID, CString& strOut )
{
    unsigned char cGUID[16];
    ::memcpy ( cGUID, pvGUID, sizeof(cGUID) );

    //  Data1/Data2/Data3 are stored little-endian, Data4 as bytes - which is
    //  what makes the first three groups a byte reversal and the last two not.
    strOut.Format ( _T("{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}")
                  , cGUID[3],  cGUID[2],  cGUID[1],  cGUID[0]
                  , cGUID[5],  cGUID[4]
                  , cGUID[7],  cGUID[6]
                  , cGUID[8],  cGUID[9]
                  , cGUID[10], cGUID[11], cGUID[12], cGUID[13], cGUID[14], cGUID[15] );
}

//
//  UTF-16 to UTF-8, written out rather than delegated to WideCharToMultiByte.
//  NOTES: Save() is the one place this component touches the outside world, and
//         a hand-rolled encoder is what lets it do so identically on both
//         targets. Surrogate pairs are combined; an unpaired surrogate becomes
//         U+FFFD, because emitting it raw produces a byte sequence that is not
//         UTF-8 and that a strict reader rejects.
//
static void
MsgPrint_ToUTF8 ( LPCTSTR lpszText, std::vector<char>& oOut )
{
    if ( lpszText == nullptr )
      return;
    for ( const TCHAR *p = lpszText; *p; ++p )
    {
      //  Cast through the character type itself, not through unsigned short: on
      //  the Linux target wchar_t is 32 bits and narrowing it here would lose
      //  every character above the BMP. The surrogate arms below are then dead
      //  code there, which is correct - a 32-bit code unit IS the code point,
      //  and a lone surrogate is not a valid one on either target.
      unsigned int uCode = (unsigned int)*p;
      if ( uCode >= 0xD800u && uCode <= 0xDBFFu )
      {
        const unsigned int uLow = (unsigned int)*(p+1);
        if ( uLow >= 0xDC00u && uLow <= 0xDFFFu )
        {
          uCode = 0x10000u + ((uCode - 0xD800u) << 10) + (uLow - 0xDC00u);
          ++p;
        }
        else
          uCode = 0xFFFDu;             // Unpaired high surrogate
      }
      else if ( uCode >= 0xDC00u && uCode <= 0xDFFFu )
        uCode = 0xFFFDu;               // Unpaired low surrogate

      if ( uCode < 0x80u )
        oOut.push_back ( (char)uCode );
      else if ( uCode < 0x800u )
      {
        oOut.push_back ( (char)(0xC0u | (uCode >> 6)) );
        oOut.push_back ( (char)(0x80u | (uCode & 0x3Fu)) );
      }
      else if ( uCode < 0x10000u )
      {
        oOut.push_back ( (char)(0xE0u | (uCode >> 12)) );
        oOut.push_back ( (char)(0x80u | ((uCode >> 6) & 0x3Fu)) );
        oOut.push_back ( (char)(0x80u | (uCode & 0x3Fu)) );
      }
      else
      {
        oOut.push_back ( (char)(0xF0u | (uCode >> 18)) );
        oOut.push_back ( (char)(0x80u | ((uCode >> 12) & 0x3Fu)) );
        oOut.push_back ( (char)(0x80u | ((uCode >> 6) & 0x3Fu)) );
        oOut.push_back ( (char)(0x80u | (uCode & 0x3Fu)) );
      }
    }
}

///////////////////////////////////////////////////////////////////////
//  Constructors and destructor

MsgPrint::MsgPrint ( ) noexcept
{
}

MsgPrint::MsgPrint ( const MsgPrint& rhs )
{
    *this = rhs;
}

MsgPrint::~MsgPrint ( )
{
}

///////////////////////////////////////////////////////////////////////
//  Operators

MsgPrint&
MsgPrint::operator = ( const MsgPrint& rhs )
{
    if ( this == &rhs )
      return *this;
    m_strText     = rhs.m_strText;
    m_nIndent     = rhs.m_nIndent;
    m_bAttributes = rhs.m_bAttributes;
    m_nDepthMax   = rhs.m_nDepthMax;
    return *this;
}

MsgPrint::operator LPCTSTR ( ) const noexcept
{
    return (LPCTSTR)m_strText;
}

///////////////////////////////////////////////////////////////////////
//  Text exposure

LPCTSTR
MsgPrint::c_str ( ) const noexcept
{
    return (LPCTSTR)m_strText;
}

const CString&
MsgPrint::r_text ( ) const noexcept
{
    return m_strText;
}

int
MsgPrint::GetLength ( ) const noexcept
{
    return m_strText.GetLength ( );
}

bool
MsgPrint::IsEmpty ( ) const noexcept
{
    return m_strText.IsEmpty ( ) ? true : false;
}

void
MsgPrint::Clear ( ) noexcept
{
    m_strText.Empty ( );
}

///////////////////////////////////////////////////////////////////////
//  Output

void
MsgPrint::Print ( FILE *fd ) const
{
    if ( fd == nullptr || m_strText.IsEmpty() )
      return;
    _fputts ( (LPCTSTR)m_strText, fd );
}

//
//  Writes the rendered document to a file as UTF-8.
//
//  Parameters:  LPCTSTR lpszFilename
//               Destination path, created or truncated
//
//               bool bBOM
//               Prefix the byte-order mark
//
//  Returns:     BOOL
//                 TRUE... Written
//                 FALSE.. Nothing rendered, or the file could not be written
//
BOOL
MsgPrint::Save ( LPCTSTR lpszFilename, bool bBOM ) const
{
    if ( lpszFilename == nullptr || *lpszFilename == 0 )
      return FALSE;

    std::vector<char> oBytes;
    if ( bBOM )
    {
      //  Written as char literals rather than cast from 0xEF: char is signed
      //  here, so (char)0xEF is a narrowing of a positive constant and W4 says
      //  so (C4310). The bytes are identical either way.
      oBytes.push_back ( '\xEF' );
      oBytes.push_back ( '\xBB' );
      oBytes.push_back ( '\xBF' );
    }
    MsgPrint_ToUTF8 ( (LPCTSTR)m_strText, oBytes );

    //  BINARY, not text. The document already carries the line endings the
    //  dialect chose; text mode would translate them a second time and a PHP
    //  file served from Windows would arrive with CRCRLF.
    FILE *fd = nullptr;
#ifdef _WIN32
    if ( _tfopen_s ( &fd, lpszFilename, _T("wb") ) != 0 || fd == nullptr )
      return FALSE;
#else
    std::vector<char> oName;
    MsgPrint_ToUTF8 ( lpszFilename, oName );
    oName.push_back ( 0 );
    fd = ::fopen ( &oName[0], "wb" );
    if ( fd == nullptr )
      return FALSE;
#endif

    const size_t nBytes   = oBytes.size ( );
    const size_t nWritten = nBytes ? ::fwrite ( &oBytes[0], 1, nBytes, fd ) : 0;
    ::fclose ( fd );
    return nWritten == nBytes ? TRUE : FALSE;
}

///////////////////////////////////////////////////////////////////////
//  Properties

int
MsgPrint::SetIndent ( int nSpaces ) noexcept
{
    const int nPrevious = m_nIndent;
    m_nIndent = nSpaces < 0 ? 0 : nSpaces;
    return nPrevious;
}

int
MsgPrint::GetIndent ( ) const noexcept
{
    return m_nIndent;
}

bool
MsgPrint::SetAttributes ( bool bRender ) noexcept
{
    const bool bPrevious = m_bAttributes;
    m_bAttributes = bRender;
    return bPrevious;
}

bool
MsgPrint::GetAttributes ( ) const noexcept
{
    return m_bAttributes;
}

int
MsgPrint::SetDepthMax ( int nDepthMax ) noexcept
{
    const int nPrevious = m_nDepthMax;
    m_nDepthMax = nDepthMax < 1 ? 1 : nDepthMax;
    return nPrevious;
}

int
MsgPrint::GetDepthMax ( ) const noexcept
{
    return m_nDepthMax;
}

///////////////////////////////////////////////////////////////////////
//  Scalar rendering

//
//  Renders one P3PmsgData cell as unescaped text plus the kind the dialect
//  should punctuate it as.
//
//  Parameters:  P3PmsgData& oData
//               Cell to render
//
//               CString& strOut
//               Receives the text; emptied for Value_Null
//
//  Returns:     ValueKind_e
//               How the text is to be punctuated
//
MsgPrint::ValueKind_e
MsgPrint::RenderScalar ( P3PmsgData& oData, CString& strOut ) const
{
    strOut.Empty ( );
    try
    {
      if ( oData.IsNull() )
        return Value_Null;

      switch ( oData.DataType ( ) )
      {
        case VBLockData_NULL:
          return Value_Null;

        case VBLockData_BOOL:
          strOut = oData.c_bool ( ) ? _T("true") : _T("false");
          return Value_Bool;

        //  Every integer width through one accessor. ReadAnyInt is the only
        //  reader that does not throw on a width or sign mismatch, and the
        //  unsigned subtypes have no dedicated public accessor at all - which
        //  is exactly why it exists.
        case VBLockData_INT08:  case VBLockData_UINT08:
        case VBLockData_INT16:  case VBLockData_UINT16:
        case VBLockData_INT32:  case VBLockData_UINT32:
        case VBLockData_INT64:  case VBLockData_UINT64:
        {
          INT64 iValue    = 0;
          bool  bUnsigned = false;
          if ( !oData.ReadAnyInt ( iValue, bUnsigned ) )
            return Value_Null;
          //  %llu / %lld rather than MSVC's %I64u / %I64i: this file compiles
          //  on the Linux target too, where the I64 length modifier is not a
          //  format specifier at all. MSVC has understood ll since VS2015.
          if ( bUnsigned )
            strOut.Format ( _T("%llu"), (unsigned long long)iValue );
          else
            strOut.Format ( _T("%lld"), (long long)iValue );
          return Value_Number;
        }

        case VBLockData_FLOAT:
          return MsgPrint_FormatReal ( (double)oData.c_float(), true, strOut )
               ? Value_Number : Value_Null;

        case VBLockData_DOUBLE:
          return MsgPrint_FormatReal ( oData.c_double(), false, strOut )
               ? Value_Number : Value_Null;

        //  TIME32 reads through c_time, which accepts the TIME32 tag and the
        //  untagged UINT32; TIME64 through c_time64, which accepts TIME64 and
        //  INT64. Both are seconds, and both become a string rather than the
        //  bare number they are stored as - a number would be indistinguishable
        //  from a count.
        case VBLockData_TIME32:
          return MsgPrint_FormatEpoch ( (INT64)(UINT32)oData.c_time(), strOut )
               ? Value_String : Value_Null;

        case VBLockData_TIME64:
          return MsgPrint_FormatEpoch ( (INT64)oData.c_time64(), strOut )
               ? Value_String : Value_Null;

        case VBLockData_WCHAR:
        {
          const wchar_t wcValue = oData.c_wchar ( );
          if ( wcValue )
            strOut += (TCHAR)wcValue;
          return Value_String;
        }

        case VBLockData_GUID:
        {
          const void *pvGUID = oData.c_vGUID ( );
          if ( pvGUID == nullptr )
            return Value_Null;
          MsgPrint_FormatGUID ( pvGUID, strOut );
          return Value_String;
        }

        //  Narrow strings arrive as bytes in the store's own code page; CString
        //  widens them, which is what P3PmsgData::ToString does with the same
        //  cells and the only conversion the format defines.
        case VBLockData_BSTR08:    case VBLockData_BSTR08var:
        case VBLockData_BSTR16:    case VBLockData_BSTR16var:
        case VBLockData_BSTR32:    case VBLockData_BSTR32var:
        {
          LPCSTR lpszValue = oData.c_str ( );
          if ( lpszValue )
            strOut = CString ( lpszValue );
          return Value_String;
        }

        case VBLockData_WSTR08:    case VBLockData_WSTR08var:
        case VBLockData_WSTR16:    case VBLockData_WSTR16var:
        case VBLockData_WSTR32:    case VBLockData_WSTR32var:
        {
          LPCWSTR lpszValue = oData.c_wstr ( );
          if ( lpszValue )
            strOut = lpszValue;
          return Value_String;
        }

        //  Base64, and read through c_vBlobCopy rather than c_vBlob: the
        //  payload begins at an arbitrary offset inside a packed image, so the
        //  bytes are copied somewhere aligned before anything reads them.
        case VBLockData_BLOB08:    case VBLockData_BLOB08var:
        case VBLockData_BLOB16:    case VBLockData_BLOB16var:
        case VBLockData_BLOB32:    case VBLockData_BLOB32var:
        {
          const size_t nBytes = oData.c_size ( );
          if ( nBytes == 0 )
            return Value_String;       // Present and empty, which is not null
          std::vector<unsigned char> oBlob ( nBytes );
          const size_t nRead = oData.c_vBlobCopy ( &oBlob[0], nBytes );
          strOut = Base64 ( &oBlob[0], nRead );
          return Value_String;
        }

        default:
          //  A type byte this build does not implement. Reported as an empty
          //  cell rather than guessed at: the byte came off the wire or off
          //  disk, and a renderer is not the place to decide what an unknown
          //  layout meant.
          return Value_Null;
      }
    }
    //  A throw here means the cell disagreed with its own type byte, which a
    //  well-formed store cannot do. Cancel() releases the event - the document
    //  reports one empty cell and the render continues, because abandoning the
    //  whole document over one cell helps nobody.
    catch_pP2Pevent_Cancel
    catch_ALL_Cancel
    strOut.Empty ( );
    return Value_Null;
}

CString
MsgPrint::TypeName ( P3PmsgData& oData )
{
    CString strType;
    try
    {
      LPCTSTR lpszType = oData.ToStringType ( );
      if ( lpszType )
        strType = lpszType;
      return strType;
    }
    catch_pP2Pevent_Cancel
    catch_ALL_Cancel
    strType.Empty ( );
    return strType;
}

///////////////////////////////////////////////////////////////////////
//  Text assembly

void
MsgPrint::Append ( LPCTSTR lpszText )
{
    if ( lpszText )
      m_strText += lpszText;
}

void
MsgPrint::AppendBreak ( int nDepth )
{
    if ( m_nIndent <= 0 )
      return;                          // Compact: one line, no padding
    m_strText += _T('\n');
    for ( int i = 0; i < nDepth * m_nIndent; i++ )
      m_strText += _T(' ');
}

//
//  Standard base64 of a byte run, with padding.
//
CString
MsgPrint::Base64 ( const unsigned char *pcBytes, size_t nBytes )
{
    static const TCHAR szAlphabet[] =
      _T("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");

    CString strOut;
    if ( pcBytes == nullptr || nBytes == 0 )
      return strOut;

    for ( size_t i = 0; i < nBytes; i += 3 )
    {
      const size_t       nRun  = (nBytes - i) < 3 ? (nBytes - i) : 3;
      const unsigned int uTrip = ((unsigned int)pcBytes[i] << 16)
                               | (nRun > 1 ? ((unsigned int)pcBytes[i+1] << 8) : 0u)
                               | (nRun > 2 ?  (unsigned int)pcBytes[i+2]       : 0u);
      strOut += szAlphabet[(uTrip >> 18) & 0x3Fu];
      strOut += szAlphabet[(uTrip >> 12) & 0x3Fu];
      strOut += nRun > 1 ? szAlphabet[(uTrip >> 6) & 0x3Fu] : _T('=');
      strOut += nRun > 2 ? szAlphabet[ uTrip       & 0x3Fu] : _T('=');
    }
    return strOut;
}
