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
//  MsgValue - the dialect-neutral value tree.
//
#include "stdafx.h"

#include "MsgValue.h"

///////////////////////////////////////////////////////////////////////
//  Constructors and destructor

MsgValue::MsgValue ( ) noexcept
{
}

MsgValue::MsgValue ( MsgPrint::ValueKind_e eKind ) noexcept
    : m_eKind ( eKind )
{
}

MsgValue::~MsgValue ( )
{
    for ( size_t i = 0; i < m_oChildren.size ( ); i++ )
      delete m_oChildren[i];
    m_oChildren.clear ( );
}

///////////////////////////////////////////////////////////////////////
//  Kind

MsgPrint::ValueKind_e
MsgValue::GetKind ( ) const noexcept
{
    return m_eKind;
}

void
MsgValue::SetKind ( MsgPrint::ValueKind_e eKind ) noexcept
{
    m_eKind = eKind;
}

void
MsgValue::SetScalar ( MsgPrint::ValueKind_e eKind, LPCTSTR lpszText )
{
    m_eKind   = eKind;
    m_strText = lpszText ? lpszText : _T("");
}

bool
MsgValue::IsScalar ( ) const noexcept
{
    return m_eKind != MsgPrint::Value_Array
        && m_eKind != MsgPrint::Value_Object;
}

bool
MsgValue::IsArray ( ) const noexcept
{
    return m_eKind == MsgPrint::Value_Array;
}

bool
MsgValue::IsObject ( ) const noexcept
{
    return m_eKind == MsgPrint::Value_Object;
}

///////////////////////////////////////////////////////////////////////
//  Scalar text

LPCTSTR
MsgValue::c_text ( ) const noexcept
{
    return (LPCTSTR)m_strText;
}

const CString&
MsgValue::r_text ( ) const noexcept
{
    return m_strText;
}

///////////////////////////////////////////////////////////////////////
//  Type hint

LPCTSTR
MsgValue::c_type ( ) const noexcept
{
    return (LPCTSTR)m_strType;
}

void
MsgValue::SetType ( LPCTSTR lpszTypeName )
{
    m_strType = lpszTypeName ? lpszTypeName : _T("");
}

///////////////////////////////////////////////////////////////////////
//  Children

size_t
MsgValue::GetCount ( ) const noexcept
{
    return m_oChildren.size ( );
}

//
//  NOTES: Out of range returns a shared empty value rather than throwing. Every
//         caller is walking a tree it has just been handed the size of, so an
//         out-of-range index is a bug in this library and not in the document -
//         and a null-kind value is what the rest of the walk already knows how
//         to write.
//
const MsgValue&
MsgValue::r_child ( size_t nIndex ) const
{
    static const MsgValue oEmpty;
    if ( nIndex >= m_oChildren.size ( ) || m_oChildren[nIndex] == nullptr )
      return oEmpty;
    return *m_oChildren[nIndex];
}

LPCTSTR
MsgValue::c_key ( size_t nIndex ) const
{
    if ( nIndex >= m_oKeys.size ( ) )
      return _T("");
    return (LPCTSTR)m_oKeys[nIndex];
}

MsgValue&
MsgValue::Add ( LPCTSTR lpszKey )
{
    //  The key vector is kept the same length as the child vector even for an
    //  array, whose elements have none. Two vectors that can disagree in length
    //  is one indexing mistake away from reading the wrong name onto a node.
    m_oKeys.push_back ( lpszKey ? CString ( lpszKey ) : CString ( ) );
    m_oChildren.push_back ( new MsgValue );
    return *m_oChildren.back ( );
}

MsgValue*
MsgValue::p_child ( size_t nIndex )
{
    if ( nIndex >= m_oChildren.size ( ) )
      return nullptr;
    return m_oChildren[nIndex];
}

void
MsgValue::AdoptChildren ( MsgValue& oFrom, LPCTSTR lpszKeyPrefix )
{
    if ( &oFrom == this )
      return;
    for ( size_t i = 0; i < oFrom.m_oChildren.size ( ); i++ )
    {
      CString strKey ( lpszKeyPrefix ? lpszKeyPrefix : _T("") );
      strKey += oFrom.m_oKeys[i];
      m_oKeys.push_back ( strKey );
      m_oChildren.push_back ( oFrom.m_oChildren[i] );
    }
    //  Emptied and not deleted: this object owns them now, and letting oFrom's
    //  destructor run over the same pointers is how that becomes a double free.
    oFrom.m_oKeys.clear ( );
    oFrom.m_oChildren.clear ( );
}

void
MsgValue::SetVectorHint ( bool bVector ) noexcept
{
    m_bVectorHint = bVector;
}

bool
MsgValue::GetVectorHint ( ) const noexcept
{
    return m_bVectorHint;
}

const MsgValue*
MsgValue::Find ( LPCTSTR lpszKey ) const
{
    if ( lpszKey == nullptr )
      return nullptr;
    for ( size_t i = 0; i < m_oKeys.size ( ); i++ )
    {
      if ( m_oKeys[i].Compare ( lpszKey ) == 0 )
        return m_oChildren[i];
    }
    return nullptr;
}
