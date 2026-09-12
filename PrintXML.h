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
//  PrintXML - a Msgcore store as an XML document.
//
//      P2PmsgMgr oMgr ( L"person.p2p" );
//      PrintXML  oXML;
//      oMgr >> oXML;
//      oXML.Save ( L"person.xml" );
//
//  THE MAPPING. One ELEMENT per node, named after the node, and everything
//  the document needs to say ABOUT a node rather than store IN it lives in
//  attributes carrying the reserved p2p: prefix:
//
//      <Person xmlns:p2p="urn:ivyware:msgcore:p2p">
//        <Surname p2p:type="WSTR16">Mann</Surname>
//        <Age p2p:type="int32">42</Age>
//        <Address>
//          <type p2p:attr="true" p2p:type="WSTR16">postal</type>
//          <Street p2p:type="WSTR16">Smith St</Street>
//        </Address>
//        <Flat p2p:list="true">
//          <p2p:item p2p:type="int32">1</p2p:item>
//          <p2p:item p2p:type="int32">2</p2p:item>
//        </Flat>
//      </Person>
//
//  WHY THE MSGCORE ATTRIBUTES ARE NOT XML ATTRIBUTES, which is the first thing
//  anyone looks for. A Msgcore attribute is a P3PmsgItem: it has a name, a
//  value, a type, and attributes and descendants of its OWN. An XML attribute
//  holds a string. Mapping one onto the other reads beautifully for the flat
//  case and loses everything the moment an attribute carries a subtree, so the
//  attributes are elements like everything else and p2p:attr="true" is what
//  says which branch a member came from. It is the same decision the JSON
//  dialect makes by prefixing the key with @, and it holds for the same reason.
//
//  THE RESERVED VOCABULARY, all of it under the p2p prefix and none of it able
//  to collide with a store's own names - T_AttrDelim and the colon are both
//  refused by P3Pmsg_IsValidItemname, so no item can be called p2p:anything:
//
//    p2p:type      Msgcore's own name for the cell's type - "int32", "WSTR16".
//                  This is what makes an XML round trip TYPE-EXACT where a
//                  JSON one cannot be, and it is why SetTypes() defaults on.
//    p2p:null      The cell is NULL, which is not the same as empty. An empty
//                  element is a present, empty string.
//    p2p:name      The node's real Msgcore name, present only when XML cannot
//                  spell it as a tag - see below.
//    p2p:attr      This element is an ATTRIBUTE of its parent rather than a
//                  descendant.
//    p2p:list      The element holds the entries of a P3PmsgList,
//    p2p:vect      or the elements of a P3PmsgVect. An array says nothing
//                  about which it was, and the store distinguishes them.
//    <p2p:item>    One unnamed entry or element of the above.
//    <p2p:value>   The node's OWN value, for a node that also carries
//                  attributes or descendants. A leaf writes its value as its
//                  own text and has no p2p:value; a node with children cannot,
//                  because mixed content is not something a reader should have
//                  to disentangle. Same role as the .value key in JSON.
//
//  TAG NAMES ARE SANITISED, and the real name is never lost. Msgcore allows
//  characters in an item name that XML does not allow in a tag - a space, a
//  leading digit, an ampersand - so everything outside [A-Za-z0-9_-] folds to
//  an underscore, a name that cannot start one takes an underscore prefix, and
//  p2p:name carries the original WHENEVER THE TWO DIFFER. Non-ASCII letters
//  fold too, which XML would in fact accept: the exact set of legal name
//  characters is a table that varies by XML version, and a tag whose spelling
//  depends on which one a reader implements is worse than a tag that is
//  obviously ASCII with the real name beside it.
//
//  THE PREFIX IS FIXED, NOT RESOLVED. The render declares
//  xmlns:p2p="urn:ivyware:msgcore:p2p" on the root element so the document is
//  namespace-well-formed, and the reader matches the literal prefix rather
//  than resolving it. Rebinding p2p to another URI, or binding this URI to
//  another prefix, produces a document this reader will not understand -
//  which is a limit worth stating rather than a namespace processor worth
//  writing for a format that emits exactly one prefix.
//
//  VALUES follow MsgPrint::RenderScalar exactly as the other dialects do, so a
//  blob is base64 and a time is ISO-8601. Two XML-specific points:
//
//    * A CARRIAGE RETURN is written as &#13;. An XML reader normalises a raw
//      one to a line feed before the application ever sees it, so a stored
//      "\r\n" would come back "\n" - the escape is what survives that.
//    * A CONTROL CHARACTER below U+0020 other than tab, CR and LF IS DROPPED.
//      XML 1.0 cannot carry one at all, not even as a character reference, so
//      there is no encoding to choose - only whether to emit a document no
//      parser will read. See the round-trip table in the Readme.
//
#pragma once

