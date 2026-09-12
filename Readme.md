# MsgcoreUtils

Renders a [Msgcore](../Msgcore) store as **JSON**, as **XML**, as **PHP class
declarations** or as a **styled HTML page**, and reads the first three back into a store.

Four dialects, one base class, and the split between them is not what it looks like: the
three data formats each write their own punctuation and each read their own, but every one
of them parses into the *same* value tree, so the code that writes a tree into a store is
written once. HTML is the odd one and says so — it is a view, it has no parse, and
`oMgr << oHTML` is a compile error rather than a runtime disappointment.

A separate component that sits **on top of** Msgcore and adds nothing to it — nothing in
that library knows this one exists, because reading a store out and writing one back are
things done *to* the object model rather than parts of it. It has its own solution, its own CMake project and its own eight
configurations, and it is not in Msgcore's install/export set.

## It needs the Msgcore checkout beside it

```
<anywhere>/
├── Msgcore/          the library — clone it first
└── MsgcoreUtils/     this
```

That is not a convention, it is the build. Every source here reaches
`../Msgcore/stdafx.h`, every header reaches `../Msgcore/Msgcore.h`, and the DLL
configurations link `Msgcore.lib` out of `$(WDMSCS_LIB)`, which Msgcore's
`Directory.Build.props` defines — `Directory.Build.props` here imports it rather than
restating it, so there is one definition of that staging directory and it stays pointed at
`Msgcore\lib\<Platform>\<Configuration>`. Both build systems say so early and clearly when
the sibling is missing: MSBuild fails in `MsgcoreUtilsRequireSiblingMsgcore`, CMake in a
`FATAL_ERROR` naming the directory it looked in.

## Building

Windows — `MsgcoreUtils(2026).vcxproj` is authoritative and carries all eight
configurations. The solution includes Msgcore's project as well, so build order resolves
and the pair opens together:

```powershell
& $msbuild "MsgcoreUtils(2026).sln" /p:Configuration=Debug /p:Platform=x64
& $msbuild "MsgcoreUtils(2026).vcxproj" /p:Configuration=Debug /p:Platform=x64
```

The solution's platform is `x86` where the project's is `Win32` — the same naming
difference Msgcore's solution has, and passing the project's spelling to the solution
fails with MSB4126.

Linux — CMake, which adds `../Msgcore` itself and then builds this library against it:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`MSCS_BUILD_STATIC_LIBS=ON` adds the `msgcoreutils_static` target beside the shared one,
mirroring Msgcore's own switch.

## Using it

```cpp
#include "Msgcore/P2PmsgMgr.h"
#include "MsgcoreUtils/PrintJson.h"
#include "MsgcoreUtils/PrintXML.h"
#include "MsgcoreUtils/PrintPHP.h"
#include "MsgcoreUtils/PrintHTML.h"

P2PmsgMgr oMgr ( L"person.p2p" );

PrintJson oJson;
oMgr >> oJson;                          // the store as a JSON document
_tprintf ( _T("%s\n"), oJson.c_str() );

PrintXML oXML;
oMgr >> oXML;                           // as XML, types and all
oXML.Save ( L"person.xml" );

PrintPHP oPHP;
oPHP << oMgr;                           // the same operation, said from the renderer's
oPHP.Save ( L"Person.php", false );     // side: false = no UTF-8 BOM, for a served file

PrintHTML oHTML;
oHTML << oMgr;                          // as a page, stylesheet included
oHTML.Save ( L"person.html" );
```

Those renders are spelled opposite ways round on purpose: `oMgr >> oPHP;` and
`oPHP << oMgr;` are one operation, and the pair below is the same choice on the parse side.
The renderer holds the text either way round, so a document from anywhere but a preceding
render is put there first. There are three doors: the **constructor** takes the document
itself, `Load` reads it out of a file, and `SetText` replaces it on a renderer already in
hand.

```cpp
PrintJson oJson ( LR"({ "Surname": "Mann", "Age": 42 })" );   // straight from a literal

P2PmsgMgr oMgr ( L"person.p2p" );
oMgr << oJson;                          // the document, into the store
if ( *oJson.GetError ( ) )
  _tprintf ( _T("line %d: %s\n"), oJson.GetErrorLine ( ), oJson.GetError ( ) );

P2PmsgMgr oOther ( L"other.p2p" );
PrintXML  oXML;
oXML.Load ( L"person.xml" );            // or out of a file
oXML >> oOther;                         // mirror of oOther << oXML;
if ( *oXML.GetError ( ) )
  _tprintf ( _T("line %d: %s\n"), oXML.GetErrorLine ( ), oXML.GetError ( ) );

PrintPHP oPHP;
oPHP.SetText ( strPHP );                // or onto one you are already holding
```

