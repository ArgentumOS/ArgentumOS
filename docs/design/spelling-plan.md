# Spelling and grammar — plan and licence decision

**Status: THE ENGINE IS DEFERRED (user, 2026-09-26, superseding the same day's SymSpell choice). The Foundation
half is being built NOW and depends on no engine.**
**Standing while the engine is open: the base image ships PERMISSIVE dictionary packs only; copyleft dictionaries
are INSTALLABLE packs, never base-image content.**

## 1. What the OS owes, and where the split is

The ledger carries exactly six open rows in `Fundamentals / Spelling and Grammar`:

| Row | Kind | Note |
|---|---|---|
| `NSSpellServer` | class | the server side of a spell-checking service |
| `NSSpellServerDelegate` | protocol | what the service implements |
| `NSGrammarCorrections`, `NSGrammarRange`, `NSGrammarUserDescription` | vars | the three keys a grammar result carries |

**THE ENGINE IS NOT FOUNDATION'S, AND THAT IS APPLE'S OWN SHAPE.** `NSSpellServer` is the SERVER side: an
application implements it and runs it, and clients reach it as a service — on macOS that service is AppleSpell,
whose engine is closed and not part of Foundation. So the engine and its dictionary data belong to a **spell
service** (an Argentum service/bundle), and `libfoundation` stays MIT regardless of what that service links.

That split is also what makes the licence decision below affordable: a weak-copyleft engine or a GPL dictionary
pack obliges *the service binary and the pack*, not the library every application links.

## 2. The licence analysis (verified 2026-09-26)

### 2.1 The project's rule, as already practised

Adopted third-party set (`.gitmodules` + the codec decision of 2026-09-21): musl, toybox, X11/pixman, lcms2,
curl, libjpeg-turbo, giflib, libwebp, libtiff, LibreSSL, FreeType, ICU, LLVM — **MIT / zlib / BSD / Apache-2.0 /
FTL / ISC / Unicode**. REFUSED class: **AGPL and GPL-only** (MuPDF was AGPL-3, Poppler GPL with no commercial
relicensing path; both rejected as "the copyleft class this tree refuses").

**AND ONE PRECEDENT THAT DRAWS THE LINE PRECISELY:** `gperf` (GPL-3) is used as a **build-time tool**. Copyleft at
build time is fine — it is not distributed and not linked into anything we ship. Copyleft in a shipped, linked
library or in shipped data is what the doctrine refuses.

### 2.2 Engines

| Engine | Licence | Morphology | Languages | Build cost here |
|---|---|---|---|---|
| Hunspell 1.x | tri-licence: **MPL-1.1 / LGPL-2.1 / GPL-2.0+** (choose one) | yes | ~90 dictionaries | no new dependency |
| Hunspell 2.x | **LGPL only** — the MPL option is gone | yes | same | — |
| Nuspell | **LGPL-3.0-or-later**, C++17 | yes (pure reimplementation, 3–3.5× faster) | 170 langs via the same dictionaries | ICU (already adopted) + CMake (already adopted) |
| **SymSpell** | **MIT** (changed from LGPL-3.0 in v6.1) | **no** — frequency-list validation + Damerau-Levenshtein suggestions | language-independent; needs a frequency list per language | tiny |

### 2.3 Dictionaries — separate licences, PER LANGUAGE

The engine and the data are licensed separately, and the data is where multilingual support actually gets
decided:

* **Permissive:** `en-US` (MIT & BSD, from SCOWL), `en-CA`, `nl`, `ru` (BSD), `tr` (MIT), `ie` (Apache-2.0), and
  **Hermit Dave's FrequencyWords (MIT)** — a frequency-list source covering ~35 languages.
* **Weak copyleft:** `fr` (MPL-2.0), `en-GB` (LGPL-2.0), `ko` (MPL/LGPL/GPL tri).
* **GPL/AGPL — the refused class:** `de`, `es`, `it` (GPL-3.0-only), `he` (**AGPL-3.0**).

Sources: Hunspell's own README (the MPL-1.1 option and the 1.x/2.x distinction); nuspell.github.io (LGPL-3.0,
ICU-based, dictionary-compatible); SymSpell's README, mirrored at github.com/RULCSoft/SymSpell (MIT since v6.1,
language-independent, frequency dictionaries); the per-language licence table in OpenEmbedded's
`hunspell-dictionaries.bb`; Debian's copyright files for `scowl`, `igerman98` and `hunspell-fr`.

## 3. The decisions (user, 2026-09-26)