#include "MsgPrint.h"

class MsgcoreUtils_EXT PrintXML : public MsgPrint
{
    // Constructors and destructor
    public:
        PrintXML ( ) noexcept;

        //  Seeds the renderer with a DOCUMENT, exactly as SetText does, so a
        //  parse needs no second statement:
        //
        //      PrintXML oXML ( LR"(<Settings><window>...</window></Settings>)" );
        //      oMgr << oXML;
        //
        //  TEXT, NOT A FILENAME, and explicit for that reason. Load() takes the
        //  filename, and both take an LPCTSTR, so an implicit conversion here
        //  would silently accept a path where a document belongs and parse the
        //  path as XML. Direct initialisation, which is what the spelling above
        //  is, costs nothing for it.
        //
        //  Not noexcept, unlike the default constructor: holding the text is an
        //  allocation.
      explicit
        PrintXML ( LPCTSTR lpszText );

        PrintXML ( const PrintXML& rhs );

      virtual
       ~PrintXML ( );

    // Operators
    public:
      PrintXML&
        operator = ( const PrintXML& rhs );

    // Rendering
    public:
      virtual MsgPrint&
        Render ( P3PmsgItem& oItem );

    // Parsing
    //  NOTES: Reads the XML this renderer HOLDS into oItem, replacing what
    //         oItem held. The text comes from a Render, from the constructor
    //         above, from Load() or from SetText(); nothing else puts any
    //         there.
    //       : There is no counterpart to PrintJson's SetRooted, because XML has
    //         no unrooted form: a document is exactly one element, that element
    //         names the node, and the name is discarded on the way in for the
    //         reason the JSON wrapper key is - a document identifies a node, it
    //         does not rename one.
    public:
      virtual MsgPrint&
        Parse ( P3PmsgItem& oItem );

    // Properties
    public:
      //  Whether the XML declaration is written:
      //
      //      <?xml version="1.0" encoding="UTF-8"?>
      //
      //  On by default, and it says UTF-8 because that is what Save() writes.
      //  Off is the form to use when the document is about to be embedded in
      //  another, where a second declaration part-way through is not legal.
      //  The READER accepts a document either way - a declaration is optional
      //  in XML itself, so believing this flag would refuse documents that are
      //  perfectly well-formed.
      bool
        SetDeclaration ( bool bDeclaration ) noexcept;
      bool
        GetDeclaration ( ) const noexcept;

      //  Whether p2p:type is written on every cell that has a type. On by
      //  default: it is the whole of the difference between an XML round trip
      //  and a JSON one, which cannot tell an int32 from an int64 or a WSTR16
      //  holding "42" from a number.
      //
      //  Turning it off produces a cleaner document that a foreign consumer
      //  will find easier to read, AT THE COST OF THAT EXACTNESS - the reader
      //  then infers from the text, so the "42" becomes an integer and the
      //  int32 comes back an int64. Exactly the JSON bargain, offered here as
      //  a switch rather than as a property of the dialect.
      bool
        SetTypes ( bool bTypes ) noexcept;
      bool
        GetTypes ( ) const noexcept;

    // Implementation
    private:
      //  Emits one complete element. lpszName is the node's Msgcore name, or
      //  nullptr for an unnamed entry of a list or vector, which becomes
      //  <p2p:item>. bAttr marks it as one of its parent's attributes, bRoot
      //  as the element that carries the namespace declaration.
      void
        RenderNode ( P3PmsgItem& oItem, LPCTSTR lpszName, bool bAttr,
                     bool bRoot, int nDepth );
      void
        RenderList ( P3PmsgList& oList, int nDepth );
      void
        RenderVect ( P3PmsgVect& oVect, int nDepth );
      void
        RenderAttrChildren ( P3PmsgAttr& oAttr, int nDepth );
      void
        RenderDescChildren ( P3PmsgDesc& oDesc, int nDepth );
      //  The <p2p:value> element of a node that carries children too. Emits
      //  nothing at all when the node has no value of its own, which is the
      //  common case for a pure container.
      void
        RenderOwnValue ( P3PmsgData& oData, int nDepth );
      //  p2p:type="..." for a cell that has a name for its type, or nothing.
      CString
        TypeAttr ( P3PmsgData& oData ) const;