Those constructors take a **document, not a filename**, and are `explicit` for exactly that
reason: `Load` takes the filename, both take an `LPCTSTR`, and an implicit conversion would
have let a path be passed where a document belongs and then parsed the path itself. Direct
initialisation is what a caller writes anyway, so the keyword costs the spelling above
nothing. `PrintHTML` has no such constructor, for the same reason it has no parse — there
would be nothing to seed it *for*.

**The arrow always points at the thing being written**, and that is the whole of the
notation:

| | store into text | text into store |
|---|---|---|
| written from the store | `oMgr >> oJson;` | `oMgr << oJson;` |
| written from the renderer | `oJson << oMgr;` | `oJson >> oMgr;` |

All four return the **renderer**, whichever side it is written on, so the outcome is
reachable from the expression: `(oMgr << oJson).GetError()`. A parse has to be asked whether
it worked, which is why returning the store from the two right-hand forms would have hidden
the one thing that matters about them.

Each pair above is one operation written from either end, and both spellings are declared
rather than one of them being a mistake the compiler happens to reject: which reads better
depends on which side the caller is thinking about. Two *different* stores appear in the
parse example for a reason — a parse replaces its target, so pointing both at `oMgr` would
have left it holding the XML document alone.

`PrintHTML` has **only the top row**. There is no `oMgr << oHTML` and no `oHTML >> oMgr`,
because a page is a rendering of a store the way a printed table is a rendering of a
spreadsheet — complete enough to read, and not the thing itself. Leaving those two
undeclared is deliberate: it makes the mistake a compile error rather than a runtime one.
`Parse()` still exists, because `MsgPrint` declares it, and reports the refusal through
`GetError()` for a caller holding a `MsgPrint&` that cannot know which dialect is behind
it — leaving the target untouched, exactly as a failed parse does everywhere else here.

Every one of these is declared on `P3PmsgItem`, not on `P2PmsgMgr`. That is what makes
the lines above work *and* lets any subtree, list or vector be rendered — or written into
— on its own: `P2PmsgMgr` derives from `P3PmsgItem`.

**Both directions replace.** A render replaces the renderer's contents, because no dialect
here has a concatenation that stays valid: two JSON documents joined end to end are not a
JSON document, two XML documents are not one either — a document is exactly one root
element — and two PHP renders declare every class twice. A parse replaces the target's
value, attributes and descendants, for the same kind of reason — half a document merged
into a live node describes a state the store was never in. So `<<` does not chain the way a
stream's does: `oJson << oItem1 << oItem2` compiles, because it returns the renderer, and
leaves `oItem2` alone in it.

Nothing is written until the **whole** document has parsed. A parse failure leaves the target
exactly as it was and says where it stopped, rather than truncating a node and filling it
with as much as it managed to read.

All four describe the same store the same way. A plain field is a value; a field that
carries attributes or descendants is a structure whose members are its attributes (under
their name prefixed `@`), its descendants (under their own names) and, if it has one, its
own value (under `.value`); a list or vector is an array, under `.items` when the node
carries more besides. **Those three key spaces cannot collide**, and that is a property of
the message format rather than of the renderer: `P3Pmsg_IsValidItemname` rejects both `.`
and `@` in an item name.

| | JSON | XML | PHP | HTML |
|---|---|---|---|---|
| structure | object | element with child elements | a **class**, instantiated from the parent's `__construct` | a collapsible `<details>` |
| list / vector | array | `p2p:list` / `p2p:vect`, entries as `<p2p:item>` | `array(...)` | a numbered sub-list |
| attributes | key prefixed `@` | element marked `p2p:attr="true"` | class-level `$_attributes` | a row marked with `@` |
| a node's own value | `.value` key | `<p2p:value>` | `$_value` | shown on the summary line |
| type | not carried | `p2p:type="int32"` | the docblock over the property | a badge at the end of the row |
| blob | base64 string | base64 string | base64 string — `base64_decode` is not a constant expression, so the decode is the consumer's | base64, shown in full |
| time | ISO-8601 local-time string | the same string | the same string | the same string |
| NaN / ±Inf / `Msgcore_NULL_DBLE` | `null` — JSON has no literal for them | flagged `p2p:null` | `null` | the word `null`, in its own colour |
| stacks (`^`) | not rendered | not rendered | not rendered | not rendered |

