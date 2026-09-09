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
#include <errno.h>
#include <stdlib.h>
#include <vector>

#include "MsgPrint.h"
#include "MsgValue.h"

#include "../Msgcore/Msgexception.h"
#include "../Msgcore/P2PmsgVBLock.h"

///////////////////////////////////////////////////////////////////////
//  File-local helpers

//  Defined with the rest of the parse-side helpers below. Declared here because
//  the round-trip check in MsgPrint_FormatReal needs it, and because there is
//  no _tcstod to use instead: the Platform shim maps _tcstol and stops there.
static bool
MsgPrint_ParseReal ( LPCTSTR lpszText, double& dOut );

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
      double dBack = 0.0;
      if ( !MsgPrint_ParseReal ( (LPCTSTR)strOut, dBack ) || dBack != dValue )
        strOut.Format ( _T("%.17g"), dValue );
    }

    //  The decimal point is the C runtime's, and a host that has called
    //  setlocale with a comma locale would otherwise emit 1,5 - which is two
    //  JSON tokens and one PHP parse error.
    //  Rebuilt rather than patched in place: CString::Replace and SetAt are
    //  both MFC's, and the Platform shim carries neither. GetAt and += are
    //  what the shim does have, and they are what the render path already uses.
    CString strFixed;
    for ( int i = 0; i < strOut.GetLength ( ); i++ )
    {
      const TCHAR c = strOut.GetAt ( i );
      strFixed += c == _T(',') ? _T('.') : c;
    }
    strOut = strFixed;
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
    //  _fputts is MSVC's; the Platform shim does not map it, and off Windows
    //  TCHAR is wchar_t, so the wide CRT call IS the same call.
#ifdef _WIN32
    _fputts ( (LPCTSTR)m_strText, fd );
#else
    ::fputws ( (LPCTSTR)m_strText, fd );
#endif
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

///////////////////////////////////////////////////////////////////////
//  File-local helpers - parsing

//  MSVC deprecates sscanf and this project builds warnings-as-errors, so the
//  secure name is used where there is one. The two are INTERCHANGEABLE HERE and
//  only here: sscanf_s differs from sscanf solely in taking a buffer size after
//  every %s, %c and %[ conversion, and every conversion below is numeric.
#ifdef _WIN32
  #define MsgPrint_sscanf sscanf_s
#else
  #define MsgPrint_sscanf sscanf
#endif

//
//  Decodes UTF-8 into wide text: the inverse of MsgPrint_ToUTF8, and paired
//  with it deliberately, because a document Save writes and Load reads back has
//  to be the same document.
//  NOTES: A malformed sequence becomes U+FFFD and the walk resynchronises on
//         the very next byte. It does NOT fail: a parser is about to be pointed
//         at this text and will report where it stopped, whereas a decoder that
//         threw would report the failure a whole layer away from the line that
//         caused it.
//
static void
MsgPrint_FromUTF8 ( const char *pcBytes, size_t nBytes, CString& strOut )
{
    strOut.Empty ( );
    if ( pcBytes == nullptr )
      return;

    size_t i = 0;
    while ( i < nBytes )
    {
      const unsigned int uLead = (unsigned char)pcBytes[i];
      unsigned int       uCode = 0xFFFDu;
      size_t             nSeq  = 1;

      if      ( uLead < 0x80u )          { nSeq = 1; uCode = uLead;        }
      else if ( (uLead & 0xE0u) == 0xC0u ) { nSeq = 2; uCode = uLead & 0x1Fu; }
      else if ( (uLead & 0xF0u) == 0xE0u ) { nSeq = 3; uCode = uLead & 0x0Fu; }
      else if ( (uLead & 0xF8u) == 0xF0u ) { nSeq = 4; uCode = uLead & 0x07u; }
      else
      {
        //  A continuation byte, or a lead byte outside the four legal forms.
        strOut += (TCHAR)0xFFFD;
        i++;
        continue;
      }

      if ( nSeq > 1 )
      {
        if ( i + nSeq > nBytes )
        {
          strOut += (TCHAR)0xFFFD;     // Truncated at the end of the file
          break;
        }
        bool bValid = true;
        for ( size_t k = 1; k < nSeq; k++ )
        {
          const unsigned int uCont = (unsigned char)pcBytes[i+k];
          if ( (uCont & 0xC0u) != 0x80u )
          {
            bValid = false;
            break;
          }
          uCode = (uCode << 6) | (uCont & 0x3Fu);
        }
        if ( !bValid )
        {
          strOut += (TCHAR)0xFFFD;
          i++;
          continue;
        }
        if ( uCode > 0x10FFFFu || (uCode >= 0xD800u && uCode <= 0xDFFFu) )
          uCode = 0xFFFDu;             // Out of range, or an encoded surrogate
      }
      i += nSeq;

      //  A surrogate pair where TCHAR is 16 bits and nothing at all where it is
      //  32 - the exact mirror of the encoder, whose surrogate arms are dead
      //  code on that same target for the same reason.
      if ( uCode >= 0x10000u && sizeof(TCHAR) == 2 )
      {
        const unsigned int uRel = uCode - 0x10000u;
        strOut += (TCHAR)(0xD800u + (uRel >> 10));
        strOut += (TCHAR)(0xDC00u + (uRel & 0x3FFu));
      }
      else
        strOut += (TCHAR)uCode;
    }
}

