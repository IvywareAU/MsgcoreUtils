# MsgcoreUtils

Renders a [Msgcore](../Msgcore) store as **JSON** or as **PHP class declarations**, and
reads either one back into a store.

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

Windows — `MsgcoreUtils(2022).vcxproj` is authoritative and carries all eight
configurations. The solution includes Msgcore's project as well, so build order resolves
and the pair opens together:

```powershell
& $msbuild "MsgcoreUtils(2022).sln" /p:Configuration=Debug /p:Platform=x64
& $msbuild "MsgcoreUtils(2022).vcxproj" /p:Configuration=Debug /p:Platform=x64
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
#include "MsgcoreUtils/PrintJson.h"
#include "MsgcoreUtils/PrintPHP.h"

P2PmsgMgr oMgr ( L"person.p2p" );

PrintJson oJson;
oMgr >> oJson;                          // the store as a JSON document
_tprintf ( _T("%s\n"), oJson.c_str() );

PrintPHP oPHP;
oMgr >> oPHP;                           // the store as PHP class declarations
oPHP.Save ( L"Person.php", false );     // false = no UTF-8 BOM, for a served file
```

And back the other way. The renderer holds the text either way round, so a document from
anywhere but a preceding render is put there first — `Load` from a file, `SetText` from
memory:

```cpp
PrintJson oJson;
oJson.Load ( L"person.json" );

P2PmsgMgr oMgr ( L"person.p2p" );
oMgr << oJson;                          // the document, into the store
if ( *oJson.GetError ( ) )
  _tprintf ( _T("line %d: %s
"), oJson.GetErrorLine(), oJson.GetError() );
```

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

Either order renders — `oJson << oMgr;` and `oPHP << oMgr;` are the same operation as the
two lines above, spelled from the renderer's side. Both say the store goes *into* the
renderer, and which one reads better depends on which side the caller is thinking about, so
both are declared rather than one of them being a mistake the compiler happens to reject.

Every one of the four is declared on `P3PmsgItem`, not on `P2PmsgMgr`. That is what makes
the lines above work *and* lets any subtree, list or vector be rendered — or written into
— on its own: `P2PmsgMgr` derives from `P3PmsgItem`.

**Both directions replace.** A render replaces the renderer's contents, because neither
dialect has a concatenation that stays valid: two JSON documents joined end to end are not a
JSON document, and two PHP renders declare every class twice. A parse replaces the target's
value, attributes and descendants, for the same kind of reason — half a document merged
into a live node describes a state the store was never in. So `<<` does not chain the way a
stream's does: `oJson << oItem1 << oItem2` compiles, because it returns the renderer, and
leaves `oItem2` alone in it.

Nothing is written until the **whole** document has parsed. A parse failure leaves the target
exactly as it was and says where it stopped, rather than truncating a node and filling it
with as much as it managed to read.

Both dialects describe the same store the same way. A plain field is a value; a field that
carries attributes or descendants is a structure whose members are its attributes (under
their name prefixed `@`), its descendants (under their own names) and, if it has one, its
own value (under `.value`); a list or vector is an array, under `.items` when the node
carries more besides. **Those three key spaces cannot collide**, and that is a property of
the message format rather than of the renderer: `P3Pmsg_IsValidItemname` rejects both `.`
and `@` in an item name.

| | JSON | PHP |
|---|---|---|
| structure | object | a **class**, instantiated from the parent's `__construct` |
| list / vector | array | `array(...)` |
| blob | base64 string | base64 string — `base64_decode` is not a constant expression, so the decode is the consumer's |
| time | ISO-8601 local-time string | the same string |
| NaN / ±Inf / `Msgcore_NULL_DBLE` | `null` — JSON has no literal for them | `null` |
| stacks (`^`) | not rendered | not rendered |

Stacks are the superseded *history* of a field, not part of its value; a document that
mixed the two would describe several states of the store at once. `P3PmsgItem::Print` does
print them, because a debug dump is exactly where the history is wanted. A parse drops them
along with everything else it replaces, for the same reason: a node cannot keep a past that
never led to its present.

## What a round trip keeps, and what it cannot

A store rendered and read straight back is the same store *structurally* — names, nesting,
attributes, order. What it is not is type-for-type identical, and the two dialects differ in
how close they get, because one of them writes the type down and the other has nowhere to.

| | JSON | PHP |
|---|---|---|
| names, nesting, attributes, order | kept | kept |
| **numeric type** | `int32` comes back **`int64`**, `float` comes back `double` — JSON has one number type | kept exactly: the docblock carries the Msgcore type name |
| **list vs vector** | a vector of nothing but scalars comes back a **list** — both render as an array | kept for a node's own elements, which the docblock names |
| time, GUID, blob | come back as the **string** they were rendered as | decoded back to the stored type |
| stacks | not rendered, so not restored | not rendered, so not restored |

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

Two more things worth knowing before pointing a parse at something:

- **`SetRooted` means the same thing to both directions.** A rooted parse consumes the outer
  object and writes what is inside it, and it believes the flag rather than sniffing the
  document — guessing would unwrap `{ "Surname": "Mann" }`, a legitimate unrooted document
  that happens to have one member, into the string `"Mann"`.
- **The target's own kind is not changed.** A `P3PmsgItem` cannot become a list in place, so a
  document whose root is an array needs a target that is already a list or a vector, and says
  so rather than dropping the elements. Every node *below* the root is created by the parse
  and takes whatever kind the document asks for.

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
component has no flat C surface at all: `PrintJson` and `PrintPHP` are C++ classes over the
C++ object model. Installing the binary would stage a DLL no installed consumer could
declare a call to. Add the install rule the day a flat C entry point exists to install
beside it.

## Licence

Apache-2.0, the same as Msgcore. See [LICENSE](../Msgcore/LICENSE).
