# EKM1 Keyboard Layout Format Specification

## Scope

This document defines the EKM1 on-disk text format used to describe USB HID keyboard layouts in EXOS.

Reference implementation:
- Loader and parser behavior: `kernel/source/drivers/input/KeyLayout-HID.c`
- Data model and constants: `kernel/include/drivers/Keyboard.h`

## File Identity and Encoding

- File encoding: UTF-8 text.
- Required first line: `EKM1`.
- Line tokenization: whitespace-separated tokens.
- Comments: start with `#`.
- Tokens are case-sensitive.

Directive ordering:
- `levels` must appear before any `map` directive.
- Other directives are order-independent.

The loader rejects malformed directives and out-of-range values.

## Directives

- `code <layout_code>`
  - Required.
  - Unique layout identifier string.
  - Example: `code en-US`.

- `levels <count>`
  - Optional.
  - Decimal level count in range `1..4`.
  - Default: `1` when omitted.

- `map <usage_hex> <level_dec> <vk_hex> <ascii_hex> <unicode_hex>`
  - Maps one HID usage to one key mapping at one level.
  - `usage_hex` range: `0x04..0xE7` (HID usage page `0x07`).
  - `level_dec` range: `0..levels-1`.
  - `vk_hex` range: `0x00..0xFF`.
  - `ascii_hex` range: `0x00..0xFF`.
  - `unicode_hex` range: `0x0000..0xFFFF`.
  - Each `(usage, level)` pair may appear only once.

- `dead <dead_unicode_hex> <base_unicode_hex> <result_unicode_hex>`
  - Defines one dead-key composition rule.
  - Maximum entries: `128`.

- `compose <first_unicode_hex> <second_unicode_hex> <result_unicode_hex>`
  - Defines one compose-sequence rule.
  - Maximum entries: `256`.

## Recommended Level Semantics

- Level `0`: base
- Level `1`: shift
- Level `2`: AltGr
- Level `3`: control

## Example

```text
EKM1
# US QWERTY layout (en-US)
code en-US
levels 2
map 0x04 0 0x30 0x61 0x0061
map 0x04 1 0x30 0x41 0x0041
```