//
//  Copies ASCII text into a narrow buffer, refusing anything above 0x7F.
//  NOTES: Every text this is used on is ASCII BY CONSTRUCTION - a number
//         literal, an ISO-8601 timestamp, a GUID. Narrowing them is what lets
//         the conversions below use strtoll / strtod / sscanf, which exist
//         identically on both targets; their wide equivalents do not. The
//         Platform shim maps _tcstol and stops there, and there is no _ttoi64
//         on the Linux side at all.
//
static bool
MsgPrint_Narrow ( LPCTSTR lpszText, char *pcOut, size_t nMax )
{
    if ( lpszText == nullptr || pcOut == nullptr || nMax == 0 )
      return false;
    size_t i = 0;
    for ( ; lpszText[i] && i < nMax - 1; i++ )
    {
      const unsigned int uChar = (unsigned int)lpszText[i];
      if ( uChar > 0x7Fu )
        return false;
      pcOut[i] = (char)uChar;
    }
    if ( lpszText[i] )
      return false;                    // Longer than the buffer, so not a number
    pcOut[i] = 0;
    return true;
}

//
//  Text to INT64, refusing anything the whole of which is not an integer.
//  NOTES: The endptr check is the point. strtoll stops at the first character
//         it cannot use and reports success for the prefix, so "12abc" would
//         otherwise become 12 - and a document that says 12abc is a document
//         this library should refuse rather than round off.
//
static bool
MsgPrint_ParseInt ( LPCTSTR lpszText, INT64& iOut )
{
    char acText[64];
    if ( !MsgPrint_Narrow ( lpszText, acText, sizeof(acText) ) || acText[0] == 0 )
      return false;

    char     *pcEnd  = nullptr;
    errno = 0;
    const long long llValue = ::strtoll ( acText, &pcEnd, 10 );
    if ( pcEnd == acText || *pcEnd != 0 || errno == ERANGE )
      return false;
    iOut = (INT64)llValue;
    return true;
}

static bool
MsgPrint_ParseReal ( LPCTSTR lpszText, double& dOut )
{
    char acText[64];
    if ( !MsgPrint_Narrow ( lpszText, acText, sizeof(acText) ) || acText[0] == 0 )
      return false;

    char *pcEnd = nullptr;
    errno = 0;
    const double dValue = ::strtod ( acText, &pcEnd );
    if ( pcEnd == acText || *pcEnd != 0 )
      return false;
    dOut = dValue;
    return true;
}

//
//  ISO-8601 local time back to seconds - the inverse of MsgPrint_FormatEpoch,
//  and it has to read exactly what that writes.
//  NOTES: tm_isdst = -1 asks the CRT to work out the offset for that local
//         date, which is what makes the round trip land on the same second
//         across a daylight-saving boundary. Zero would assert standard time
//         and shift half the year by an hour.
//
static bool
MsgPrint_ParseEpoch ( LPCTSTR lpszText, INT64& iSeconds )
{
    char acText[64];
    if ( !MsgPrint_Narrow ( lpszText, acText, sizeof(acText) ) )
      return false;

    int nYear = 0, nMon = 0, nDay = 0, nHour = 0, nMin = 0, nSec = 0;
    if ( MsgPrint_sscanf ( acText, "%d-%d-%dT%d:%d:%d"
                  , &nYear, &nMon, &nDay, &nHour, &nMin, &nSec ) != 6 )
      return false;

    struct tm oTM;
    ::memset ( &oTM, 0, sizeof(oTM) );
    oTM.tm_year  = nYear - 1900;
    oTM.tm_mon   = nMon - 1;
    oTM.tm_mday  = nDay;
    oTM.tm_hour  = nHour;
    oTM.tm_min   = nMin;
    oTM.tm_sec   = nSec;
    oTM.tm_isdst = -1;

#ifdef _WIN32
    const __time64_t tValue = ::_mktime64 ( &oTM );
#else
    const time_t     tValue = ::mktime ( &oTM );
#endif
    if ( tValue == (decltype(tValue))-1 )
      return false;
    iSeconds = (INT64)tValue;
    return true;
}

