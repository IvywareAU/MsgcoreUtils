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
//  PrintPHP - PHP class declarations from a P3PmsgItem tree. See PrintPHP.h
//  for the mapping and for the constraints PHP source puts on it.
//
//  LAYOUT: this dialect is always line-oriented, unlike the JSON one. The
//  indent controls the WIDTH of a level and nothing else - a PHP file rendered
//  onto one line would put its own banner comment in front of the code it is
//  describing, because // runs to the end of a line and here there would not
//  be one.
//
#include "stdafx.h"

#include "PrintPHP.h"
#include "MsgValue.h"

#include "../Msgcore/Msgexception.h"

//  PSR-12 is four, and this is source rather than payload.
#define PRINTPHP_INDENT_DEFAULT 4

///////////////////////////////////////////////////////////////////////
//  File-local helpers

//
//  PHP reserved words, which a CLASS may not be named after. Property names are
//  unaffected - $list and $class are legal - so this is applied to class names
//  only, and a match is prefixed rather than rejected.
//  NOTES: The list is PHP's reserved words and its predefined class-like
//         keywords together. It is checked case-INSENSITIVELY because PHP
//         class names are.
//
static LPCTSTR const g_lpszPHPReserved[] =
{
    _T("abstract"),   _T("and"),        _T("array"),      _T("as"),
    _T("break"),      _T("callable"),   _T("case"),       _T("catch"),
    _T("class"),      _T("clone"),      _T("const"),      _T("continue"),
    _T("declare"),    _T("default"),    _T("do"),         _T("echo"),
    _T("else"),       _T("elseif"),     _T("empty"),      _T("enddeclare"),
    _T("endfor"),     _T("endforeach"), _T("endif"),      _T("endswitch"),
    _T("endwhile"),   _T("enum"),       _T("extends"),    _T("final"),
    _T("finally"),    _T("fn"),         _T("for"),        _T("foreach"),
    _T("function"),   _T("global"),     _T("goto"),       _T("if"),
    _T("implements"), _T("include"),    _T("instanceof"), _T("insteadof"),
    _T("interface"),  _T("isset"),      _T("list"),       _T("match"),
    _T("namespace"),  _T("new"),        _T("or"),         _T("print"),
    _T("private"),    _T("protected"),  _T("public"),     _T("readonly"),
    _T("require"),    _T("return"),     _T("static"),     _T("switch"),
    _T("throw"),      _T("trait"),      _T("try"),        _T("unset"),
    _T("use"),        _T("var"),        _T("while"),      _T("xor"),
    _T("yield"),      _T("bool"),       _T("false"),      _T("float"),
    _T("int"),        _T("iterable"),   _T("mixed"),      _T("never"),
    _T("null"),       _T("object"),     _T("parent"),     _T("self"),
    _T("string"),     _T("true"),       _T("void"),
};

static bool
PrintPHP_IsReserved ( const CString& strName )
{
    for ( size_t i = 0; i < sizeof(g_lpszPHPReserved)/sizeof(g_lpszPHPReserved[0]); i++ )
      if ( strName.CompareNoCase ( g_lpszPHPReserved[i] ) == 0 )
        return true;
    return false;
}

///////////////////////////////////////////////////////////////////////
//  Constructors and destructor

PrintPHP::PrintPHP ( ) noexcept
{
    m_nIndent = PRINTPHP_INDENT_DEFAULT;
}

PrintPHP::PrintPHP ( LPCTSTR lpszText )
{
    //  The indent first, and for the same reason the default constructor sets
    //  it: MsgPrint initialises m_nIndent to 2 and PHP is written at 4. A
    //  renderer seeded with a document is usually about to be parsed, but
    //  nothing stops it being rendered into afterwards, and it would then
    //  disagree with every other PrintPHP in the process.
    m_nIndent = PRINTPHP_INDENT_DEFAULT;
    //  SetText, not an assignment to m_strText: it is the one place the held
    //  text and the error state are kept in step, and a renderer built from a
    //  document has to start with no error exactly as SetText leaves one.
    SetText ( lpszText );
}

PrintPHP::PrintPHP ( const PrintPHP& rhs )
       : MsgPrint ( rhs )
{
    m_strClassName = rhs.m_strClassName;
    m_bPreamble    = rhs.m_bPreamble;
    m_bPlainObjects = rhs.m_bPlainObjects;
}

PrintPHP::~PrintPHP ( )
{
}

///////////////////////////////////////////////////////////////////////
//  Operators

