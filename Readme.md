# MsgcoreUtils

Renders a [Msgcore](../Msgcore) store as **JSON** or as **PHP class declarations**.

A separate component that sits **on top of** Msgcore and adds nothing to it — nothing in
that library knows this one exists, because a renderer is a consumer of the store rather
than part of it. It has its own solution, its own CMake project and its own eight
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

`operator>>` is declared on `P3PmsgItem`, not on `P2PmsgMgr`. That one declaration is what
makes the line above work *and* lets any subtree, list or vector be rendered on its own —
`P2PmsgMgr` derives from `P3PmsgItem`. It **replaces** the renderer's contents rather than
appending, because neither dialect has a concatenation that stays valid: two JSON documents
joined end to end are not a JSON document, and two PHP renders declare every class twice.

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
print them, because a debug dump is exactly where the history is wanted.

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