//
//  {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} back to sixteen bytes, in the byte
//  order MsgPrint_FormatGUID wrote them: the first three groups reversed
//  because Data1/2/3 are stored little-endian, the last two straight through
//  because Data4 is stored as bytes.
//
static bool
MsgPrint_ParseGUID ( LPCTSTR lpszText, unsigned char *pcGUID )
{
    char acText[64];
    if ( !MsgPrint_Narrow ( lpszText, acText, sizeof(acText) ) )
      return false;

    unsigned int auByte[16];
    const char  *pcRead = acText;
    if ( *pcRead == '{' )
      pcRead++;                        // The braces the formatter writes
    if ( MsgPrint_sscanf ( pcRead
                  , "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"
                  , &auByte[0],  &auByte[1],  &auByte[2],  &auByte[3]
                  , &auByte[4],  &auByte[5]
                  , &auByte[6],  &auByte[7]
                  , &auByte[8],  &auByte[9]
                  , &auByte[10], &auByte[11], &auByte[12]
                  , &auByte[13], &auByte[14], &auByte[15] ) != 16 )
      return false;

    pcGUID[3]  = (unsigned char)auByte[0];
    pcGUID[2]  = (unsigned char)auByte[1];
    pcGUID[1]  = (unsigned char)auByte[2];
    pcGUID[0]  = (unsigned char)auByte[3];
    pcGUID[5]  = (unsigned char)auByte[4];
    pcGUID[4]  = (unsigned char)auByte[5];
    pcGUID[7]  = (unsigned char)auByte[6];
    pcGUID[6]  = (unsigned char)auByte[7];
    for ( int i = 8; i < 16; i++ )
      pcGUID[i] = (unsigned char)auByte[i];
    return true;
}

///////////////////////////////////////////////////////////////////////
//  Input

void
MsgPrint::SetText ( LPCTSTR lpszText )
{
    m_strText = lpszText ? lpszText : _T("");
    ClearError ( );
}

//
//  Reads lpszFilename into the held text as UTF-8.
//
//  Parameters:  LPCTSTR lpszFilename
//               File to read
//
//  Returns:     BOOL
//               TRUE when the file was opened and read to the end
//
BOOL
MsgPrint::Load ( LPCTSTR lpszFilename )
{
    if ( lpszFilename == nullptr || *lpszFilename == 0 )
      return FALSE;

    //  BINARY, as Save writes it. Text mode would eat the CR of every CRLF and
    //  a document whose line endings the dialect chose would come back with
    //  different ones.
    FILE *fd = nullptr;
#ifdef _WIN32
    if ( _tfopen_s ( &fd, lpszFilename, _T("rb") ) != 0 || fd == nullptr )
      return FALSE;
#else
    std::vector<char> oName;
    MsgPrint_ToUTF8 ( lpszFilename, oName );
    oName.push_back ( 0 );
    fd = ::fopen ( &oName[0], "rb" );
    if ( fd == nullptr )
      return FALSE;
#endif

    std::vector<char> oBytes;
    char              acChunk[4096];
    size_t            nRead = 0;
    while ( (nRead = ::fread ( acChunk, 1, sizeof(acChunk), fd )) > 0 )
      oBytes.insert ( oBytes.end ( ), acChunk, acChunk + nRead );
    const bool bFailed = ::ferror ( fd ) != 0;
    ::fclose ( fd );
    if ( bFailed )
      return FALSE;

    //  The BOM Save may have written, dropped whether or not one was expected.
    //  It is an encoding mark and not the first character of the document, and
    //  a parser handed one reports a syntax error on column one.
    size_t nStart = 0;
    if ( oBytes.size ( ) >= 3
      && (unsigned char)oBytes[0] == 0xEF
      && (unsigned char)oBytes[1] == 0xBB
      && (unsigned char)oBytes[2] == 0xBF )
      nStart = 3;

    CString strText;
    if ( oBytes.size ( ) > nStart )
      MsgPrint_FromUTF8 ( &oBytes[nStart], oBytes.size ( ) - nStart, strText );
    SetText ( (LPCTSTR)strText );
    return TRUE;
}

///////////////////////////////////////////////////////////////////////
//  Parse status

LPCTSTR
MsgPrint::GetError ( ) const noexcept
{
    return (LPCTSTR)m_strError;
}

int
MsgPrint::GetErrorPos ( ) const noexcept
{
    return m_nErrorPos;
}

int
MsgPrint::GetErrorLine ( ) const noexcept
{
    return m_nErrorLine;
}

void
MsgPrint::ClearError ( ) noexcept
{
    m_strError.Empty ( );
    m_nErrorPos  = -1;
    m_nErrorLine = 0;
}

