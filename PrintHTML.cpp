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
//  PrintHTML - a Msgcore store as a page someone can read. See PrintHTML.h for
//  why this is the one dialect that only goes one way.
//
//  THE CLASS NAMES ARE PART OF THE INTERFACE, because SetStyle(false) exists
//  and a host page needs something to style. They are stable:
//
//    .p2p            the outer <main>
//    .p2p-head       the header, holding .p2p-title and .p2p-meta
//    .p2p-chip       one count in the header line
//    .p2p-tree       a <ul> of nodes, nested once per level
//    .p2p-row        a leaf node - a key, a value and a badge
//    .p2p-node       a node with children, wrapping a <details>
//    .p2p-isattr     on either of those, when the node is an ATTRIBUTE
//    .p2p-key        the name, with .p2p-at on the @ and .p2p-idx on an index
//    .p2p-val        the value, plus one of .p2p-str .p2p-num .p2p-bool
//                    .p2p-nul saying which KIND it is
//    .p2p-badge      the type name
//    .p2p-sum        what a collapsed node contains
//    .p2p-foot       the footer
//
#include "stdafx.h"

#include "PrintHTML.h"

#include "../Msgcore/Msgexception.h"

///////////////////////////////////////////////////////////////////////
//  The stylesheet
//  NOTES: Custom properties for every colour, redefined once under
//         prefers-color-scheme, so the dark theme is a dozen lines rather than
//         a second stylesheet. Nothing here needs a script, a web font or a
//         second file: the page has to survive being mailed to someone.
//       : No colour is defined ONLY inside the media query. A reader whose
//         browser does not support prefers-color-scheme gets the light set,
//         which is complete on its own.
static const TCHAR *const PRINTHTML_CSS =
_T("*,*::before,*::after{box-sizing:border-box}\n")
_T(":root{color-scheme:light dark;\n")
_T("  --bg:#f4f6f8;--card:#fff;--ink:#1a1d21;--dim:#6a7280;--line:#e2e6ea;\n")
_T("  --rule:#dfe4e9;--key:#0b5394;--at:#b45309;--str:#0a7d43;--num:#b02a6f;\n")
_T("  --bool:#6d28d9;--nul:#98a0a8;--chip:#eef1f5;--chipink:#4a5361}\n")
_T("@media (prefers-color-scheme:dark){:root{\n")
_T("  --bg:#101317;--card:#181c21;--ink:#e6e9ec;--dim:#98a1ab;--line:#272d34;\n")
_T("  --rule:#252b32;--key:#7cb7f0;--at:#e0a458;--str:#68d391;--num:#f490c0;\n")
_T("  --bool:#b39ef5;--nul:#6b7480;--chip:#232930;--chipink:#a9b3bd}}\n")
_T("body{margin:0;background:var(--bg);color:var(--ink);-webkit-font-smoothing:antialiased;\n")
_T("  font:14px/1.55 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif}\n")
_T(".p2p{max-width:1000px;margin:32px auto;padding:0 20px}\n")
_T(".p2p-head{margin:0 0 16px}\n")
_T(".p2p-title{margin:0;font-size:22px;font-weight:650;letter-spacing:-.01em}\n")
_T(".p2p-meta{margin:8px 0 0;display:flex;flex-wrap:wrap;gap:6px}\n")
_T(".p2p-chip{background:var(--chip);color:var(--chipink);border-radius:999px;\n")
_T("  padding:2px 10px;font-size:12px;white-space:nowrap}\n")
_T(".p2p-tree{list-style:none;margin:0;padding:0}\n")
_T(".p2p>.p2p-tree{background:var(--card);border:1px solid var(--line);border-radius:10px;\n")
_T("  padding:6px;box-shadow:0 1px 2px rgba(0,0,0,.04)}\n")
_T(".p2p-tree .p2p-tree{margin-left:9px;padding-left:12px;border-left:1px solid var(--rule)}\n")
_T(".p2p-row,.p2p-node>details>summary{display:flex;align-items:baseline;gap:8px;\n")
_T("  padding:3px 8px;border-radius:6px}\n")
_T(".p2p-row:hover,.p2p-node>details>summary:hover{background:var(--chip)}\n")
_T(".p2p-key{color:var(--key);font-weight:600;font-size:13px;flex:none;\n")
_T("  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}\n")
_T(".p2p-at{color:var(--at)}\n")
_T(".p2p-idx{color:var(--dim);font-weight:400}\n")
_T(".p2p-isattr>.p2p-key,.p2p-isattr>details>summary>.p2p-key{color:var(--at)}\n")
_T(".p2p-val{font-size:13px;overflow-wrap:anywhere;min-width:0;white-space:pre-wrap;\n")
_T("  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}\n")
_T(".p2p-str{color:var(--str)}\n")
_T(".p2p-num{color:var(--num)}\n")
_T(".p2p-bool{color:var(--bool)}\n")
_T(".p2p-nul{color:var(--nul);font-style:italic}\n")
_T(".p2p-sum{color:var(--dim);font-size:12px}\n")
_T(".p2p-badge{margin-left:auto;flex:none;background:var(--chip);color:var(--chipink);\n")
_T("  border-radius:5px;padding:0 6px;font-size:11px;letter-spacing:.02em;white-space:nowrap}\n")
_T(".p2p-node>details>summary{cursor:pointer;list-style:none}\n")
_T(".p2p-node>details>summary::-webkit-details-marker{display:none}\n")
_T(".p2p-node>details>summary::before{content:'';flex:none;width:0;height:0;color:var(--dim);\n")
_T("  border-left:5px solid currentColor;border-top:4px solid transparent;\n")
_T("  border-bottom:4px solid transparent;transition:transform .12s ease}\n")
_T(".p2p-node>details[open]>summary::before{transform:rotate(90deg)}\n")
_T(".p2p-foot{margin:14px 2px 0;color:var(--dim);font-size:12px}\n")
_T("@media print{body{background:#fff}.p2p>.p2p-tree{box-shadow:none}\n")
_T("  .p2p-node>details>summary::before{display:none}}\n");

