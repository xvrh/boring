# boring

Raw `ffigen` bindings to **BoringSSL** powered by **Dart Native Assets**.

[![pub package](https://img.shields.io/pub/v/boring.svg)](https://pub.dev/packages/boring)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

`package:boring` bundles Google's [BoringSSL](https://boringssl.googlesource.com/boringssl/) (`bssl_dart`) via Dart Native Assets and exposes its raw `@Native` C bindings along with an `OPENSSL_malloc` / `OPENSSL_free`-backed `ffi.Allocator` across Linux, macOS, Windows, Android, and iOS.

---

## Key Highlights

- **100% Symbol Isolation (`bssl_dart`)**: Compiled with `-DBORINGSSL_PREFIX=bssl_dart`, eliminating dynamic linker collisions with Flutter, the Dart VM, or system OpenSSL libraries.
- **Dart Native Assets**: Bundles and dynamically loads native code automatically via `package:code_assets` and `package:hooks`.
- **Automatic Memory Scrubbing (`opensslAllocator`)**: Exports `opensslAllocator` (`ffi.Allocator` backed by `OPENSSL_malloc` / `OPENSSL_free`), which stores allocation sizes and unconditionally runs `OPENSSL_cleanse` before freeing native memory.
- **Concrete `CBS` & `CBB` Structs**: Both `CBS` (CRYPTO ByteString) and `CBB` (CRYPTO ByteBuilder) are generated as concrete `ffi.Struct` types, allowing direct stack/arena allocation (`arena<CBS>()`, `arena<CBB>()`) without C wrapper shims.
- **Scoped Native Memory (`BoringArena`)**: An `ffi.Allocator` and resource tracker backed by `opensslAllocator`. `BoringArena.run` and `BoringArena.stream` release allocations and `X_new` / `X_free` resources (`arena.using(EC_KEY_new(), EC_KEY_free)`) in reverse order once the computation is done, including `async` ones. `move()` supports BoringSSL's `set0` ownership transfer, and `copyBytes`, `cbs()`, `cbb()`, and `CBB.toBytes()` cover byte-string plumbing.
- **Finalizable Handles (`NativeHandle`)**: A GC-managed wrapper for long-lived BoringSSL objects (`NativeHandle(EVP_PKEY_new(), addresses.EVP_PKEY_free)`) with deterministic `dispose()`, using the `*_free` symbol addresses exposed as `addresses.*`.
- **Tree-Shaking**: Release builds only bundle the BoringSSL functions the application uses, see [Tree-Shaking](#tree-shaking).

---

## Getting Started

Add `boring` to your `pubspec.yaml`:

```yaml
dependencies:
  boring: ^0.3.0
```

---

## Usage

Import `package:boring/bindings.dart` (or `package:boring/boring.dart`) and scope native allocations and resources with `BoringArena`:

```dart
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:boring/bindings.dart' as ssl;

void main() {
  final digest = ssl.BoringArena.run((arena) {
    final input = utf8.encode('hello world');
    final md = ssl.EVP_sha256();
    final out = arena<ffi.Uint8>(ssl.EVP_MD_size(md));
    final outLen = arena<ffi.UnsignedInt>();
    final ctx = arena.using(ssl.EVP_MD_CTX_new(), ssl.EVP_MD_CTX_free);

    if (ssl.EVP_DigestInit(ctx, md) != 1 ||
        ssl.EVP_DigestUpdate(ctx, arena.copyBytes(input), input.length) != 1 ||
        ssl.EVP_DigestFinal(ctx, out, outLen) != 1) {
      throw StateError(ssl.extractBoringSslError() ?? 'SHA-256 failed');
    }
    return Uint8List.fromList(out.asTypedList(outLen.value));
  });

  final hex = digest.map((b) => b.toRadixString(16).padLeft(2, '0')).join();
  print('SHA-256("hello world"): $hex');
}
```

### Error Handling

BoringSSL signals failure through return values and pushes details onto a per-thread error queue. A Dart isolate may resume on a different OS thread after an `await`, and every package using `package:boring` on a thread shares its queue, so:

- Read errors with `extractBoringSslError()`, which also clears the queue, right after the failing call. Don't leave an `await` in between, and don't defer it to a `finally` that may run after one, such as the release of an `async` `BoringArena.run`.
- Discard errors you ignore with `ERR_clear_error()`, for example when a failed signature verification just means `false`. Otherwise they are reported for the next, unrelated failure.
- Call `ERR_clear_error()` before a call whose errors you report, so errors left behind by other code aren't attributed to it.

---

## Native Asset Build Modes

Configured in `pubspec.yaml` under `hooks.user_defines.boring`:

```yaml
hooks:
  user_defines:
    boring:
      buildMode: fetch # 'fetch', 'checkout', or 'local'
```

- **`fetch`** *(default)*: Downloads prebuilt binaries from GitHub Releases verified against pinned SHA-256 checksums, falling back to local compilation if unavailable.
- **`checkout`**: Always compiles BoringSSL locally from bundled sources via CMake and Ninja.
- **`local`**: Uses a custom prebuilt dynamic library at `localPath`, which is bundled as is, without [tree-shaking](#tree-shaking).

`fetch` and `checkout` provide a dynamic library with all of BoringSSL when linking is disabled (`dart run`, `dart test`, and Flutter debug builds), and a static library for [tree-shaking](#tree-shaking) when it is enabled. Every GitHub Release has both for each prebuilt target.

---

## Tree-Shaking

When linking is enabled (`dart build`, and Flutter profile and release builds), `hook/link.dart` links a dynamic library with only the functions the application uses from the static library. The bindings are annotated with `@RecordUse()`, so the Dart compiler records which of them the application calls, tears off, or takes the address of with `addresses.*`. For the [example](example/boring_example.dart), the bundled library shrinks from 2.9 MB to 240 KB on Linux x64.

- Use `addresses.X` rather than `Native.addressOf(X)` for the address of a function, for example for a `NativeFinalizer`. `Native.addressOf` isn't recorded, so the function would be missing from the library.
- Without recorded uses, for example with `flutter config --no-enable-record-use`, all functions are kept.
- Linking requires a C toolchain for the target (Clang or GCC, Xcode, MSVC, or the Android NDK), with the static C++ standard library on Linux (`libstdc++.a`, which comes with g++). Without one, for example when cross-compiling, the `fetch` build mode bundles the prebuilt dynamic library instead, which is not tree-shaken, and prints a warning.

---

## Conformance Testing

CI checks the bundled BoringSSL and the generated bindings against two external suites, calling BoringSSL directly through the bindings (see [`test/conformance/`](test/conformance/)):

- [**Project Wycheproof**](https://github.com/C2SP/wycheproof) (`./tool/run_conformance_tests.sh`): AES-GCM, ChaCha20-Poly1305, XChaCha20-Poly1305, AES-CBC, AES Key Wrap, Ed25519, ECDSA (P-256, P-384, P-521), RSA PKCS#1 v1.5 and RSA-PSS signatures, RSA-OAEP, ECDH, HKDF, HMAC, and PBKDF2.
- [**x509-limbo**](https://x509-limbo.com) (`./tool/run_x509_limbo_tests.sh`): 9,770 of the 9,793 path validation testcases run against `X509_verify_cert`, and 9,237 (94.5%) agree. The divergences, mostly name constraint types BoringSSL does not support and CA/Browser Forum profile checks it leaves to the caller, are listed and explained in [`test/conformance/x509_limbo_expected_failures.txt`](test/conformance/x509_limbo_expected_failures.txt).

---

## License

Apache License, Version 2.0. See [LICENSE](LICENSE) for details. BoringSSL is licensed under Apache 2.0 and BSD-style licenses.