//
//  Records the first failure of a parse and nothing after it.
//  NOTES: FIRST, not last. A recursive descent unwinds through every level
//         above the failure and each of them would happily overwrite the
//         message with its own, more general one - and "unexpected end of
//         document" is no help to someone whose actual mistake was a missing
//         comma on line 40.
//       : nPos is a character offset into m_strText; the line number is counted
//         from it here rather than tracked by the reader, because a reader that
//         carried a line counter would have to keep it right through every
//         backtrack and this cannot get it wrong.
//
void
MsgPrint::SetError ( LPCTSTR lpszError, int nPos )
{
    if ( !m_strError.IsEmpty ( ) )
      return;

    m_strError  = lpszError ? lpszError : _T("Parse failed");
    m_nErrorPos = nPos;

    if ( nPos < 0 )
    {
      m_nErrorLine = 0;
      return;
    }
    const int nEnd = nPos < m_strText.GetLength ( ) ? nPos : m_strText.GetLength ( );
    m_nErrorLine = 1;
    for ( int i = 0; i < nEnd; i++ )
    {
      if ( m_strText[i] == _T('\n') )
        m_nErrorLine++;
    }
}

///////////////////////////////////////////////////////////////////////
//  Scalar parsing

//
//  The VBLockData byte a Msgcore type name stands for - the inverse of
//  P3PmsgData::ToStringType, which is what TypeName hands out.
//
//  Parameters:  LPCTSTR lpszTypeName
//               Type name as TypeName would have written it
//
//  Returns:     UCHAR
//               The type byte, or VBLockData_NULL for a name it cannot place
//
//  NOTES: The compare is CASE-INSENSITIVE because the two halves of the table
//         it inverts are not spelled alike - ToStringType says "int32" and
//         "WSTR16" - and a reader matching case exactly would work for half the
//         types and fail silently for the rest.
//       : TIME32 has no name in that table at all, so nothing here can produce
//         one. That is the table's limit, not this function's.
//
UCHAR
MsgPrint::TypeByte ( LPCTSTR lpszTypeName )
{
    if ( lpszTypeName == nullptr || *lpszTypeName == 0 )
      return VBLockData_NULL;

    static const struct { LPCTSTR lpszName; UCHAR uType; } aoTable[] =
    {
      { _T("char"),      VBLockData_INT08     },
      { _T("uchar"),     VBLockData_UINT08    },
      { _T("short"),     VBLockData_INT16     },
      { _T("ushort"),    VBLockData_UINT16    },
      { _T("int32"),     VBLockData_INT32     },
      { _T("uint32"),    VBLockData_UINT32    },
      { _T("int64"),     VBLockData_INT64     },
      { _T("uint64"),    VBLockData_UINT64    },
      { _T("float"),     VBLockData_FLOAT     },
      { _T("double"),    VBLockData_DOUBLE    },
      { _T("bool"),      VBLockData_BOOL      },
      { _T("wchar"),     VBLockData_WCHAR     },
      { _T("Time64"),    VBLockData_TIME64    },
      { _T("WSTR08"),    VBLockData_WSTR08    },
      { _T("WSTR08var"), VBLockData_WSTR08var },
      { _T("WSTR16"),    VBLockData_WSTR16    },
      { _T("WSTR16var"), VBLockData_WSTR16var },
      { _T("WSTR32"),    VBLockData_WSTR32    },
      { _T("WSTR32var"), VBLockData_WSTR32var },
      { _T("BSTR08"),    VBLockData_BSTR08    },
      { _T("BSTR08var"), VBLockData_BSTR08var },
      { _T("BSTR16"),    VBLockData_BSTR16    },
      { _T("BSTR16var"), VBLockData_BSTR16var },
      { _T("BSTR32"),    VBLockData_BSTR32    },
      { _T("BSTR32var"), VBLockData_BSTR32var },
      { _T("BLOB08"),    VBLockData_BLOB08    },
      { _T("BLOB08var"), VBLockData_BLOB08var },
      { _T("BLOB16"),    VBLockData_BLOB16    },
      { _T("BLOB16var"), VBLockData_BLOB16var },
      { _T("BLOB32"),    VBLockData_BLOB32    },
      { _T("BLOB32var"), VBLockData_BLOB32var },
      { _T("GUID"),      VBLockData_GUID      },
    };

    const CString strName ( lpszTypeName );
    for ( size_t i = 0; i < sizeof(aoTable)/sizeof(aoTable[0]); i++ )
    {
      if ( strName.CompareNoCase ( aoTable[i].lpszName ) == 0 )
        return aoTable[i].uType;
    }
    return VBLockData_NULL;
}