///////////////////////////////////////////////////////////////////////
//  Constructors and destructor

PrintHTML::PrintHTML ( ) noexcept
{
}

PrintHTML::PrintHTML ( const PrintHTML& rhs )
        : MsgPrint ( rhs )
{
    m_bDocument  = rhs.m_bDocument;
    m_bStyle     = rhs.m_bStyle;
    m_bTypes     = rhs.m_bTypes;
    m_bCollapsed = rhs.m_bCollapsed;
    m_strTitle   = rhs.m_strTitle;
    m_nNodes     = rhs.m_nNodes;
    m_nAttrs     = rhs.m_nAttrs;
    m_nArrays    = rhs.m_nArrays;
}

PrintHTML::~PrintHTML ( )
{
}

///////////////////////////////////////////////////////////////////////
//  Operators

PrintHTML&
PrintHTML::operator = ( const PrintHTML& rhs )
{
    if ( this == &rhs )
      return *this;
    MsgPrint::operator = ( rhs );
    m_bDocument  = rhs.m_bDocument;
    m_bStyle     = rhs.m_bStyle;
    m_bTypes     = rhs.m_bTypes;
    m_bCollapsed = rhs.m_bCollapsed;
    m_strTitle   = rhs.m_strTitle;
    m_nNodes     = rhs.m_nNodes;
    m_nAttrs     = rhs.m_nAttrs;
    m_nArrays    = rhs.m_nArrays;
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Rendering

//
//  Renders oItem and everything beneath it as an HTML page, REPLACING whatever
//  this renderer held.
//
//  Parameters:  P3PmsgItem& oItem
//               Manager, item, list or vector to render
//
//  Returns:     MsgPrint&
//               This renderer, so the page can be read from the expression
//
//  NOTES: THE TREE IS RENDERED FIRST AND THE PAGE AROUND IT SECOND. The header
//         counts what the document contains, and the only honest way to know
//         that is to have rendered it - a second traversal to count would be a
//         second chance to disagree with the first about what it found.
//
MsgPrint&
PrintHTML::Render ( P3PmsgItem& oItem )
{
    m_strText.Empty ( );
    m_nNodes  = 0;
    m_nAttrs  = 0;
    m_nArrays = 0;

    try
    {
      Append      ( _T("<ul class=\"p2p-tree\">") );
      RenderNode  ( oItem, oItem.c_name ( ), nullptr, false, 1 );
      AppendBreak ( 0 );
      Append      ( _T("</ul>") );

      const CString strBody = m_strText;
      m_strText.Empty ( );

      //  The node's own name, unless the caller named the page. A manager's
      //  name is the store's root name, which is what makes an untitled page
      //  identify itself.
      CString strTitle = m_strTitle;
      if ( strTitle.IsEmpty ( ) && oItem.c_name ( ) )
        strTitle = oItem.c_name ( );
      if ( strTitle.IsEmpty ( ) )
        strTitle = _T("Msgcore store");

      RenderHead ( strTitle );
      Append     ( (LPCTSTR)strBody );
      RenderFoot ( );
      return *this;
    }
    //  A throw from the traversal leaves a page with unclosed elements, which
    //  a browser will render as something - and something wrong is worse here
    //  than nothing, because nobody checks a page for truncation. Empty it, so
    //  IsEmpty() is the answer to "did this work".
    catch_pP2Pevent_Cancel
    catch_ALL_Cancel
    m_strText.Empty ( );
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Parsing

//
//  There is no HTML parse. Reports that and leaves oItem exactly as it was.
//
//  Parameters:  P3PmsgItem& oItem
//               Untouched, deliberately
//
//  Returns:     MsgPrint&
//               This renderer, carrying the refusal in GetError()
//
//  NOTES: Reachable only through a MsgPrint& - the parsing operators are not
//         declared for this class, so oMgr << oHTML does not compile. This
//         exists for the caller who has a base reference and cannot know which
//         dialect is behind it, and it makes the same promise a failed parse
//         makes everywhere else in this library: the target is untouched.
//
MsgPrint&
PrintHTML::Parse ( P3PmsgItem& oItem )
{
    (void)oItem;
    ClearError ( );
    SetError ( _T("HTML is a view of a store, not a source for one - there is ")
               _T("no HTML parse. Use PrintXML or PrintJson to read a document ")
               _T("back into a store."), -1 );
    return *this;
}

///////////////////////////////////////////////////////////////////////
//  Properties

bool
PrintHTML::SetDocument ( bool bDocument ) noexcept
{
    const bool bPrevious = m_bDocument;
    m_bDocument = bDocument;
    return bPrevious;
}

bool
PrintHTML::GetDocument ( ) const noexcept
{
    return m_bDocument;
}

bool
PrintHTML::SetStyle ( bool bStyle ) noexcept
{
    const bool bPrevious = m_bStyle;
    m_bStyle = bStyle;
    return bPrevious;
}

bool
PrintHTML::GetStyle ( ) const noexcept
{
    return m_bStyle;
}

void
PrintHTML::SetTitle ( LPCTSTR lpszTitle )
{
    m_strTitle = lpszTitle ? lpszTitle : _T("");
}

LPCTSTR
PrintHTML::GetTitle ( ) const noexcept
{
    return (LPCTSTR)m_strTitle;
}

bool
PrintHTML::SetTypes ( bool bTypes ) noexcept
{
    const bool bPrevious = m_bTypes;
    m_bTypes = bTypes;
    return bPrevious;
}

bool
PrintHTML::GetTypes ( ) const noexcept
{
    return m_bTypes;
}

bool
PrintHTML::SetCollapsed ( bool bCollapsed ) noexcept
{
    const bool bPrevious = m_bCollapsed;
    m_bCollapsed = bCollapsed;
    return bPrevious;
}

bool
PrintHTML::GetCollapsed ( ) const noexcept
{
    return m_bCollapsed;
}

///////////////////////////////////////////////////////////////////////
//  Implementation

//
//  Emits one <li> - a row for a plain field, a collapsible <details> for a
//  node that carries anything.
//
//  Parameters:  P3PmsgItem& oItem
//               Node to render
//
//               LPCTSTR lpszName
//               Its Msgcore name, or nullptr when it is an unnamed element
//
//               LPCTSTR lpszIndex
//               Its position, for an unnamed element. One of the two is null
//
//               bool bAttr
//               Whether it is one of its parent's attributes
//
//               int nDepth
//               Nesting level, for the indent
//
void
PrintHTML::RenderNode ( P3PmsgItem& oItem, LPCTSTR lpszName, LPCTSTR lpszIndex,
                        bool bAttr, int nDepth )
{
    if ( bAttr )
      m_nAttrs++;
    else
      m_nNodes++;

    //  The inside of the key span, already escaped - an index in its own
    //  colour, or the name behind the @ that marks an attribute everywhere
    //  else in this library.
    CString strKey;
    if ( lpszIndex )
    {
      strKey += _T("<span class=\"p2p-idx\">");
      strKey += Escape ( lpszIndex );
      strKey += _T("</span>");
    }
    else
    {
      if ( bAttr )
        strKey += _T("<span class=\"p2p-at\">@</span>");
      strKey += Escape ( lpszName );
    }

    if ( nDepth > m_nDepthMax )
    {
      //  The ceiling, not a structural case. A tree cannot reach it; a store
      //  whose links were corrupted can, and an unbounded descent on one is a
      //  stack overflow rather than a diagnostic.
      AppendBreak ( nDepth );
      Append ( _T("<li class=\"p2p-row\"><span class=\"p2p-key\">") );
      Append ( (LPCTSTR)strKey );
      Append ( _T("</span><span class=\"p2p-val p2p-nul\">too deep to render</span></li>") );
      return;
    }

    const bool bList  = oItem.r_Object().IsList ( );
    const bool bVect  = oItem.r_Object().IsVect ( );
    const bool bDesc  = !oItem.r_Desc().IsEmpty ( );
    const bool bAttrs = m_bAttributes && !oItem.r_Attr().IsEmpty ( );

    if ( bList || bVect )
      m_nArrays++;

    if ( !bList && !bVect && !bDesc && !bAttrs )
    {
      RenderLeaf ( oItem.r_data(), strKey, bAttr, nDepth );
      return;
    }

    AppendBreak ( nDepth );
    Append ( _T("<li class=\"p2p-node") );
    if ( bAttr )
      Append ( _T(" p2p-isattr") );
    Append ( _T("\"><details") );
    Append ( m_bCollapsed ? _T(">") : _T(" open>") );
    Append ( _T("<summary><span class=\"p2p-key\">") );
    Append ( (LPCTSTR)strKey );
    Append ( _T("</span>") );

    //  A node with children may still hold a value of its own, and the summary
    //  line is where it belongs: it is the one thing about the node that a
    //  reader would otherwise have to open it to see.
    if ( !bList && !bVect )
    {
      CString strValue;
      if ( RenderScalar ( oItem.r_data(), strValue ) != Value_Null )
      {
        RenderValue ( oItem.r_data() );
        RenderBadge ( oItem.r_data() );
      }
    }
    Append ( _T("<span class=\"p2p-sum\">") );
    Append ( (LPCTSTR)Summarise ( oItem ) );
    Append ( _T("</span></summary>") );

    AppendBreak ( nDepth + 1 );
    Append ( _T("<ul class=\"p2p-tree\">") );
    if ( bList )
      RenderList ( dynamic_cast<P3PmsgList&>(oItem), nDepth + 2 );
    else if ( bVect )
      RenderVect ( dynamic_cast<P3PmsgVect&>(oItem), nDepth + 2 );
    RenderChildren ( oItem, nDepth + 2 );
    AppendBreak ( nDepth + 1 );
    Append ( _T("</ul>") );

    AppendBreak ( nDepth );
    Append ( _T("</details></li>") );
}

//
//  A list is a chain of data entries and nothing else, so every entry is a row
//  numbered by position.
//
void
PrintHTML::RenderList ( P3PmsgList& oList, int nDepth )
{
    VBLaddr aEntry = oList.GetHeadPos ( );
    int     nIndex = 0;
    while ( aEntry )
    {
      P3PmsgData& oEntry = oList.GetNext ( aEntry );
      CString     strKey;
      strKey.Format ( _T("<span class=\"p2p-idx\">[%d]</span>"), nIndex++ );
      RenderLeaf ( oEntry, strKey, false, nDepth );
    }
}

//
//  A vector holds ELEMENTS, which may themselves be items, lists or vectors,
//  so each one goes back through RenderNode under its index.
//  NOTES: r_item(i) walks the cursor to element i and the reference it returns
//         is invalidated by the next walk - which is why nothing here holds one
//         across an iteration.
//
void
PrintHTML::RenderVect ( P3PmsgVect& oVect, int nDepth )
{
    const VBLelem nCount = oVect.GetCount ( );
    for ( VBLelem i = 0; i < nCount; i++ )
    {
      CString strIndex;
      strIndex.Format ( _T("[%d]"), (int)i );
      try
      {
        RenderNode ( oVect.r_item ( (int)i ), nullptr, (LPCTSTR)strIndex,
                     false, nDepth );
      }
      //  An element the vector cannot instantiate is one element, not one
      //  page. Skipped, the same call RenderScalar makes for a cell that
      //  disagrees with its own type byte.
      catch_pP2Pevent_Cancel
      catch_ALL_Cancel
    }
}

//
//  Attributes then descendants, in that order, which is the order every other
//  dialect in this library writes them.
//
void
PrintHTML::RenderChildren ( P3PmsgItem& oItem, int nDepth )
{
    if ( m_bAttributes && !oItem.r_Attr().IsEmpty ( ) )
    {
      P3PmsgCurs& oCurs = oItem.r_Attr().r_Curs ( );
      for ( int i = 0; oCurs.Goto ( i ); i++ )
      {
        P3PmsgItem& oChild = oCurs.r_item ( );
        RenderNode ( oChild, oChild.c_name ( ), nullptr, true, nDepth );
      }
    }
    if ( !oItem.r_Desc().IsEmpty ( ) )
    {
      P3PmsgCurs& oCurs = oItem.r_Desc().r_Curs ( );
      for ( int i = 0; oCurs.Goto ( i ); i++ )
      {
        P3PmsgItem& oChild = oCurs.r_item ( );
        RenderNode ( oChild, oChild.c_name ( ), nullptr, false, nDepth );
      }
    }
}

//
//  The <li> of a node with no children.
//
//  Parameters:  const CString& strKey
//               The inside of the key span, ALREADY ESCAPED - it carries
//               markup of its own for the @ and for an index
//
void
PrintHTML::RenderLeaf ( P3PmsgData& oData, const CString& strKey, bool bAttr,
                        int nDepth )
{
    AppendBreak ( nDepth );
    Append ( _T("<li class=\"p2p-row") );
    if ( bAttr )
      Append ( _T(" p2p-isattr") );
    Append ( _T("\"><span class=\"p2p-key\">") );
    Append ( (LPCTSTR)strKey );
    Append ( _T("</span>") );
    //  No badge on a null. The cell still has a type byte and TypeName still
    //  answers for it, but a row reading "null   null" says nothing twice.
    if ( RenderValue ( oData ) != Value_Null )
      RenderBadge ( oData );
    Append ( _T("</li>") );
}

//
//  The value, in the colour of its KIND rather than of its type - a string, a
//  number, a bool and a null are four different things to a reader, and an
//  int32 against an int64 is not. The type is in the badge for anyone who
//  wants it.
//  NOTES: STRINGS ARE QUOTED. Without the quotes an empty string renders as
//         nothing at all, which is indistinguishable from a null on a page
//         and is not the same thing in a store.
//       : Returns the kind, because the caller has one more decision to make
//         with it - a null cell still has a type byte, and a row that reads
//         "null   null" says the same word twice.
//
MsgPrint::ValueKind_e
PrintHTML::RenderValue ( P3PmsgData& oData )
{
    CString           strValue;
    const ValueKind_e eKind = RenderScalar ( oData, strValue );
    switch ( eKind )
    {
      case Value_Bool:
        Append ( _T("<span class=\"p2p-val p2p-bool\">") );
        Append ( (LPCTSTR)Escape ( (LPCTSTR)strValue ) );
        Append ( _T("</span>") );
        break;

      case Value_Number:
        Append ( _T("<span class=\"p2p-val p2p-num\">") );
        Append ( (LPCTSTR)Escape ( (LPCTSTR)strValue ) );
        Append ( _T("</span>") );
        break;

      case Value_String:
        Append ( _T("<span class=\"p2p-val p2p-str\">&quot;") );
        Append ( (LPCTSTR)Escape ( (LPCTSTR)strValue ) );
        Append ( _T("&quot;</span>") );
        break;

      case Value_Null:
      default:
        Append ( _T("<span class=\"p2p-val p2p-nul\">null</span>") );
        break;
    }
    return eKind;
}

void
PrintHTML::RenderBadge ( P3PmsgData& oData )
{
    if ( !m_bTypes )
      return;

    const CString strType = TypeName ( oData );
    if ( strType.IsEmpty ( ) )
      return;

    Append ( _T("<span class=\"p2p-badge\">") );
    Append ( (LPCTSTR)Escape ( (LPCTSTR)strType ) );
    Append ( _T("</span>") );
}

//
//  What a collapsed node contains, so it can be read without opening it.
//
CString
PrintHTML::Summarise ( P3PmsgItem& oItem ) const
{
    CString strOut;
    if ( oItem.r_Object().IsList ( ) )
    {
      strOut += _T("list &middot; ");
      strOut += Plural ( (int)dynamic_cast<P3PmsgList&>(oItem).GetCount ( ),
                         _T("entry"), _T("entries") );
    }
    else if ( oItem.r_Object().IsVect ( ) )
    {
      strOut += _T("vector &middot; ");
      strOut += Plural ( (int)dynamic_cast<P3PmsgVect&>(oItem).GetCount ( ),
                         _T("element"), _T("elements") );
    }

    const int nDesc = (int)oItem.r_Desc().GetCount ( );
    const int nAttr = m_bAttributes ? (int)oItem.r_Attr().GetCount ( ) : 0;
    if ( nDesc > 0 )
    {
      if ( !strOut.IsEmpty ( ) )
        strOut += _T(" &middot; ");
      strOut += Plural ( nDesc, _T("field"), _T("fields") );
    }
    if ( nAttr > 0 )
    {
      if ( !strOut.IsEmpty ( ) )
        strOut += _T(" &middot; ");
      strOut += Plural ( nAttr, _T("attribute"), _T("attributes") );
    }
    return strOut;
}

//
//  Everything above the tree. Written AFTER it, which is what lets the count
//  line be a fact rather than a second opinion.
//  NOTES: In fragment form the <style> lands in the body rather than the head,
//         which no browser has ever objected to and the validator will. It is
//         the price of a fragment that carries its own appearance; SetStyle
//         (false) is the way out for a host page that has its own.
//
void
PrintHTML::RenderHead ( const CString& strTitle )
{
    if ( m_bDocument )
    {
      Append      ( _T("<!DOCTYPE html>") );
      AppendBreak ( 0 );
      Append      ( _T("<html lang=\"en\">") );
      AppendBreak ( 0 );
      Append      ( _T("<head>") );
      AppendBreak ( 0 );
      Append      ( _T("<meta charset=\"utf-8\">") );
      AppendBreak ( 0 );
      Append      ( _T("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">") );
      AppendBreak ( 0 );
      Append      ( _T("<title>") );
      Append      ( (LPCTSTR)Escape ( (LPCTSTR)strTitle ) );
      Append      ( _T("</title>") );
      AppendBreak ( 0 );
    }

    if ( m_bStyle )
    {
      Append      ( _T("<style>") );
      AppendBreak ( 0 );
      Append      ( PRINTHTML_CSS );
      Append      ( _T("</style>") );
      AppendBreak ( 0 );
    }

    if ( m_bDocument )
    {
      Append      ( _T("</head>") );
      AppendBreak ( 0 );
      Append      ( _T("<body>") );
      AppendBreak ( 0 );
    }

    Append      ( _T("<main class=\"p2p\">") );
    AppendBreak ( 0 );
    Append      ( _T("<header class=\"p2p-head\"><h1 class=\"p2p-title\">") );
    Append      ( (LPCTSTR)Escape ( (LPCTSTR)strTitle ) );
    Append      ( _T("</h1><p class=\"p2p-meta\">") );

    Append ( _T("<span class=\"p2p-chip\">") );
    Append ( (LPCTSTR)Plural ( m_nNodes, _T("node"), _T("nodes") ) );
    Append ( _T("</span>") );
    if ( m_nAttrs > 0 )
    {
      Append ( _T("<span class=\"p2p-chip\">") );
      Append ( (LPCTSTR)Plural ( m_nAttrs, _T("attribute"), _T("attributes") ) );
      Append ( _T("</span>") );
    }
    if ( m_nArrays > 0 )
    {
      Append ( _T("<span class=\"p2p-chip\">") );
      Append ( (LPCTSTR)Plural ( m_nArrays, _T("collection"), _T("collections") ) );
      Append ( _T("</span>") );
    }

    Append      ( _T("</p></header>") );
    AppendBreak ( 0 );
}

void
PrintHTML::RenderFoot ( )
{
    AppendBreak ( 0 );
    Append      ( _T("<footer class=\"p2p-foot\">Msgcore store &middot; ")
                  _T("rendered by MsgcoreUtils</footer>") );
    AppendBreak ( 0 );
    Append      ( _T("</main>") );
    if ( m_bDocument )
    {
      AppendBreak ( 0 );
      Append      ( _T("</body>") );
      AppendBreak ( 0 );
      Append      ( _T("</html>") );
    }
}

//
//  HTML text escaping.
//  NOTES: The quotation mark is escaped along with the three that have to be.
//         Nothing here is written into an attribute today, and a helper that
//         is safe in only one of the two places it might be used is a trap for
//         whoever adds the second.
//
CString
PrintHTML::Escape ( LPCTSTR lpszText )
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
        default:       strOut += *p;           break;
      }
    }
    return strOut;
}

CString
PrintHTML::Plural ( int nCount, LPCTSTR lpszOne, LPCTSTR lpszMany )
{
    CString strOut;
    strOut.Format ( _T("%d %s"), nCount, nCount == 1 ? lpszOne : lpszMany );
    return strOut;
}

///////////////////////////////////////////////////////////////////////
//  Rendering operators

PrintHTML&
operator >> ( P3PmsgItem& oItem, PrintHTML& oHTML )
{
    oHTML.Render ( oItem );
    return oHTML;
}

PrintHTML&
operator << ( PrintHTML& oHTML, P3PmsgItem& oItem )
{
    oHTML.Render ( oItem );
    return oHTML;
}
