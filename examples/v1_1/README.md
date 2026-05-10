# v1.1 Mach-O examples

v1.1 starts the Mach-O loader implementation, but deterministic Mach-O fixture generation and automated tests are intentionally still pending.

The supported runtime profile is narrow:

```text
64-bit little-endian arm64 Mach-O
MH_EXECUTE
LC_SEGMENT_64 mapped into the existing 1 MiB flat memory
LC_MAIN resolved through mapped file ranges
toy SVC #0 syscalls only
no dyld, shared libraries, rebasing, binding, code signing, or Darwin process setup
```

Future v1.1 example fixtures should be generated deterministically rather than requiring Xcode or Apple platform tools.