//
//  RFC 4648 base64 to bytes - the inverse of Base64.
//  NOTES: A character outside the alphabet fails the WHOLE decode rather than
//         being skipped. A blob is bytes at fixed offsets, and a decode that
//         dropped one character would return every subsequent byte shifted -
//         which is not a smaller blob, it is a wrong one.
//
bool
MsgPrint::Base64Decode ( LPCTSTR lpszText, std::vector<unsigned char>& oBytes )
{
    oBytes.clear ( );
    if ( lpszText == nullptr )
      return true;                     // Nothing to decode is not a failure

    unsigned int uAccum = 0;
    int          nBits  = 0;
    for ( const TCHAR *p = lpszText; *p; ++p )
    {
      const TCHAR c = *p;
      if ( c == _T(' ') || c == _T('\t') || c == _T('\r') || c == _T('\n') )
        continue;
      if ( c == _T('=') )
        break;                         // Padding, and the end of the payload

      int nSix = -1;
      if      ( c >= _T('A') && c <= _T('Z') ) nSix = (int)(c - _T('A'));
      else if ( c >= _T('a') && c <= _T('z') ) nSix = (int)(c - _T('a')) + 26;
      else if ( c >= _T('0') && c <= _T('9') ) nSix = (int)(c - _T('0')) + 52;
      else if ( c == _T('+') )                 nSix = 62;
      else if ( c == _T('/') )                 nSix = 63;
      else
      {
        oBytes.clear ( );
        return false;
      }

      uAccum = (uAccum << 6) | (unsigned int)nSix;
      nBits += 6;
      if ( nBits >= 8 )
      {
        nBits -= 8;
        oBytes.push_back ( (unsigned char)((uAccum >> nBits) & 0xFFu) );
      }
    }
    //  Up to four leftover bits are the quantum the encoder padded out. Six or
    //  more means a whole character's worth of payload with no byte to put it
    //  in, which is text that was cut in the middle.
    if ( nBits >= 6 )
    {
      oBytes.clear ( );
      return false;
    }
    return true;
}

//
//  The P3PmsgData a scalar MsgValue describes - the inverse of RenderScalar.
//
//  Parameters:  const MsgValue& oValue
//               Scalar value, carrying the dialect's type hint where it has one
//
//  Returns:     P3PmsgData
//               The cell, by value, ready for DeclareItem or +=
//
P3PmsgData
MsgPrint::ParseScalar ( const MsgValue& oValue )
{
    const CString& strText = oValue.r_text ( );
    const UCHAR    uType   = TypeByte ( oValue.c_type ( ) );

    //  ASKED FOR A TYPE, which is the PHP dialect's docblock talking. It is the
    //  only thing that makes a round trip type-exact: JSON writes 42 and no
    //  reader can tell an int32 from an int64, while the PHP render wrote the
    //  byte's own name on the line above the property.
    if ( uType != VBLockData_NULL && oValue.GetKind ( ) != Value_Null )
    {
      INT64  iValue = 0;
      double dValue = 0.0;
      switch ( uType )
      {
        case VBLockData_BOOL:
          return P3PmsgData ( strText.CompareNoCase ( _T("true") ) == 0 );

        case VBLockData_INT08:  case VBLockData_UINT08:
        case VBLockData_INT16:  case VBLockData_UINT16:
        case VBLockData_INT32:  case VBLockData_UINT32:
        case VBLockData_INT64:  case VBLockData_UINT64:
          if ( !MsgPrint_ParseInt ( (LPCTSTR)strText, iValue ) )
            break;                     // Not the number it claimed to be
          switch ( uType )
          {
            case VBLockData_INT08:  return P3PmsgData ( (INT08) iValue );
            case VBLockData_UINT08: return P3PmsgData ( (UINT08)iValue );
            case VBLockData_INT16:  return P3PmsgData ( (INT16) iValue );
            case VBLockData_UINT16: return P3PmsgData ( (UINT16)iValue );
            case VBLockData_INT32:  return P3PmsgData ( (INT32) iValue );
            case VBLockData_UINT32: return P3PmsgData ( (UINT32)iValue );
            case VBLockData_INT64:  return P3PmsgData ( (INT64) iValue );
            default:                return P3PmsgData ( (UINT64)iValue );
          }

        case VBLockData_FLOAT:
          if ( !MsgPrint_ParseReal ( (LPCTSTR)strText, dValue ) )
            break;
          return P3PmsgData ( (float)dValue );

        case VBLockData_DOUBLE:
          if ( !MsgPrint_ParseReal ( (LPCTSTR)strText, dValue ) )
            break;
          return P3PmsgData ( dValue );

        //  TIME64 is seconds, and INT64 is the tag c_time64 accepts alongside
        //  it - so the VALUE survives even though the tag does not, because
        //  P3PmsgData has no constructor that produces a time tag at all.
        case VBLockData_TIME64:
          if ( !MsgPrint_ParseEpoch ( (LPCTSTR)strText, iValue ) )
            break;
          return P3PmsgData ( (INT64)iValue );

        case VBLockData_WCHAR:
          return P3PmsgData ( strText.IsEmpty ( ) ? (wchar_t)0
                                                  : (wchar_t)strText[0] );

        case VBLockData_GUID:
        {
          unsigned char acGUID[16];
          if ( !MsgPrint_ParseGUID ( (LPCTSTR)strText, acGUID ) )
            break;
          GUID oGUID;
          ::memcpy ( &oGUID, acGUID, sizeof(acGUID) );
          return P3PmsgData ( oGUID );
        }

        case VBLockData_BLOB08: case VBLockData_BLOB08var:
        case VBLockData_BLOB16: case VBLockData_BLOB16var:
        case VBLockData_BLOB32: case VBLockData_BLOB32var:
        {
          std::vector<unsigned char> oBlob;
          if ( !Base64Decode ( (LPCTSTR)strText, oBlob ) )
            break;
          return P3PmsgData ( oBlob.empty ( ) ? nullptr : (const void *)&oBlob[0]
                            , (VBLsize)oBlob.size ( ), uType );
        }

        //  Narrowed through CStringA, which is the inverse of the CString
        //  widening RenderScalar applies to the same cells. Round trips within
        //  the store's own code page and no further, which is the format's
        //  limit rather than this conversion's.
        case VBLockData_BSTR08: case VBLockData_BSTR08var:
        case VBLockData_BSTR16: case VBLockData_BSTR16var:
        case VBLockData_BSTR32: case VBLockData_BSTR32var:
        {
          const CStringA strNarrow ( (LPCTSTR)strText );
          return P3PmsgData ( (LPCSTR)strNarrow, 0, uType );
        }

        case VBLockData_WSTR08: case VBLockData_WSTR08var:
        case VBLockData_WSTR16: case VBLockData_WSTR16var:
        case VBLockData_WSTR32: case VBLockData_WSTR32var:
          return P3PmsgData ( (LPCWSTR)(LPCTSTR)strText, 0, uType );

        default:
          break;                       // Not a byte we can build - infer below
      }
    }

    //  INFERRED, which is all JSON can offer, and where the round trip through
    //  it loses the width the store had: one number type and one string type
    //  mean every integer comes back INT64 and every real comes back double.
    //  NOTHING IS GUESSED FROM THE SHAPE OF A STRING. An ISO-8601-looking
    //  string stays a string, because a store may legitimately hold one as
    //  text and a reader that promoted it would corrupt the cell it was meant
    //  to restore.
    switch ( oValue.GetKind ( ) )
    {
      case Value_Bool:
        return P3PmsgData ( strText.CompareNoCase ( _T("true") ) == 0 );

      case Value_Number:
      {
        //  Integer unless the text says otherwise. A '.', an exponent, or a
        //  magnitude strtoll will not take, all mean a double; everything else
        //  is an integer it would be wrong to widen, because 1 and 1.0 are the
        //  same JSON number and only one of them is what the store held.
        INT64 iValue = 0;
        if ( strText.Find ( _T('.') ) < 0
          && strText.Find ( _T('e') ) < 0
          && strText.Find ( _T('E') ) < 0
          && MsgPrint_ParseInt ( (LPCTSTR)strText, iValue ) )
          return P3PmsgData ( iValue );

        double dValue = 0.0;
        if ( MsgPrint_ParseReal ( (LPCTSTR)strText, dValue ) )
          return P3PmsgData ( dValue );
        return P3PmsgData ( );
      }

      case Value_String:
        return P3PmsgData ( (LPCWSTR)(LPCTSTR)strText );

      case Value_Null:
      default:
        return P3PmsgData ( );
    }
}