XML is the one dialect whose *own* attributes go unused, and that is the interesting
decision in it. A Msgcore attribute is a `P3PmsgItem` — it has a name, a value, a type, and
attributes and descendants of its own; an XML attribute holds a string. Mapping one onto
the other reads beautifully until an attribute carries a subtree, at which point it loses
everything, so attributes are elements like anything else and `p2p:attr="true"` says which
branch a member came from. Everything the document needs to say *about* a node rather than
store *in* one lives under the reserved `p2p:` prefix — which no item name can collide with,
since `P3Pmsg_IsValidItemname` refuses the colon outright.

Tag names are **sanitised, and the real name is never lost**: XML cannot spell a tag
`First Name` or start one with a digit, so anything outside `[A-Za-z0-9_-]` folds to an
underscore and `p2p:name` carries the original whenever the two differ. `<First_Name
p2p:name="First Name" p2p:type="WSTR16">Alex</First_Name>` round trips to exactly the name
it started with.

Stacks are the superseded *history* of a field, not part of its value; a document that
mixed the two would describe several states of the store at once. `P3PmsgItem::Print` does
print them, because a debug dump is exactly where the history is wanted. A parse drops them
along with everything else it replaces, for the same reason: a node cannot keep a past that
never led to its present.

## What a round trip keeps, and what it cannot

A store rendered and read straight back is the same store *structurally* — names, nesting,
attributes, order. What it is not always is type-for-type identical, and the three dialects
that read differ in how close they get, because two of them write the type down and one has
nowhere to. HTML is not in this table at all: it does not read, so it does not round trip.

| | JSON | XML | PHP |
|---|---|---|---|
| names, nesting, attributes, order | kept | kept | kept |
| **numeric type** | `int32` comes back **`int64`**, `float` comes back `double` — JSON has one number type | kept exactly: `p2p:type` carries the Msgcore type name | kept exactly: the docblock carries it |
| **list vs vector** | a vector of nothing but scalars comes back a **list** — both render as an array | kept: `p2p:list` and `p2p:vect` are different attributes | kept for a node's own elements, which the docblock names |
| time, GUID, blob | come back as the **string** they were rendered as | decoded back to the stored type | decoded back to the stored type |
| **empty string vs null** | kept — `""` and `null` are different literals | kept — `p2p:null="true"` says which | kept |
| a name the dialect cannot spell | kept — a JSON key is any string | kept — `p2p:name` carries it | kept — the docblock carries it |
| stacks | not rendered, so not restored | not rendered, so not restored | not rendered, so not restored |

`SetTypes(false)` moves XML into JSON's column deliberately: it drops `p2p:type` for a
cleaner document a foreign consumer will find easier to read, and the reader then infers
from the text, so an `int32` comes back `int64` and a `WSTR16` holding `"42"` comes back a
number. That is the JSON bargain, offered as a switch rather than as a property of the
dialect. Two XML details that are not obvious and are not negotiable:

- **A carriage return is written `&#13;`.** An XML reader normalises a raw one to a line
  feed before the application ever sees it, so a stored `"\r\n"` would come back `"\n"`.
  The escape is what survives, and it does: a value round trips through a third-party
  parser byte for byte.
- **A control character below U+0020 other than tab, CR and LF is dropped.** XML 1.0 has no
  representation for one — not a raw character and not a reference either — so the choice
  is between losing the character and emitting a document no parser will read.

Nothing is ever guessed from the *shape* of a string. A string that looks like an ISO-8601
timestamp stays a string, because a store may legitimately hold one as text and a reader that
promoted it would corrupt the cell it was meant to restore. Where a type is known it is
because the document said so, not because the text resembled something.

That is also why the PHP direction leans on the generated comments, and why the file says do
not edit. `Sanitise` folds a Msgcore item name into a PHP identifier — spaces to
underscores, duplicates suffixed — so the only surviving record of the real name is the
`(Msgcore 'Name')` above the property. Strip the docblocks and the names and the types are
gone. Reading PHP is reading **this renderer's own output**: JSON has a grammar anything can
write, PHP source does not, so the honest scope of that direction is the round trip.

