<picture>
  <source media="(prefers-color-scheme: dark)" srcset=".github/judo-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset=".github/judo.svg">
  <img alt="Judo" src=".github/judo.svg" width="360px">
</picture>

**Judo** is a JSON and JSON5 parser with MISRA C:2012 conformance.
It is written in C99 with no external dependencies.

## Choose Your Standard

Judo offers three different JSON standards to choose from:

* **RFC 4627** - The original JSON standard, "discovered" by Douglas Crockford.
* **RFC 8259** - The latest JSON standard.
* **JSON5** - An unofficial extension of JSON, incorporating features from ECMAScript 5.1 (ES5).

The most notable difference between RFC 4627 and RFC 8259 is that, in the former, the root value must be an array or an object, whereas in the latter, the root value can be any valid JSON value.

### Optional Extensions

Judo supports optional extensions for **comments** and **trailing commas**, which can be enabled for both RFC 4627 and RFC 8259.
When both extensions are enabled, the parser becomes a [JSONC](https://code.visualstudio.com/docs/languages/json#_json-with-comments) parser.
These features are already part of the JSON5 standard.

## Lightweight Interface

Judo is split into two interfaces: the scanner and (optional) parser.
[Judo's scanner](https://railgunlabs.com/judo/manual/api/scanner/) consists of only **three functions**, all of which are idempotent.
The Judo parser builds an in-memory tree and relies on dynamic memory allocation via a custom memory allocator you provide.

## Robust Processing

Judo is robust.
It can process both null and non-null-terminated UTF-8 encoded strings.
It can detect malformed UTF-8 sequences and problematic conditions not fully specified in the JSON specification, such as mismatched UTF-16 surrogate pairs.

## MISRA C:2012 Conformance

The Judo scanner honors all Mandatory, Required, and Advisory rules defined by MISRA C:2012, including Rule 17.2, which requires all functions to be non-recursive.
As a result, Judo does not implement a recursive scanner or parser.

The optional Judo parser honors all MISRA rules except Advisory Rule 11.5.
If strict conformance is required, then the parser can be disabled at configuration time.

The complete compliance table with deviations is [documented here](https://railgunlabs.com/charisma/manual/misra-compliance/).

## Extensively Tested

* Unit tests with 100% branch coverage
* Fuzz tests (libFuzzer and AFL++)
* Out-of-memory tests
* Static analysis
* Valgrind analysis
* Code sanitizers (UBSAN, ASAN, and MSAN)
* Extensive use of assert() and run-time checks

Additionally, all combinations of features and JSON extensions are tested.

## Ultra Portable

Judo is _ultra portable_.
It does **not** require an FPU or 64-bit integers.
It's written in C99 and only requires a few features from libc which are listed in the following table.

| Header | Types | Macros | Functions |
| --- | --- | --- | --- |
| **stdint.h** |  `uint8_t`, `uint16_t`, `int32_t`, `uint32_t` | | |
| **string.h** | | | `memcpy`, `memset`, `memcmp` |
| **stddef.h** | `size_t` | `NULL` | |
| **stdbool.h** | |  `bool`, `true`, `false` | |
| **math.h** | |  `INFINITY`, `NAN` | |
| **assert.h** | |  `assert` | |

## Building

Download the latest release from the [releases page](https://github.com/railgunlabs/judo/releases) and build with

```
$ ./configure
$ make
$ make install
```

or [CMake](https://cmake.org/).

## Support

* [Documentation](https://railgunlabs.com/judo/manual/)
* [Premium Support](https://railgunlabs.com/services/)

Submit patches and bug reports to [railgunlabs.com/contribute](https://railgunlabs.com/contribute/).
Do **not** open a pull request.
The pull request tab is enabled because GitHub does not provide a mechanism to disable it.

## License

Judo is dual-licensed under the GNU Affero General Public License version 3 (AGPL v3) and a proprietary license, which can be purchased from [Railgun Labs](https://railgunlabs.com/judo/license/).

The unit tests are **not** published.
Access to them is granted exclusively to commercial licensees.