1. **THE ENGINE IS DEFERRED, AND THE FIRST CHOICE WAS WITHDRAWN ON LANGUAGE GROUNDS.** SymSpell (MIT) was picked
   earlier the same day and is now CANCELLED: it validates against a word list, which is the wrong instrument for
   the languages this system most needs to serve. A word list cannot recognise a Finnish or Hungarian inflection,
   a Turkish suffix chain or a German compound as a word, and for Chinese, Japanese and Thai it cannot even
   establish where the words END — so adopting it would have produced a checker that is confidently wrong in
   exactly the places a spell checker is judged. The licence was never the problem; the *shape* of the engine was.
   **No engine is chosen, and the choice is not to be made on licence grounds alone** — it is a language question
   first, which is what this revision records.
2. **Data: permissive packs on the base image; copyleft packs installable only.** This STANDS and is
   engine-independent: the permissive set is `en-US`/`en-CA`/`nl`/`ru`/`tr`/`ie` plus FrequencyWords-derived
   lists, and GPL/MPL packs are installable and never base-image content.

## 4. What the decision costs, stated rather than discovered

1. **WORD-LIST VALIDATION IS NOT MORPHOLOGY, AND THE UI MUST NOT PRETEND OTHERWISE.** This is the finding that
   CANCELLED SymSpell (§3.1) and it constrains whatever engine is eventually chosen: a frequency list answers "is
   this string in the list", not "is this a legal word" — the difference is invisible in English and decisive in
   Finnish, Hungarian, Turkish and German, and it is not a difference a UI can paper over.
2. **THE COPYLEFT TIER IS DEFERRED, NOT SOLVED.** A GPL `.dic`/`.aff` pack is data *in the affix format*, so
   consuming those packs later needs an affix engine — which is precisely where the **MPL-1.1 (Hunspell 1.7.x)
   vs LGPL-3 (Nuspell)** question resurfaces. Also note: DERIVING a frequency list from a GPL dictionary makes a
   derivative of that data, so the copyleft packs must ship as the dictionaries they are, not as converted lists.
3. **ANY THIRD-PARTY ENGINE IS VENDORED WITH BOTH CHECKS, NOT ONE.** The withdrawn SymSpell choice illustrated it:
   its canonical implementation is C# and its C++ ports are third-party, with the author stating they "have not
   been tested … whether they are an exact port, error free, provide identical results or are as fast as the
   original" — so vendoring any engine owes a licence verification AND a correctness check against the reference
   behaviour. Applies unchanged to the affix engines, whose reference behaviours are their own test suites.
4. **CJK AND THAI NEED SEGMENTATION BEFORE ANY LOOKUP.** Hunspell/Nuspell have no affix dictionaries for Chinese
   or Japanese, and a "word" frequency list cannot be consulted without knowing where the words end. ICU's break
   iterators are already adopted; the permissive tokenisers are jieba (MIT, zh), Kuromoji (Apache-2.0, ja) and
   MeCab (tri-licence including BSD, ja).
5. **GRAMMAR IS OURS.** The three `NSGrammar*` vars and the server's grammar checking need an engine of our own;
   the third-party grammar checkers are not light (LanguageTool is LGPL-2.1 and Java; Grammalecte is GPL-3).
6. **STANDING POLICY AT ADOPTION:** a `docs/design/self-hosting-packages.md` §6 entry per package (SymSpell, each
   dictionary pack) recording its build and staging requirements — the policy applies when software is adopted,
   which is not yet.

## 5. Next steps

1. **`NSSpellServer` + `NSSpellServerDelegate` + the three grammar vars — 6 rows, and this closes the family.
   BEING BUILT NOW, AND IT IS ENGINE-AGNOSTIC BY CONSTRUCTION:** the server side is the API a SERVICE implements
   and the protocol the service answers; both are written against whatever engine that service eventually links,
   so deferring the engine does not block it. This is the whole reason it goes first.
2. Reopen the engine question ON LANGUAGE GROUNDS once there is a server to plug into: the honest candidates are
   the affix engines (Hunspell 1.7.x under MPL-1.1, Nuspell under LGPL-3) whose morphology is what the withdrawn
   choice lacked, and a CJK/Thai path (ICU break iterators are already adopted; jieba MIT, Kuromoji Apache-2.0,
   MeCab tri-licence incl. BSD) for the languages no affix dictionary covers.
3. Design the installable pack format for the copyleft tier — including the decision about which affix engine
   reads them, and the honest statement that a pack's engine is an added dependency, not part of the base system.