///////////////////////////////////////////////////////////////////////
//  Store writing

//
//  The array a node's elements live in - the value itself when it is a bare
//  array, its .items member when the node carries attributes or descendants
//  besides. Null for a node that is not a list or a vector at all.
//
const MsgValue*
MsgPrint::ItemsOf ( const MsgValue& oValue )
{
    if ( oValue.IsArray ( ) )
      return &oValue;
    if ( oValue.IsObject ( ) )
    {
      const MsgValue *pItems = oValue.Find ( MSGPRINT_KEY_ITEMS );
      if ( pItems && pItems->IsArray ( ) )
        return pItems;
    }
    return nullptr;
}

//
//  Which of the three shapes a value describes.
//  NOTES: An array of nothing but scalars is a LIST and an array with a
//         container in it is a VECTOR, because a list holds data entries and
//         nothing else. That rule is not reversible and it is the only one that
//         can produce a list at all: both render as an array, so a VECTOR of
//         plain scalars comes back a list. The alternative - always a vector -
//         would mean no document could ever restore a list, which is the more
//         common of the two by far.
//       : An EMPTY array is a list for the same reason. Nothing in it says
//         otherwise, and a list is the cheaper structure.
//       : UNLESS THE DOCUMENT SAID SO OUTRIGHT. The PHP render writes list or
//         vector into the docblock over $_items, so a PHP document does not
//         need the guess and does not get it - which is the second thing, after
//         the type names, that makes that round trip the more faithful one.
//
MsgPrint::NodeKind_e
MsgPrint::Classify ( const MsgValue& oValue ) const
{
    const MsgValue *pItems = ItemsOf ( oValue );
    if ( pItems == nullptr )
      return Node_Item;
    if ( oValue.GetVectorHint ( ) )
      return Node_Vect;

    for ( size_t i = 0; i < pItems->GetCount ( ); i++ )
    {
      if ( !pItems->r_child ( i ).IsScalar ( ) )
        return Node_Vect;
    }
    return Node_List;
}