PrintPHP&
PrintPHP::operator = ( const PrintPHP& rhs )
{
    if ( this == &rhs )
      return *this;
    MsgPrint::operator = ( rhs );
    m_strClassName = rhs.m_strClassName;
    m_bPreamble    = rhs.m_bPreamble;
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Rendering

//
//  Renders oItem and everything beneath it as PHP class declarations,
//  REPLACING whatever this renderer held.
//
//  Parameters:  P3PmsgItem& oItem
//               Manager, item, list or vector to render
//
//  Returns:     MsgPrint&
//               This renderer, so the text can be read from the expression
//
MsgPrint&
PrintPHP::Render ( P3PmsgItem& oItem )
{
    m_strText.Empty ( );
    m_oClassNames.clear ( );
    try
    {
      if ( m_bPreamble )
      {
        CString strBanner;
        strBanner.Format ( _T("<?php\n")
                           _T("//  Generated by MsgcoreUtils PrintPHP - do not edit.\n")
                           _T("//  Source: Msgcore item '%s'  (Msgcore %hs)\n")
                         , oItem.c_name ( )
                         , MSGCORE_VERSION_STRING );
        Append ( (LPCTSTR)strBanner );
        //  Said in the file and not only in the header: a reader who meets
        //  this output with no idea where it came from has to be told that
        //  feeding it back is not a thing that works.
        if ( m_bPlainObjects )
          Append ( _T("//  Plain objects - stdClass and assignments. ")
                   _T("This form does not parse back.\n") );
        Append ( _T("\n") );
      }

      CString strRoot = Sanitise ( m_strClassName.IsEmpty ( )
                                     ? oItem.c_name ( )
                                     : (LPCTSTR)m_strClassName
                                 , _T("Msgcore") );
      if ( PrintPHP_IsReserved ( strRoot ) )
        strRoot = _T("_") + strRoot;
      strRoot = Unique ( strRoot, m_oClassNames );

      if ( m_bPlainObjects )
      {
        //  The root VARIABLE takes the same sanitised name the root CLASS
        //  would have, so one store renders to one recognisable identifier
        //  whichever form was asked for.
        const CString strName = oItem.c_name ( );
        RenderPlain ( oItem, _T("$") + strRoot,
                      strRoot == strName ? nullptr : (LPCTSTR)strName, 0 );
      }
      else
        RenderClass ( oItem, strRoot, 0 );
      return *this;
    }
    //  Half a class declaration is a parse error wherever it is included, and
    //  one that names a line the reader did not write. Empty the buffer so
    //  IsEmpty() is the answer to "did this work".
    catch_pP2Pevent_Cancel
    catch_ALL_Cancel
    m_strText.Empty ( );
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Properties

bool
PrintPHP::SetPlainObjects ( bool bPlain ) noexcept
{
    const bool bPrevious = m_bPlainObjects;
    m_bPlainObjects = bPlain;
    return bPrevious;
}

bool
PrintPHP::GetPlainObjects ( ) const noexcept
{
    return m_bPlainObjects;
}

LPCTSTR
PrintPHP::SetClassName ( LPCTSTR lpszClassName )
{
    m_strClassName = lpszClassName ? lpszClassName : _T("");
    return (LPCTSTR)m_strClassName;
}

LPCTSTR
PrintPHP::GetClassName ( ) const noexcept
{
    return (LPCTSTR)m_strClassName;
}

bool
PrintPHP::SetPreamble ( bool bPreamble ) noexcept
{
    const bool bPrevious = m_bPreamble;
    m_bPreamble = bPreamble;
    return bPrevious;
}

bool
PrintPHP::GetPreamble ( ) const noexcept
{
    return m_bPreamble;
}

///////////////////////////////////////////////////////////////////////
//  Implementation

//
//  Appends the class declarations for oItem: every descendant that needs a
//  class of its own FIRST, then oItem itself.
//
//  Parameters:  P3PmsgItem& oItem
//               Node the class describes
//
//               const CString& strClass
//               Its class name, already sanitised and already unique
//
//               int nDepth
//               Recursion depth, against MsgPrint's ceiling
//
void
PrintPHP::RenderClass ( P3PmsgItem& oItem, const CString& strClass, int nDepth )
{
    if ( nDepth > m_nDepthMax )
      return;

    const CString strPad1 = Pad ( 1 );
    const CString strPad2 = Pad ( 2 );

    CString strProps, strCtor;
    //  Property names taken by THIS class. Seeded with the three synthetic
    //  ones as they are emitted, so a descendant that sanitises to _value gets
    //  the suffix rather than silently overwriting the node's own data.
    std::vector<CString> oMembers;

    const bool bList = oItem.r_Object().IsList ( );
    const bool bVect = oItem.r_Object().IsVect ( );
    const bool bAttr = m_bAttributes && !oItem.r_Attr().IsEmpty ( );

    //  $_value - the node's own data, when it has any.
    CString strScalar;
    if ( RenderScalar ( oItem.r_data(), strScalar ) != Value_Null )
    {
      const CString strMember = Unique ( _T("_value"), oMembers );
      strProps += strPad1;
      strProps += _T("/** @var mixed  ");
      strProps += TypeName ( oItem.r_data() );
      strProps += _T("  (the node's own value) */\n");
      strProps += strPad1;
      strProps += _T("public $") + strMember + _T(" = ")
                + ScalarValue ( oItem.r_data() ) + _T(";\n");
    }

    //  $_items - the elements of a list or a vector.
    if ( bList || bVect )
    {
      const CString strMember = Unique ( _T("_items"), oMembers );
      strProps += strPad1;
      strProps += bList ? _T("/** @var array  (Msgcore list elements) */\n")
                        : _T("/** @var array  (Msgcore vector elements) */\n");
      strProps += strPad1;
      strProps += _T("public $") + strMember + _T(" = ")
                + ( bList ? ListValue ( dynamic_cast<P3PmsgList&>(oItem), 1 )
                          : VectValue ( dynamic_cast<P3PmsgVect&>(oItem), 1 ) )
                + _T(";\n");
    }

    //  $_attributes - the attribute set, as an associative array.
    if ( bAttr )
    {
      const CString strMember = Unique ( _T("_attributes"), oMembers );
      strProps += strPad1;
      strProps += _T("/** @var array  (Msgcore attributes) */\n");
      strProps += strPad1;
      strProps += _T("public $") + strMember + _T(" = ")
                + AttrValue ( oItem.r_Attr(), 1 ) + _T(";\n");
    }

    //  One property per descendant. A descendant that has descendants of its
    //  own becomes a class, declared here and instantiated in __construct;
    //  everything else becomes a constant initialiser.
    P3PmsgDesc& oDesc = oItem.r_Desc ( );
    if ( !oDesc.IsEmpty() )
    {
      P3PmsgCurs& oCurs = oDesc.r_Curs ( );
      for ( int i = 0; oCurs.Goto ( i ); i++ )
      {
        P3PmsgItem&   oChild   = oCurs.r_item ( );
        const CString strName  = oChild.c_name ( );
        const CString strIdent = Sanitise ( (LPCTSTR)strName, _T("item") );
        const CString strProp  = Unique ( strIdent, oMembers );

        if ( !oChild.r_Desc().IsEmpty() )
        {
          CString strChildClass = strClass + _T("_") + strIdent;
          if ( PrintPHP_IsReserved ( strChildClass ) )
            strChildClass = _T("_") + strChildClass;
          strChildClass = Unique ( strChildClass, m_oClassNames );

          //  Before this class, not after: PHP hoists an unconditional class
          //  declaration so the order is not required, but a generated file is
          //  also pasted into larger ones where hoisting no longer applies.
          RenderClass ( oChild, strChildClass, nDepth + 1 );

          strProps += strPad1;
          strProps += _T("/** @var ") + strChildClass
                    + _T("  (Msgcore '") + strName + _T("') */\n");
          strProps += strPad1;
          strProps += _T("public $") + strProp + _T(";\n");

          strCtor  += strPad2;
          strCtor  += _T("$this->") + strProp + _T(" = new ")
                    + strChildClass + _T("();\n");
        }
        else
        {
          //  @var array whenever NodeValue is going to produce an array
          //  literal - a list, a vector, or the associative form a node with
          //  attributes takes. Saying "mixed" there and then emitting an array
          //  would make the docblock the least accurate line in the file, and
          //  a docblock nobody can trust is worse than none.
          const bool  bArray = oChild.r_Object().IsList ( )
                            || oChild.r_Object().IsVect ( )
                            || ( m_bAttributes && !oChild.r_Attr().IsEmpty() );
          strProps += strPad1;
          strProps += _T("/** @var ");
          strProps += bArray ? CString ( _T("array") )
                             : CString ( _T("mixed  ") ) + TypeName ( oChild.r_data() );
          strProps += _T("  (Msgcore '") + strName + _T("') */\n");
          strProps += strPad1;
          strProps += _T("public $") + strProp + _T(" = ")
                    + NodeValue ( oChild, 1 ) + _T(";\n");
        }
      }
    }

    //  The declaration itself.
    Append ( _T("class ") );
    Append ( (LPCTSTR)strClass );
    Append ( _T("\n{\n") );
    if ( strProps.IsEmpty() && strCtor.IsEmpty() )
    {
      Append ( (LPCTSTR)strPad1 );
      Append ( _T("//  The Msgcore node carries no value, attributes or descendants.\n") );
    }
    Append ( (LPCTSTR)strProps );
    if ( !strCtor.IsEmpty() )
    {
      Append ( _T("\n") );
      Append ( (LPCTSTR)strPad1 );
      Append ( _T("public function __construct ( )\n") );
      Append ( (LPCTSTR)strPad1 );
      Append ( _T("{\n") );
      Append ( (LPCTSTR)strCtor );
      Append ( (LPCTSTR)strPad1 );
      Append ( _T("}\n") );
    }
    Append ( _T("}\n\n") );
}

//
//  One node as a run of stdClass assignments under strVar, descendants after
//  the node's own members, depth-first.
//
//  Parameters:  P3PmsgItem& oItem
//               Node to render
//
//               const CString& strVar
//               The whole left-hand side this node is reached by, already
//               folded and already unique inside its parent
//
//               LPCTSTR lpszNote
//               The item's real name, when folding lost it; null otherwise
//
//               int nDepth
//               Recursion depth, against MsgPrint's ceiling
//
//  NOTES: NO DECLARATION ORDER to keep. The class form emits children before
//         their parent because a generated file gets pasted into larger ones
//         where hoisting no longer applies; an assignment has no such rule, so
//         this walk is in store order and reads top down.
//       : THE ARRAY LITERALS ARE THE SAME ONES. NodeValue, ListValue and
//         AttrValue are shared with the class form, so a list, a vector and an
//         attribute set are spelled identically in both - only the thing they
//         are attached to changes.
//       : The three synthetic members keep their names from the class form for
//         the same reason, and are made unique against the descendants here as
//         they are there: a child that folds to _value must not overwrite the
//         node's own data.
//
void
PrintPHP::RenderPlain ( P3PmsgItem& oItem, const CString& strVar,
                        LPCTSTR lpszNote, int nDepth )
{
    if ( nDepth > m_nDepthMax )
      return;

    const bool bList = oItem.r_Object ( ).IsList ( );
    const bool bVect = oItem.r_Object ( ).IsVect ( );
    const bool bAttr = m_bAttributes && !oItem.r_Attr ( ).IsEmpty ( );

    Append ( (LPCTSTR)strVar );
    Append ( _T(" = new stdClass();") );
    if ( lpszNote && *lpszNote )
    {
      //  The one thing this form loses that it can still report. The type is
      //  gone either way - there is nowhere on an assignment to put it.
      Append ( _T("   //  (Msgcore '") );
      Append ( lpszNote );
      Append ( _T("')") );
    }
    Append ( _T("\n") );

    std::vector<CString> oMembers;

    CString strScalar;
    if ( RenderScalar ( oItem.r_data ( ), strScalar ) != Value_Null )
    {
      Append ( (LPCTSTR)( strVar + _T("->") + Unique ( _T("_value"), oMembers )
                        + _T(" = ") + ScalarValue ( oItem.r_data ( ) )
                        + _T(";\n") ) );
    }

    if ( bList || bVect )
    {
      Append ( (LPCTSTR)( strVar + _T("->") + Unique ( _T("_items"), oMembers )
                        + _T(" = ")
                        + ( bList ? ListValue ( dynamic_cast<P3PmsgList&>(oItem), 0 )
                                  : VectValue ( dynamic_cast<P3PmsgVect&>(oItem), 0 ) )
                        + _T(";\n") ) );
    }

    if ( bAttr )
    {
      Append ( (LPCTSTR)( strVar + _T("->")
                        + Unique ( _T("_attributes"), oMembers )
                        + _T(" = ") + AttrValue ( oItem.r_Attr ( ), 0 )
                        + _T(";\n") ) );
    }

    P3PmsgDesc& oDesc = oItem.r_Desc ( );
    if ( oDesc.IsEmpty ( ) )
      return;

    P3PmsgCurs& oCurs = oDesc.r_Curs ( );
    for ( int i = 0; oCurs.Goto ( i ); i++ )
    {
      P3PmsgItem&   oChild   = oCurs.r_item ( );
      const CString strName  = oChild.c_name ( );
      const CString strIdent = Sanitise ( (LPCTSTR)strName, _T("item") );
      const CString strProp  = Unique ( strIdent, oMembers );
      const CString strChild = strVar + _T("->") + strProp;
      LPCTSTR       lpszKept = strProp == strName ? nullptr : (LPCTSTR)strName;

      if ( !oChild.r_Desc ( ).IsEmpty ( ) )
        RenderPlain ( oChild, strChild, lpszKept, nDepth + 1 );
      else
      {
        Append ( (LPCTSTR)( strChild + _T(" = ") + NodeValue ( oChild, 0 )
                          + _T(";") ) );
        if ( lpszKept )
        {
          Append ( _T("   //  (Msgcore '") );
          Append ( lpszKept );
          Append ( _T("')") );
        }
        Append ( _T("\n") );
      }
    }
}

//
//  One node as a PHP VALUE expression. Follows the same shape PrintJson uses -
//  scalar, array, or the associative-array form when the node carries more than
//  a value - so the two dialects describe the same store the same way.
//
CString
PrintPHP::NodeValue ( P3PmsgItem& oItem, int nDepth ) const
{
    if ( nDepth > m_nDepthMax )
      return _T("null");

    const bool bList = oItem.r_Object().IsList ( );
    const bool bVect = oItem.r_Object().IsVect ( );
    const bool bDesc = !oItem.r_Desc().IsEmpty ( );
    const bool bAttr = m_bAttributes && !oItem.r_Attr().IsEmpty ( );

    if ( !bDesc && !bAttr )
    {
      if ( bList )
        return ListValue ( dynamic_cast<P3PmsgList&>(oItem), nDepth );
      if ( bVect )
        return VectValue ( dynamic_cast<P3PmsgVect&>(oItem), nDepth );
      return ScalarValue ( oItem.r_data() );
    }

    //  The structured form. Built entry by entry rather than through a shared
    //  array writer because the keys come from three different places and only
    //  the punctuation is common.
    CString strOut = _T("array(");
    bool    bFirst = true;

    if ( bList || bVect )
    {
      strOut += Pad ( nDepth + 1, bFirst );
      strOut += _T("'") + Escape ( MSGPRINT_KEY_ITEMS ) + _T("' => ");
      strOut += bList ? ListValue ( dynamic_cast<P3PmsgList&>(oItem), nDepth + 1 )
                      : VectValue ( dynamic_cast<P3PmsgVect&>(oItem), nDepth + 1 );
    }
    else
    {
      CString strScalar;
      if ( RenderScalar ( oItem.r_data(), strScalar ) != Value_Null )
      {
        strOut += Pad ( nDepth + 1, bFirst );
        strOut += _T("'") + Escape ( MSGPRINT_KEY_VALUE ) + _T("' => ");
        strOut += ScalarValue ( oItem.r_data() );
      }
    }

    if ( bAttr )
    {
      P3PmsgCurs& oCurs = oItem.r_Attr().r_Curs ( );
      for ( int i = 0; oCurs.Goto ( i ); i++ )
      {
        P3PmsgItem& oAttrItem = oCurs.r_item ( );
        CString     strKey;
        strKey += MSGPRINT_KEY_ATTR;
        strKey += oAttrItem.c_name ( );
        strOut += Pad ( nDepth + 1, bFirst );
        strOut += _T("'") + Escape ( (LPCTSTR)strKey ) + _T("' => ");
        strOut += NodeValue ( oAttrItem, nDepth + 1 );
      }
    }

    if ( bDesc )
    {
      P3PmsgCurs& oCurs = oItem.r_Desc().r_Curs ( );
      for ( int i = 0; oCurs.Goto ( i ); i++ )
      {
        P3PmsgItem& oChild = oCurs.r_item ( );
        strOut += Pad ( nDepth + 1, bFirst );
        strOut += _T("'") + Escape ( oChild.c_name() ) + _T("' => ");
        strOut += NodeValue ( oChild, nDepth + 1 );
      }
    }

    if ( !bFirst && m_nIndent > 0 )
      strOut += _T("\n") + Pad ( nDepth );
    strOut += _T(")");
    return strOut;
}

CString
PrintPHP::ScalarValue ( P3PmsgData& oData ) const
{
    CString strValue;
    switch ( RenderScalar ( oData, strValue ) )
    {
      case Value_Bool:
      case Value_Number:
        return strValue;
      case Value_String:
        return _T("\"") + Escape ( (LPCTSTR)strValue ) + _T("\"");
      case Value_Null:
      default:
        return _T("null");
    }
}

CString
PrintPHP::ListValue ( P3PmsgList& oList, int nDepth ) const
{
    CString strOut = _T("array(");
    bool    bFirst = true;
    VBLaddr aEntry = oList.GetHeadPos ( );
    while ( aEntry )
    {
      P3PmsgData& oEntry = oList.GetNext ( aEntry );
      strOut += Pad ( nDepth + 1, bFirst );
      strOut += ScalarValue ( oEntry );
    }
    if ( !bFirst && m_nIndent > 0 )
      strOut += _T("\n") + Pad ( nDepth );
    strOut += _T(")");
    return strOut;
}

//
//  NOTES: r_item(i) walks the vector's cursor and invalidates the reference the
//         previous call returned, so nothing here holds one across an
//         iteration.
//
CString
PrintPHP::VectValue ( P3PmsgVect& oVect, int nDepth ) const
{
    CString       strOut = _T("array(");
    bool          bFirst = true;
    const VBLelem nCount = oVect.GetCount ( );
    for ( VBLelem i = 0; i < nCount; i++ )
    {
      strOut += Pad ( nDepth + 1, bFirst );
      try
      {
        strOut += NodeValue ( oVect.r_item ( (int)i ), nDepth + 1 );
      }
      //  One element the vector cannot instantiate is one element, not one
      //  document - the same call RenderScalar makes for a cell that disagrees
      //  with its own type byte.
      catch_pP2Pevent_Cancel
      catch_ALL_Cancel
    }
    if ( !bFirst && m_nIndent > 0 )
      strOut += _T("\n") + Pad ( nDepth );
    strOut += _T(")");
    return strOut;
}

CString
PrintPHP::AttrValue ( P3PmsgAttr& oAttr, int nDepth ) const
{
    CString     strOut = _T("array(");
    bool        bFirst = true;
    P3PmsgCurs& oCurs  = oAttr.r_Curs ( );
    for ( int i = 0; oCurs.Goto ( i ); i++ )
    {
      P3PmsgItem& oItem = oCurs.r_item ( );
      strOut += Pad ( nDepth + 1, bFirst );
      strOut += _T("'") + Escape ( oItem.c_name() ) + _T("' => ");
      strOut += NodeValue ( oItem, nDepth + 1 );
    }
    if ( !bFirst && m_nIndent > 0 )
      strOut += _T("\n") + Pad ( nDepth );
    strOut += _T(")");
    return strOut;
}

///////////////////////////////////////////////////////////////////////
//  Layout

CString
PrintPHP::Pad ( int nDepth ) const
{
    CString strPad;
    for ( int i = 0; i < nDepth * m_nIndent; i++ )
      strPad += _T(' ');
    return strPad;
}

//
//  The separator in front of an array entry, which is where the comma lives:
//  nothing before the first, a comma before the rest, and a line break plus
//  padding when the indent asks for one.
//
CString
PrintPHP::Pad ( int nDepth, bool& bFirst ) const
{
    CString strOut;
    if ( !bFirst )
      strOut += _T(",");
    if ( m_nIndent > 0 )
      strOut += _T("\n") + Pad ( nDepth );
    else if ( !bFirst )
      strOut += _T(" ");
    bFirst = false;
    return strOut;
}

///////////////////////////////////////////////////////////////////////
//  Identifier hygiene

//
//  Reduces a Msgcore item name to a PHP identifier.
//  NOTES: Everything outside [A-Za-z0-9_] becomes an underscore, including
//         non-ASCII letters - which PHP would actually accept in a UTF-8 file,
//         but which make an identifier whose spelling depends on the encoding
//         of the file it lands in. A leading digit takes an underscore prefix.
//       : THE ORIGINAL NAME IS NOT LOST. Every property this feeds carries it
//         in the docblock above it, which is the only place it survives once
//         the identifier has been folded.
//
CString
PrintPHP::Sanitise ( LPCTSTR lpszName, LPCTSTR lpszFallback )
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
               uCode == (unsigned int)_T('_')                                        )
          strOut += *p;
        else
          strOut += _T('_');
      }
    }
    if ( strOut.IsEmpty() )
      strOut = lpszFallback ? lpszFallback : _T("item");
    const TCHAR chFirst = strOut[0];
    if ( chFirst >= _T('0') && chFirst <= _T('9') )
      strOut = _T("_") + strOut;
    return strOut;
}