      //  Character data, and an attribute value, which differ in what has to
      //  be escaped: an XML reader normalises whitespace inside an attribute
      //  to spaces, so a tab or a newline in one has to be a reference.
      static CString
        EscapeText ( LPCTSTR lpszText );
      static CString
        EscapeAttr ( LPCTSTR lpszText );
      //  The tag name for a Msgcore item name - see the sanitising note above.
      static CString
        XmlName ( LPCTSTR lpszName );

    // Reading
    //  NOTES: A recursive descent over the held text by character offset, the
    //         same shape as PrintJson's. Each one leaves nPos on the first
    //         character it did not consume and returns false having called
    //         SetError.
    private:
      //  The attributes of one start tag, in document order.
      struct XmlAttrs
      {
        std::vector<CString> oNames;
        std::vector<CString> oValues;
        //  The value under lpszName, or nullptr when it is absent - which is
        //  not the same as present and empty, and p2p:name relies on that.
        LPCTSTR
          Find ( LPCTSTR lpszName ) const;
        //  Find() == "true". Every p2p flag is written that way and nothing
        //  else counts as set, so a hand-edited "1" is ignored rather than
        //  guessed at.
        bool
          Flag ( LPCTSTR lpszName ) const;
      };

      //  Everything before the root element - the declaration, comments,
      //  processing instructions and a DOCTYPE, all optional and all skipped.
      bool
        ReadProlog ( int& nPos );
      //  Consumes '<' through '>', leaving nPos after it. bEmpty says the tag
      //  closed itself.
      bool
        ReadStartTag ( CString& strTag, XmlAttrs& oAttrs, bool& bEmpty, int& nPos );
      //  Everything after the start tag: the content, the end tag, and the
      //  assembly of the MsgValue the two describe.
      bool
        ReadElementBody ( MsgValue& oOut, const CString& strTag,
                          const XmlAttrs& oAttrs, bool bEmpty,
                          int& nPos, int nDepth );
      bool
        ReadName ( CString& strOut, int& nPos ) const;
      bool
        ReadAttrValue ( CString& strOut, int& nPos );
      //  Character data up to the next '<', with references resolved and CDATA
      //  sections taken verbatim.
      bool
        ReadCharData ( CString& strOut, int& nPos );
      bool
        ReadReference ( CString& strOut, int& nPos );
      //  Comments, processing instructions and CDATA openers - the three
      //  things a '<!' or '<?' can begin. Returns false when it consumed
      //  nothing, which is how the content loop knows it has an element.
      bool
        SkipComment ( int& nPos );
      bool
        SkipPI ( int& nPos );
      void
        SkipSpace ( int& nPos ) const;
      //  Whether the text from nPos begins with lpszWord. Does NOT consume.
      bool
        LooksLike ( LPCTSTR lpszWord, int nPos ) const;

    // Attributes
    private:
      bool     m_bDeclaration{true};
      bool     m_bTypes{true};
};

///////////////////////////////////////////////////////////////////////
//  Rendering operators
//  NOTES: Declared on P3PmsgItem rather than on P2PmsgMgr, and that ONE
//         declaration is what makes the documented usage work: P2PmsgMgr
//         derives from P3PmsgItem, so a manager binds to it directly, and so
//         does any subtree, list or vector inside one.
//       : Both spellings say the store goes INTO the renderer, and both return
//         the renderer, so the text is reachable straight from the expression.
MsgcoreUtils_EXT PrintXML&
operator >> ( P3PmsgItem& oItem, PrintXML& oXML );

MsgcoreUtils_EXT PrintXML&
operator << ( PrintXML& oXML, P3PmsgItem& oItem );

///////////////////////////////////////////////////////////////////////
//  Parsing operators
//  NOTES: The same two spellings pointing the other way, and the arrow is the
//         whole of the difference - it points at the thing being written:
//
//             oMgr >> oXML;   oXML << oMgr;    render - store into text
//             oMgr << oXML;   oXML >> oMgr;    parse  - text into store
//
//       : ALL FOUR RETURN THE RENDERER, so the outcome of a parse is reachable
//         from the expression - (oMgr << oXML).GetError().
MsgcoreUtils_EXT PrintXML&
operator << ( P3PmsgItem& oItem, PrintXML& oXML );

MsgcoreUtils_EXT PrintXML&
operator >> ( PrintXML& oXML, P3PmsgItem& oItem );