//
//  Everything a node carries EXCEPT the elements of a list or a vector - its
//  own value, its attributes and its descendants. The elements are left to the
//  concrete type, which is the only thing that can take them.
//
void
MsgPrint::FillNode ( P3PmsgItem& oNode, const MsgValue& oValue, int nDepth )
{
    if ( !m_strError.IsEmpty ( ) )
      return;
    if ( nDepth > m_nDepthMax )
    {
      //  The ceiling, not a structural case. A document nested this deep was
      //  not written by the renderer, and an unbounded descent on one is a
      //  stack overflow rather than a diagnostic.
      SetError ( _T("Document nests deeper than the depth ceiling"), -1 );
      return;
    }

    //  A scalar is the whole of the node.
    if ( oValue.IsScalar ( ) )
    {
      oNode = ParseScalar ( oValue );
      return;
    }

    //  A bare array carries no value, attributes or descendants at all - the
    //  elements are its whole content, and the caller has them.
    if ( oValue.IsArray ( ) )
      return;

    for ( size_t i = 0; i < oValue.GetCount ( ) && m_strError.IsEmpty ( ); i++ )
    {
      const CString   strKey  = oValue.c_key ( i );
      const MsgValue& oMember = oValue.r_child ( i );

      //  The node's own value.
      if ( strKey.Compare ( MSGPRINT_KEY_VALUE ) == 0 )
      {
        oNode = ParseScalar ( oMember );
        continue;
      }
      //  The elements, which the caller has already taken.
      if ( strKey.Compare ( MSGPRINT_KEY_ITEMS ) == 0 )
        continue;

      //  An attribute, under its name prefixed with the attribute delimiter -
      //  which no item name may contain, so this test can never misread a
      //  descendant. See MsgPrint.h.
      if ( strKey.GetLength ( ) > 1 && strKey[0] == MSGPRINT_KEY_ATTR )
      {
        if ( !m_bAttributes )
          continue;                    // Rendering them is off, so is reading
        Attach ( oNode.r_Attr ( P3PmsgField::AttrCMD_Create )
               , (LPCTNAM)(LPCTSTR)strKey.Mid ( 1 ), oMember, nDepth + 1 );
        continue;
      }

      //  Everything else is a descendant under its own name.
      Attach ( oNode.r_Desc ( P3PmsgField::AttrCMD_Create )
             , (LPCTNAM)(LPCTSTR)strKey, oMember, nDepth + 1 );
    }
}

//
//  A list: its entries, then everything else a node carries.
//  NOTES: The entries go in FIRST because AddListTail is what gives the block
//         its list shape, and the common parts are appended to a node that is
//         already one.
//
void
MsgPrint::FillList ( P3PmsgList& oList, const MsgValue& oValue, int nDepth )
{
    const MsgValue *pItems = ItemsOf ( oValue );
    if ( pItems )
    {
      for ( size_t i = 0; i < pItems->GetCount ( ); i++ )
        oList.AddListTail ( ParseScalar ( pItems->r_child ( i ) ) );
    }
    FillNode ( oList, oValue, nDepth );
}

//
//  A vector: its elements, each of which goes back through the same decision
//  the caller made about this one, then everything else the node carries.
//
void
MsgPrint::FillVect ( P3PmsgVect& oVect, const MsgValue& oValue, int nDepth )
{
    const MsgValue *pItems = ItemsOf ( oValue );
    if ( pItems )
    {
      for ( size_t i = 0; i < pItems->GetCount ( ) && m_strError.IsEmpty ( ); i++ )
        Attach ( oVect, pItems->r_child ( i ), nDepth + 1 );
    }
    FillNode ( oVect, oValue, nDepth );
}

//
//  Builds oValue DETACHED, under lpszName, and hands it to oDesc.
//  NOTES: Detached and then copied in, rather than declared in place and filled
//         through a navigation, because PushBack takes a RECURSIVE COPY - so
//         the whole subtree lands in the store in one operation and there is
//         never a half-built node visible inside it.
//
void
MsgPrint::Attach ( P3PmsgDesc& oDesc, LPCTNAM lpszName
                 , const MsgValue& oValue, int nDepth )
{
    if ( !m_strError.IsEmpty ( ) )
      return;
    if ( nDepth > m_nDepthMax )
    {
      SetError ( _T("Document nests deeper than the depth ceiling"), -1 );
      return;
    }

    switch ( Classify ( oValue ) )
    {
      case Node_List:
      {
        P3PmsgList oList ( lpszName, P3PmsgData ( ) );
        FillList ( oList, oValue, nDepth );
        oDesc += oList;
        break;
      }
      case Node_Vect:
      {
        //  Zero elements and then InsertAt for each, which appends: the
        //  count the constructor takes is a starting size and not a limit.
        P3PmsgVect oVect ( 0, lpszName, P3PmsgData ( ) );
        FillVect ( oVect, oValue, nDepth );
        oDesc += oVect;
        break;
      }
      case Node_Item:
      default:
      {
        P3PmsgField oField ( lpszName );
        FillNode ( oField, oValue, nDepth );
        oDesc += oField;
        break;
      }
    }
}