//
//  Appends the smallest numeric suffix that makes strName absent from oTaken,
//  and records the result there.
//  NOTES: The comparison is case-INSENSITIVE, which is stricter than it needs
//         to be for properties and exactly right for classes: PHP class names
//         are case-insensitive, so Address and address are one class and the
//         second declaration is a fatal error. One rule for both is worth more
//         than the two extra property spellings the strict one gives up.
//
CString
PrintPHP::Unique ( const CString& strName, std::vector<CString>& oTaken )
{
    CString strTry = strName;
    for ( int nSuffix = 2; ; nSuffix++ )
    {
      bool bTaken = false;
      for ( size_t i = 0; i < oTaken.size(); i++ )
        if ( oTaken[i].CompareNoCase ( (LPCTSTR)strTry ) == 0 )
        {
          bTaken = true;
          break;
        }
      if ( !bTaken )
        break;
      strTry.Format ( _T("%s_%d"), (LPCTSTR)strName, nSuffix );
    }
    oTaken.push_back ( strTry );
    return strTry;
}

//
//  PHP double-quoted string escaping.
//  NOTES: Double quotes rather than single, because a single-quoted PHP string
//         has no escape for a control character - it would carry a raw one into
//         the source file. The cost is that the dollar sign has to be escaped
//         as well, since a double-quoted string interpolates variables.
//       : Control characters take a FULL TWO-DIGIT \xNN. PHP reads up to two
//         hex digits after \x, so a one-digit escape followed by a hex
//         character would swallow it and produce a different string.
//
CString
PrintPHP::Escape ( LPCTSTR lpszText )
{
    CString strOut;
    if ( lpszText == nullptr )
      return strOut;

    for ( const TCHAR *p = lpszText; *p; ++p )
    {
      switch ( *p )
      {
        case _T('\\'): strOut += _T("\\\\"); break;
        case _T('\"'): strOut += _T("\\\""); break;
        case _T('$'):  strOut += _T("\\$");  break;
        case _T('\n'): strOut += _T("\\n");  break;
        case _T('\r'): strOut += _T("\\r");  break;
        case _T('\t'): strOut += _T("\\t");  break;
        default:
        {
          const unsigned int uCode = (unsigned int)*p;
          if ( uCode < 0x20u || uCode == 0x7Fu )
          {
            CString strEscape;
            strEscape.Format ( _T("\\x%02X"), uCode );
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
//  Reads the PHP declarations this renderer holds into oItem, REPLACING
//  everything oItem held.
//
//  Parameters:  P3PmsgItem& oItem
//               Manager, item, list or vector to write into
//
//  Returns:     MsgPrint&
//               This renderer, so GetError() can be read from the expression
//
//  NOTES: WHAT THIS READS IS THIS RENDERER'S OWN OUTPUT, and that is the whole
//         of the contract. It is not a PHP parser: it knows one shape - a class
//         per node, three synthetic properties, a __construct that assigns the
//         child objects - and a file that has been rearranged by hand is
//         outside it. JSON has a grammar that anything can write; PHP source
//         does not, so the honest scope is the round trip.
//       : IT LEANS ON THE DOCBLOCKS, and they are not decoration. The property
//         identifiers are SANITISED on the way out - spaces folded to
//         underscores, duplicates suffixed - so the only surviving record of a
//         Msgcore item's real name is the (Msgcore 'Name') in the comment above
//         it. So is the type. Strip the comments and this reads a file whose
//         names and types are gone; that is why the render calls them
//         do-not-edit.
//       : THE ROOT IS THE LAST CLASS. RenderClass emits every child class
//         before the parent that instantiates it, so the file ends with the
//         node the render was pointed at.
//
MsgPrint&
PrintPHP::Parse ( P3PmsgItem& oItem )
{
    ClearError ( );
    m_oClasses.clear ( );
    try
    {
      if ( !ReadClasses ( ) )
        return *this;
      if ( m_oClasses.empty ( ) )
      {
        SetError ( _T("No class declaration in the document"), 0 );
        return *this;
      }

      MsgValue oRoot;
      if ( !ReadClass ( m_oClasses.size ( ) - 1, oRoot, 0 ) )
        return *this;

      BuildItem ( oItem, oRoot, 0 );
      return *this;
    }
    //  A throw from the store side - a name the format refuses, a heap that
    //  cannot grow. The document was readable and the store would not take it,
    //  which is a different failure from a syntax error and says so.
    catch_pP2Pevent_Cancel
    catch_ALL_Cancel
    SetError ( _T("The store refused the document"), -1 );
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Reading

//
//  Steps over whitespace and comments.
//  NOTES: Comment-aware rather than a whitespace skip, because the words this
//         reader looks for - class, public, function - are words a Msgcore item
//         name may contain, and every item name in the file appears inside a
//         comment. A blind search would find "class" in a docblock and start
//         reading a declaration in the middle of a sentence.
//
void
PrintPHP::SkipTrivia ( int& nPos ) const
{
    const int nLength = m_strText.GetLength ( );
    for ( ;; )
    {
      while ( nPos < nLength
           && ( m_strText[nPos] == _T(' ')  || m_strText[nPos] == _T('\t')
             || m_strText[nPos] == _T('\r') || m_strText[nPos] == _T('\n') ) )
        nPos++;

      if ( nPos + 1 >= nLength || m_strText[nPos] != _T('/') )
        return;

      if ( m_strText[nPos + 1] == _T('/') )
      {
        nPos += 2;
        while ( nPos < nLength && m_strText[nPos] != _T('\n') )
          nPos++;
        continue;
      }
      if ( m_strText[nPos + 1] == _T('*') )
      {
        nPos += 2;
        while ( nPos + 1 < nLength
             && !( m_strText[nPos] == _T('*') && m_strText[nPos + 1] == _T('/') ) )
          nPos++;
        nPos = nPos + 1 < nLength ? nPos + 2 : nLength;
        continue;
      }
      return;
    }
}

//
//  The same, but keeping the last /** ... */ block it stepped over - which is
//  the docblock belonging to whatever comes next.
//
void
PrintPHP::SkipTriviaDoc ( int& nPos, CString& strDoc ) const
{
    const int nLength = m_strText.GetLength ( );
    for ( ;; )
    {
      while ( nPos < nLength
           && ( m_strText[nPos] == _T(' ')  || m_strText[nPos] == _T('\t')
             || m_strText[nPos] == _T('\r') || m_strText[nPos] == _T('\n') ) )
        nPos++;

      if ( nPos + 1 >= nLength || m_strText[nPos] != _T('/') )
        return;

      if ( m_strText[nPos + 1] == _T('/') )
      {
        nPos += 2;
        while ( nPos < nLength && m_strText[nPos] != _T('\n') )
          nPos++;
        continue;
      }
      if ( m_strText[nPos + 1] == _T('*') )
      {
        const bool bDoc   = nPos + 2 < nLength && m_strText[nPos + 2] == _T('*');
        const int  nStart = nPos + ( bDoc ? 3 : 2 );
        nPos += 2;
        while ( nPos + 1 < nLength
             && !( m_strText[nPos] == _T('*') && m_strText[nPos + 1] == _T('/') ) )
          nPos++;
        if ( bDoc )
          strDoc = m_strText.Mid ( nStart, nPos - nStart );
        nPos = nPos + 1 < nLength ? nPos + 2 : nLength;
        continue;
      }
      return;
    }
}

//
//  Steps over one string literal, from its opening quote to just past its
//  closing one - without decoding it. For the brace matcher, which has to know
//  that a '}' inside a string is not a '}'.
//
void
PrintPHP::SkipString ( int& nPos ) const
{
    const int   nLength = m_strText.GetLength ( );
    const TCHAR cQuote  = m_strText[nPos++];
    while ( nPos < nLength )
    {
      if ( m_strText[nPos] == _T('\\') )
      {
        nPos += 2;
        continue;
      }
      if ( m_strText[nPos] == cQuote )
      {
        nPos++;
        return;
      }
      nPos++;
    }
}

//
//  A PHP identifier, or false when what stands at nPos is not one.
//
bool
PrintPHP::ReadIdent ( CString& strOut, int& nPos ) const
{
    const int nLength = m_strText.GetLength ( );
    const int nStart  = nPos;
    while ( nPos < nLength )
    {
      const TCHAR c = m_strText[nPos];
      if ( ( c >= _T('a') && c <= _T('z') )
        || ( c >= _T('A') && c <= _T('Z') )
        || ( c >= _T('0') && c <= _T('9') )
        ||   c == _T('_') )
        nPos++;
      else
        break;
    }
    if ( nPos == nStart )
      return false;
    strOut = m_strText.Mid ( nStart, nPos - nStart );
    return true;
}

//
//  True when lpszWord stands at nPos as a whole word, and consumes it.
//
bool
PrintPHP::ReadWord ( LPCTSTR lpszWord, int& nPos ) const
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

//
//  Finds every `class <Ident> { ... }` in the document and records where each
//  body begins and ends.
//
bool
PrintPHP::ReadClasses ( )
{
    const int nLength = m_strText.GetLength ( );
    int       nPos    = 0;

    while ( nPos < nLength )
    {
      SkipTrivia ( nPos );
      if ( nPos >= nLength )
        break;

      //  A string at top level belongs to nothing this reads, but it can hold
      //  braces and the word class, so it is stepped over as a unit.
      if ( m_strText[nPos] == _T('"') || m_strText[nPos] == _T('\'') )
      {
        SkipString ( nPos );
        continue;
      }
      if ( !ReadWord ( _T("class"), nPos ) )
      {
        nPos++;
        continue;
      }

      SkipTrivia ( nPos );
      PHPClass oClass;
      if ( !ReadIdent ( oClass.strName, nPos ) )
      {
        SetError ( _T("Expecting a class name"), nPos );
        return false;
      }

      SkipTrivia ( nPos );
      if ( nPos >= nLength || m_strText[nPos] != _T('{') )
      {
        SetError ( _T("Expecting '{' after a class name"), nPos );
        return false;
      }
      nPos++;
      oClass.nBody = nPos;

      //  Brace matching, string-aware and comment-aware for the same reason
      //  SkipTrivia is: a brace inside either is not a brace.
      int nOpen = 1;
      while ( nPos < nLength && nOpen > 0 )
      {
        const int nBefore = nPos;
        SkipTrivia ( nPos );
        if ( nPos != nBefore )
          continue;
        const TCHAR c = m_strText[nPos];
        if ( c == _T('"') || c == _T('\'') )
        {
          SkipString ( nPos );
          continue;
        }
        if ( c == _T('{') )
          nOpen++;
        else if ( c == _T('}') )
          nOpen--;
        nPos++;
      }
      if ( nOpen > 0 )
      {
        SetError ( _T("Unterminated class body"), nPos );
        return false;
      }
      oClass.nEnd = nPos - 1;          // The matching '}'
      m_oClasses.push_back ( oClass );
    }
    return true;
}

int
PrintPHP::FindClass ( LPCTSTR lpszName ) const
{
    for ( size_t i = 0; i < m_oClasses.size ( ); i++ )
    {
      if ( m_oClasses[i].strName.Compare ( lpszName ) == 0 )
        return (int)i;
    }
    return -1;
}

//
//  The text between (Msgcore ' and ') in a docblock - the item's real name,
//  which the sanitised property identifier no longer carries.
//  NOTES: Terminated on ') and not on the next quote, so a name that itself
//         contains an apostrophe survives. One containing ') does not, and
//         cannot: the render does not escape the docblock, so that name is
//         already ambiguous in the file by the time it gets here.
//
bool
PrintPHP::DocName ( const CString& strDoc, CString& strName )
{
    const int nStart = strDoc.Find ( _T("(Msgcore '") );
    if ( nStart < 0 )
      return false;
    const int nFrom = nStart + 10;
    //  Searched in the TAIL rather than through a Find(text,offset), which MFC
    //  has and the Platform shim's CString does not - the shim carries the
    //  overloads the compiled set named, and this would have been the first
    //  call to that one. Same result, and it builds on both targets.
    const int nEnd = strDoc.Mid ( nFrom ).Find ( _T("')") );
    if ( nEnd < 0 )
      return false;
    strName = strDoc.Mid ( nFrom, nEnd );
    return true;
}

//
//  The Msgcore type name a docblock carries: the token after "mixed" in
//  `@var mixed  WSTR16  (Msgcore 'X')`. Empty for the array and class forms,
//  which name a PHP type rather than a stored one.
//
CString
PrintPHP::DocType ( const CString& strDoc )
{
    CString   strType;
    const int nVar = strDoc.Find ( _T("@var") );
    if ( nVar < 0 )
      return strType;

    const int nMixed = strDoc.Mid ( nVar ).Find ( _T("mixed") );
    if ( nMixed < 0 )
      return strType;
    int nPos = nVar + nMixed + 5;

    while ( nPos < strDoc.GetLength ( )
         && ( strDoc[nPos] == _T(' ') || strDoc[nPos] == _T('\t') ) )
      nPos++;
    const int nStart = nPos;
    while ( nPos < strDoc.GetLength ( )
         && strDoc[nPos] != _T(' ')  && strDoc[nPos] != _T('\t')
         && strDoc[nPos] != _T('\r') && strDoc[nPos] != _T('\n')
         && strDoc[nPos] != _T('(')  && strDoc[nPos] != _T('*') )
      nPos++;
    if ( nPos > nStart )
      strType = strDoc.Mid ( nStart, nPos - nStart );
    return strType;
}

//
//  One class declaration as a MsgValue object.
//
//  Parameters:  size_t nClass
//               Index into m_oClasses
//
//               MsgValue& oValue
//               Value to fill - always an object
//
//               int nDepth
//               Recursion depth, against MsgPrint's ceiling
//
//  NOTES: THE SYNTHETIC PROPERTIES ARE FOUND BY THEIR DOCBLOCK AND NOT BY THEIR
//         NAME. $_value is only called _value when no descendant sanitised to
//         that identifier first; when one did, the render suffixed it and the
//         node's own value is in $_value_2. The comment is the same either way.
//       : Properties are read IN DECLARATION ORDER, which is the order the
//         render walked the store in, so descendants come back in the order
//         they were in.
//       : A property with NO initialiser is a child object, and __construct is
//         where it is assigned. The constructor comes after the properties, so
//         those members are added empty on the first pass and filled once the
//         body has been read to the end.
//
bool
PrintPHP::ReadClass ( size_t nClass, MsgValue& oValue, int nDepth )
{
    if ( nDepth > m_nDepthMax )
    {
      SetError ( _T("Document nests deeper than the depth ceiling"), -1 );
      return false;
    }
    if ( nClass >= m_oClasses.size ( ) )
    {
      SetError ( _T("Reference to a class that is not in the document"), -1 );
      return false;
    }

    const PHPClass oClass = m_oClasses[nClass];
    oValue.SetKind ( Value_Object );

    //  Members added empty because their value is a class instantiated in the
    //  constructor, paired with the property they are assigned from.
    std::vector<CString> oPending;
    std::vector<size_t>  oPendingAt;
    //  What __construct assigns, once it has been read.
    std::vector<CString> oCtorProp;
    std::vector<CString> oCtorClass;

    int nPos = oClass.nBody;
    while ( nPos < oClass.nEnd )
    {
      CString strDoc;
      SkipTriviaDoc ( nPos, strDoc );
      if ( nPos >= oClass.nEnd )
        break;

      if ( !ReadWord ( _T("public"), nPos ) )
      {
        //  Nothing else in a generated class is anything this reads.
        nPos++;
        continue;
      }
      SkipTrivia ( nPos );

      //  public function __construct ( ) { ... }
      if ( ReadWord ( _T("function"), nPos ) )
      {
        if ( !ReadCtor ( nPos, oClass.nEnd, oCtorProp, oCtorClass ) )
          return false;
        continue;
      }

      //  public $name [= expression] ;
      if ( nPos >= oClass.nEnd || m_strText[nPos] != _T('$') )
      {
        SetError ( _T("Expecting a property after 'public'"), nPos );
        return false;
      }
      nPos++;

      CString strProp;
      if ( !ReadIdent ( strProp, nPos ) )
      {
        SetError ( _T("Expecting a property name"), nPos );
        return false;
      }

      //  Which of the four kinds of property this is, from the docblock.
      const bool bIsValue = strDoc.Find ( _T("(the node's own value)") ) >= 0;
      const bool bIsList  = strDoc.Find ( _T("(Msgcore list elements)") ) >= 0;
      const bool bIsVect  = strDoc.Find ( _T("(Msgcore vector elements)") ) >= 0;
      const bool bIsAttr  = strDoc.Find ( _T("(Msgcore attributes)") ) >= 0;

      CString strName;
      if ( !bIsValue && !bIsList && !bIsVect && !bIsAttr
        && !DocName ( strDoc, strName ) )
      {
        //  No docblock, so no name - and the identifier is not one, because it
        //  has been through Sanitise. Refused rather than guessed at: writing
        //  the folded identifier into the store would put a name there that
        //  was never in it.
        SetError ( _T("Property has no (Msgcore 'name') docblock, so its ")
                   _T("Msgcore name cannot be recovered"), nPos );
        return false;
      }

      SkipTrivia ( nPos );
      if ( nPos < oClass.nEnd && m_strText[nPos] == _T(';') )
      {
        //  No initialiser: a child class, assigned in the constructor below.
        nPos++;
        oPending.push_back ( strProp );
        oPendingAt.push_back ( oValue.GetCount ( ) );
        oValue.Add ( (LPCTSTR)strName );
        continue;
      }
      if ( nPos >= oClass.nEnd || m_strText[nPos] != _T('=') )
      {
        SetError ( _T("Expecting '=' or ';' after a property name"), nPos );
        return false;
      }
      nPos++;

      if ( bIsAttr )
      {
        //  The attribute set arrives as one associative array keyed by the
        //  bare names - the class-level form does not prefix them, where the
        //  nested one does - so its entries are lifted out and re-keyed here
        //  into the space the store writer expects.
        MsgValue oAttrs;
        if ( !ReadExpr ( oAttrs, nPos, nDepth + 1 ) )
          return false;
        CString strPrefix;
        strPrefix += MSGPRINT_KEY_ATTR;
        oValue.AdoptChildren ( oAttrs, (LPCTSTR)strPrefix );
      }
      else
      {
        LPCTSTR lpszKey = bIsValue ? MSGPRINT_KEY_VALUE
                        : ( bIsList || bIsVect ) ? MSGPRINT_KEY_ITEMS
                        : (LPCTSTR)strName;
        MsgValue& oMember = oValue.Add ( lpszKey );
        if ( !ReadExpr ( oMember, nPos, nDepth + 1 ) )
          return false;
        //  The one thing a PHP render says outright that no array literal can:
        //  whether the elements were a list or a vector.
        if ( bIsVect )
          oValue.SetVectorHint ( true );
        if ( bIsValue || !bIsList )
          oMember.SetType ( (LPCTSTR)DocType ( strDoc ) );
      }

      SkipTrivia ( nPos );
      if ( nPos < oClass.nEnd && m_strText[nPos] == _T(';') )
        nPos++;
    }

    //  The constructor has been read by now, so the child objects can be
    //  resolved to the classes they instantiate.
    for ( size_t i = 0; i < oPending.size ( ); i++ )
    {
      int nChild = -1;
      for ( size_t k = 0; k < oCtorProp.size ( ); k++ )
      {
        if ( oCtorProp[k].Compare ( (LPCTSTR)oPending[i] ) == 0 )
        {
          nChild = FindClass ( (LPCTSTR)oCtorClass[k] );
          break;
        }
      }
      if ( nChild < 0 )
      {
        SetError ( _T("A property has neither a value nor a constructor ")
                   _T("assignment"), -1 );
        return false;
      }
      MsgValue *pMember = oValue.p_child ( oPendingAt[i] );
      if ( pMember == nullptr
        || !ReadClass ( (size_t)nChild, *pMember, nDepth + 1 ) )
        return false;
    }
    return true;
}

//
//  The body of __construct, collecting every `$this->prop = new Class();`.
//  NOTES: Only that one statement shape is read. It is the only one the render
//         writes, and a constructor that had been edited to do anything else is
//         outside what this reader claims to understand.
//
bool
PrintPHP::ReadCtor ( int& nPos, int nEnd
                   , std::vector<CString>& oProp
                   , std::vector<CString>& oClass )
{
    const int nLength = m_strText.GetLength ( );

    //  Past the name and the parameter list to the opening brace.
    while ( nPos < nEnd && m_strText[nPos] != _T('{') )
      nPos++;
    if ( nPos >= nEnd )
    {
      SetError ( _T("Unterminated constructor"), nPos );
      return false;
    }
    nPos++;

    int nOpen = 1;
    while ( nPos < nLength && nOpen > 0 )
    {
      const int nBefore = nPos;
      SkipTrivia ( nPos );
      if ( nPos != nBefore )
        continue;

      const TCHAR c = m_strText[nPos];
      if ( c == _T('"') || c == _T('\'') )
      {
        SkipString ( nPos );
        continue;
      }
      if ( c == _T('{') )
      {
        nOpen++;
        nPos++;
        continue;
      }
      if ( c == _T('}') )
      {
        nOpen--;
        nPos++;
        continue;
      }

      //  $this->Prop = new Class();
      if ( c == _T('$') )
      {
        const int nSave = nPos;
        nPos++;
        CString strThis;
        if ( ReadIdent ( strThis, nPos ) && strThis.Compare ( _T("this") ) == 0 )
        {
          SkipTrivia ( nPos );
          if ( nPos + 1 < nLength
            && m_strText[nPos] == _T('-') && m_strText[nPos + 1] == _T('>') )
          {
            nPos += 2;
            SkipTrivia ( nPos );
            CString strProp;
            if ( ReadIdent ( strProp, nPos ) )
            {
              SkipTrivia ( nPos );
              if ( nPos < nLength && m_strText[nPos] == _T('=') )
              {
                nPos++;
                SkipTrivia ( nPos );
                if ( ReadWord ( _T("new"), nPos ) )
                {
                  SkipTrivia ( nPos );
                  CString strClass;
                  if ( ReadIdent ( strClass, nPos ) )
                  {
                    oProp.push_back ( strProp );
                    oClass.push_back ( strClass );
                    continue;
                  }
                }
              }
            }
          }
        }
        nPos = nSave + 1;
        continue;
      }
      nPos++;
    }
    return true;
}

//
//  One PHP value expression: a scalar literal, or an array().
//
bool
PrintPHP::ReadExpr ( MsgValue& oValue, int& nPos, int nDepth )
{
    if ( nDepth > m_nDepthMax )
    {
      SetError ( _T("Document nests deeper than the depth ceiling"), nPos );
      return false;
    }

    SkipTrivia ( nPos );
    const int nLength = m_strText.GetLength ( );
    if ( nPos >= nLength )
    {
      SetError ( _T("Unexpected end of document, expecting a value"), nPos );
      return false;
    }

    const TCHAR c = m_strText[nPos];
    if ( c == _T('"') || c == _T('\'') )
    {
      CString strText;
      if ( !ReadPHPString ( strText, nPos ) )
        return false;
      oValue.SetScalar ( Value_String, (LPCTSTR)strText );
      return true;
    }
    if ( ReadWord ( _T("array"), nPos ) )
      return ReadArrayExpr ( oValue, nPos, nDepth );
    if ( ReadWord ( _T("true"), nPos ) )
    {
      oValue.SetScalar ( Value_Bool, _T("true") );
      return true;
    }
    if ( ReadWord ( _T("false"), nPos ) )
    {
      oValue.SetScalar ( Value_Bool, _T("false") );
      return true;
    }
    if ( ReadWord ( _T("null"), nPos ) )
    {
      oValue.SetScalar ( Value_Null, _T("") );
      return true;
    }
    if ( c == _T('-') || c == _T('+') || ( c >= _T('0') && c <= _T('9') ) )
    {
      const int nStart = nPos;
      if ( c == _T('-') || c == _T('+') )
        nPos++;
      while ( nPos < nLength
           && ( ( m_strText[nPos] >= _T('0') && m_strText[nPos] <= _T('9') )
             ||   m_strText[nPos] == _T('.')
             ||   m_strText[nPos] == _T('e') || m_strText[nPos] == _T('E')
             || ( ( m_strText[nPos] == _T('-') || m_strText[nPos] == _T('+') )
                  && ( m_strText[nPos-1] == _T('e') || m_strText[nPos-1] == _T('E') ) ) ) )
        nPos++;
      if ( nPos == nStart )
      {
        SetError ( _T("Expecting a number"), nPos );
        return false;
      }
      oValue.SetScalar ( Value_Number
                       , (LPCTSTR)m_strText.Mid ( nStart, nPos - nStart ) );
      return true;
    }

    SetError ( _T("Expecting a value"), nPos );
    return false;
}

//
//  array( ... ) - an ARRAY when its entries are bare and an OBJECT when they
//  are 'key' => value, which is the same distinction JSON draws with [ and {.
//  NOTES: The keys are the sentinels the render writes - '.value', '.items' and
//         the @ prefix - and they arrive here already in the space the store
//         writer expects, which is why nothing re-keys them the way the
//         class-level $_attributes has to be.
//
bool
PrintPHP::ReadArrayExpr ( MsgValue& oValue, int& nPos, int nDepth )
{
    const int nLength = m_strText.GetLength ( );

    SkipTrivia ( nPos );
    if ( nPos >= nLength || m_strText[nPos] != _T('(') )
    {
      SetError ( _T("Expecting '(' after array"), nPos );
      return false;
    }
    nPos++;

    oValue.SetKind ( Value_Array );

    SkipTrivia ( nPos );
    if ( nPos < nLength && m_strText[nPos] == _T(')') )
    {
      nPos++;
      return true;
    }

    for ( ;; )
    {
      SkipTrivia ( nPos );
      if ( nPos >= nLength )
      {
        SetError ( _T("Unexpected end of document inside an array"), nPos );
        return false;
      }

      //  A key, if this entry has one. Read ahead rather than committed to,
      //  because a quoted STRING is also how a keyless entry can start.
      CString strKey;
      bool    bKeyed = false;
      if ( m_strText[nPos] == _T('\'') || m_strText[nPos] == _T('"') )
      {
        const int nSave = nPos;
        CString   strText;
        if ( !ReadPHPString ( strText, nPos ) )
          return false;
        SkipTrivia ( nPos );
        if ( nPos + 1 < nLength
          && m_strText[nPos] == _T('=') && m_strText[nPos + 1] == _T('>') )
        {
          nPos  += 2;
          strKey = strText;
          bKeyed = true;
        }
        else
          nPos = nSave;                // A value after all
      }

      if ( bKeyed )
        oValue.SetKind ( Value_Object );

      if ( !ReadExpr ( oValue.Add ( bKeyed ? (LPCTSTR)strKey : nullptr )
                     , nPos, nDepth + 1 ) )
        return false;

      SkipTrivia ( nPos );
      if ( nPos >= nLength )
      {
        SetError ( _T("Unexpected end of document inside an array"), nPos );
        return false;
      }
      if ( m_strText[nPos] == _T(',') )
      {
        nPos++;
        SkipTrivia ( nPos );
        //  PHP allows a trailing comma and the render does not write one, but
        //  a file that has been reformatted may.
        if ( nPos < nLength && m_strText[nPos] == _T(')') )
        {
          nPos++;
          return true;
        }
        continue;
      }
      if ( m_strText[nPos] == _T(')') )
      {
        nPos++;
        return true;
      }
      SetError ( _T("Expecting ',' or ')'"), nPos );
      return false;
    }
}

//
//  A quoted string with its escapes resolved - the inverse of PrintPHP::Escape,
//  and it accepts both quote styles because the render uses double quotes for
//  values and single ones for keys and escapes BOTH the same way.
//
bool
PrintPHP::ReadPHPString ( CString& strOut, int& nPos )
{
    strOut.Empty ( );
    const int   nLength = m_strText.GetLength ( );
    const TCHAR cQuote  = m_strText[nPos++];

    while ( nPos < nLength )
    {
      const TCHAR c = m_strText[nPos];
      if ( c == cQuote )
      {
        nPos++;
        return true;
      }
      if ( c != _T('\\') )
      {
        strOut += c;
        nPos++;
        continue;
      }

      nPos++;
      if ( nPos >= nLength )
        break;
      const TCHAR cEscape = m_strText[nPos++];
      switch ( cEscape )
      {
        case _T('\\'): strOut += _T('\\'); break;
        case _T('"'):  strOut += _T('"');  break;
        case _T('\''): strOut += _T('\''); break;
        case _T('$'):  strOut += _T('$');  break;
        case _T('n'):  strOut += _T('\n'); break;
        case _T('r'):  strOut += _T('\r'); break;
        case _T('t'):  strOut += _T('\t'); break;
        case _T('v'):  strOut += _T('\v'); break;
        case _T('f'):  strOut += _T('\f'); break;
        case _T('e'):  strOut += (TCHAR)0x1B; break;
        case _T('x'):
        {
          //  UP TO two hex digits, which is what PHP itself takes and what the
          //  render relies on when it always writes two.
          unsigned int uCode = 0;
          int          nSeen = 0;
          while ( nSeen < 2 && nPos < nLength )
          {
            const TCHAR  cHex = m_strText[nPos];
            unsigned int nDigit = 0;
            if      ( cHex >= _T('0') && cHex <= _T('9') ) nDigit = (unsigned int)(cHex - _T('0'));
            else if ( cHex >= _T('a') && cHex <= _T('f') ) nDigit = (unsigned int)(cHex - _T('a')) + 10;
            else if ( cHex >= _T('A') && cHex <= _T('F') ) nDigit = (unsigned int)(cHex - _T('A')) + 10;
            else
              break;
            uCode = (uCode << 4) | nDigit;
            nPos++;
            nSeen++;
          }
          if ( nSeen == 0 )
          {
            strOut += _T("\\x");       // Not an escape after all
            break;
          }
          strOut += (TCHAR)uCode;
          break;
        }
        default:
          //  PHP leaves an unknown escape as the two characters it is written
          //  with, and so does this - the render never emits one, so a file
          //  that has one was edited and its literal text is the best reading.
          strOut += _T('\\');
          strOut += cEscape;
          break;
      }
    }

    SetError ( _T("Unterminated string"), nPos );
    return false;
}

///////////////////////////////////////////////////////////////////////
//  Rendering operator

PrintPHP&
operator >> ( P3PmsgItem& oItem, PrintPHP& oPHP )
{
    oPHP.Render ( oItem );
    return oPHP;
}

PrintPHP&
operator << ( PrintPHP& oPHP, P3PmsgItem& oItem )
{
    oPHP.Render ( oItem );
    return oPHP;
}

///////////////////////////////////////////////////////////////////////
//  Parsing operators

PrintPHP&
operator << ( P3PmsgItem& oItem, PrintPHP& oPHP )
{
    oPHP.Parse ( oItem );
    return oPHP;
}

PrintPHP&
operator >> ( PrintPHP& oPHP, P3PmsgItem& oItem )
{
    oPHP.Parse ( oItem );
    return oPHP;
}