XML is not like that. Its reader takes documents from **elsewhere** — a prolog, comments,
processing instructions, a DOCTYPE, CDATA sections, single-quoted attributes and
self-closing tags are all accepted, and a hand-written document with none of the reserved
attributes in it parses fine. Where it is strict is where silence would corrupt a value: an
entity reference it does not know is an error rather than a literal left in the string, and
so is an element under the `p2p:` prefix that this dialect does not define. It is one
concession to a document nobody here wrote — text sitting beside child elements, which a
render never produces, is taken as the node's own value.

Two more things worth knowing before pointing a parse at something:

- **`SetRooted` means the same thing to both directions.** A rooted parse consumes the outer
  object and writes what is inside it, and it believes the flag rather than sniffing the
  document — guessing would unwrap `{ "Surname": "Mann" }`, a legitimate unrooted document
  that happens to have one member, into the string `"Mann"`. XML has no counterpart, because
  it has no unrooted form: a document *is* one element. The root element's name is discarded
  on the way in for the reason the JSON wrapper key is — a document identifies a node, it
  does not rename one.
- **The target's own kind is not changed.** A `P3PmsgItem` cannot become a list in place, so a
  document whose root is an array needs a target that is already a list or a vector, and says
  so rather than dropping the elements. Every node *below* the root is created by the parse
  and takes whatever kind the document asks for.

## The HTML page

One file, no script, no second request. The stylesheet is inside it, the collapsing is
`<details>`/`<summary>` rather than JavaScript, and the whole thing opens from a filesystem
and survives being mailed to someone.

```cpp
PrintHTML oHTML;
oHTML.SetTitle    ( L"Person 4412" );   // default: the node's own name
oHTML.SetCollapsed( true );             // default: everything open
oHTML.SetTypes    ( false );            // default: a type badge on every row
oMgr >> oHTML;
oHTML.Save ( L"person.html" );
```

Values are coloured by **kind** rather than by type — a string, a number, a bool and a null
are four different things to a reader and an `int32` against an `int64` is not, so the type
goes in a badge at the end of the row where it can be ignored. Strings are quoted, which is
not decoration: without the quotes an empty string renders as nothing at all, and *nothing*
is what a reader would take for a null. Nothing is truncated and nothing is rounded; a
`base64` blob is shown in full, because a view that quietly shortened a value would be worse
than no view.

The page follows the reader's **system theme**. There is no toggle, because a toggle needs
script and this page has none, and no fixed choice, because a white page in a dark room is
not a decision a renderer should make on someone's behalf.

`SetDocument(false)` emits only the `<main>` element, for embedding in a page of your own.
The stylesheet still comes with it unless `SetStyle(false)` says otherwise — a fragment
styled by nothing is a bulleted list — which does put a `<style>` in the body, legal in no
specification and accepted by every browser. The class names it uses are documented at the
top of `PrintHTML.cpp` and are stable, which is the point of being able to turn the
stylesheet off at all.

## Linkage

Eight configurations mirroring Msgcore's, and they have to:
**[LINKAGE.md](../Msgcore/LINKAGE.md)'s rule applies across the pair**, so a consumer that
links the static Msgcore must link the static MsgcoreUtils. `MsgcoreUtils_EXPORTS` /
`MsgcoreUtils_STATIC` select which, exactly as `Msgcore_EXPORTS` / `Msgcore_STATIC` do
there. The `Lib` configurations define **both** `Msgcore_STATIC` and `MsgcoreUtils_STATIC`:
the first stops Msgcore's headers decorating the symbols this archive calls, the second
stops these headers decorating the ones it defines, and setting only the second produces an
archive whose call sites are still `dllimport`.

It is deliberately **not installed** and not in any export set. Msgcore ships no C++
headers — every one of them reaches `stdafx.h`, and so MFC and `Platform/` — and this
component has no flat C surface at all: `PrintJson`, `PrintXML`, `PrintPHP` and `PrintHTML`
are C++ classes over the C++ object model. Installing the binary would stage a DLL no installed consumer could
declare a call to. Add the install rule the day a flat C entry point exists to install
beside it.

## Licence

Apache-2.0, the same as Msgcore. See [LICENSE](../Msgcore/LICENSE).