//
//  The same, into an attribute set. Attributes are items like any other and
//  may themselves carry attributes and descendants, which is why this is the
//  whole decision again rather than a scalar assignment.
//
void
MsgPrint::Attach ( P3PmsgAttr& oAttr, LPCTNAM lpszName
                 , const MsgValue& oValue, int nDepth )
{
    if ( !m_strError.IsEmpty ( ) )
      return;
    if ( nDepth > m_nDepthMax )
    {
      SetError ( _T("Document nests deeper than the depth ceiling"), -1 );
      return;
    }

    switch ( Classify ( oValue ) )
    {
      case Node_List:
      {
        P3PmsgList oList ( lpszName, P3PmsgData ( ) );
        FillList ( oList, oValue, nDepth );
        oAttr += oList;
        break;
      }
      case Node_Vect:
      {
        //  Zero elements and then InsertAt for each, which appends: the
        //  count the constructor takes is a starting size and not a limit.
        P3PmsgVect oVect ( 0, lpszName, P3PmsgData ( ) );
        FillVect ( oVect, oValue, nDepth );
        oAttr += oVect;
        break;
      }
      case Node_Item:
      default:
      {
        P3PmsgField oField ( lpszName );
        FillNode ( oField, oValue, nDepth );
        oAttr += oField;
        break;
      }
    }
}

//
//  And into a vector, whose elements have no names - which is the only
//  difference between this and the two above.
//
void
MsgPrint::Attach ( P3PmsgVect& oVect, const MsgValue& oValue, int nDepth )
{
    if ( !m_strError.IsEmpty ( ) )
      return;
    if ( nDepth > m_nDepthMax )
    {
      SetError ( _T("Document nests deeper than the depth ceiling"), -1 );
      return;
    }

    switch ( Classify ( oValue ) )
    {
      case Node_List:
      {
        P3PmsgList oList;
        FillList ( oList, oValue, nDepth );
        oVect.InsertAt ( -1, oList );
        break;
      }
      case Node_Vect:
      {
        P3PmsgVect oElem;
        FillVect ( oElem, oValue, nDepth );
        oVect.InsertAt ( -1, oElem );
        break;
      }
      case Node_Item:
      default:
      {
        P3PmsgField oField;
        FillNode ( oField, oValue, nDepth );
        oVect.InsertAt ( -1, oField );
        break;
      }
    }
}

//
//  Writes oValue into oItem, REPLACING oItem's value, attributes and
//  descendants.
//
//  Parameters:  P3PmsgItem& oItem
//               Manager, item, list or vector to write into
//
//               const MsgValue& oValue
//               The document, as the dialect's reader left it
//
//               int nDepth
//               Starting depth, against the ceiling
//
//  NOTES: THE TARGET'S OWN KIND IS NOT CHANGED. A P3PmsgItem cannot become a
//         list in place, so a document whose root is an array needs a target
//         that is already a list or a vector - and says so rather than
//         silently dropping the elements. Every node BELOW the root is created
//         by this parse and takes whatever kind the document asks for.
//       : Truncate() drops attributes, descendants AND STACKS. The stacks go
//         because they are the superseded history of a value that is being
//         replaced wholesale; keeping them would leave the node claiming a
//         past that never led to its present.
//       : The NAME is left alone. A document identifies a node, it does not
//         rename it, and the root name of a store is the store's own.
//
void
MsgPrint::BuildItem ( P3PmsgItem& oItem, const MsgValue& oValue, int nDepth )
{
    const NodeKind_e eKind = Classify ( oValue );
    const bool       bList = oItem.r_Object ( ).IsList ( );
    const bool       bVect = oItem.r_Object ( ).IsVect ( );

    if ( eKind != Node_Item && !bList && !bVect )
    {
      SetError ( _T("The document's root is an array, and the target is ")
                 _T("neither a list nor a vector"), -1 );
      return;
    }

    oItem.Truncate ( );

    if ( bList )
      FillList ( dynamic_cast<P3PmsgList&>(oItem), oValue, nDepth );
    else if ( bVect )
      FillVect ( dynamic_cast<P3PmsgVect&>(oItem), oValue, nDepth );
    else
      FillNode ( oItem, oValue, nDepth );
}
