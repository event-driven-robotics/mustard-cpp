---
description: "Audit a binary decode function or file parser for correctness and portability issues. Checks for: no bit-fields, correct masks, little-endian safety, unescape correctness, and off-by-one errors."
agent: agent
tools: [read, search]
argument-hint: "File path or function name to audit (e.g. src/data/events/BinaryEventLoader.cpp)"
---
Audit the binary decode logic in `$ARGUMENTS` for the following issues. Report each finding with: file, line range, issue category, description, and a corrected code snippet.

## Checklist

### 1. No Compiler Bit-Fields
- Flag any `struct` using `: N` bit-field syntax for hardware register layout
- Required fix: replace with explicit `& mask` + shift decode

### 2. Correct Masks and Shifts (DVSEvent 32-bit layout)
Verify the following decode matches the spec exactly:
```
bit  0      → polarity  (mask 0x1)
bits 1–11   → x         (mask 0x7FF, shift right 1)
bits 12–21  → y         (mask 0x3FF, shift right 12)
bits 22–25  → metadata  (mask 0xF,   shift right 22)
bits 26–31  → ignored
```
Flag any mask that is off by one bit, any missing shift, or any sign-extension issue.

### 3. Little-Endian Safety
- Flag any `reinterpret_cast<uint32_t*>` or `memcpy` that assumes host byte order without documentation
- Check that multi-byte reads from file/buffer use explicit byte-order reconstruction if the format mandates little-endian

### 4. Unescape Correctness
Verify the unescape function handles all required escape sequences:
- `\\` → `\` (backslash)
- `\n` → `0x0A`
- `\r` → `0x0D`
- `\0` → `0x00`
- `\x` → `x` (any other char after backslash: emit the char, drop the backslash)
Flag: missing cases, incorrect byte values, per-char heap allocation (should use index-based loop with pre-reserved output).

### 5. Off-by-One / Boundary Errors
- Verify `readChunk(t0, t1)` includes events at exactly `t0` and `t1` (inclusive) and excludes `t1 + 1`
- Check binary-search index lookup for fencepost errors

### 6. Integer Types
- Timestamps should be `int64_t` microseconds throughout — flag any `int` or `uint32_t` timestamps
- x/y should be `uint16_t`; polarity `bool`

## Output Format
Produce a numbered list of findings. If no issues found in a category, write "✓ OK". End with a summary of critical vs. minor findings.
