// Copyright 2026 Moritz Sümmermann. Licensed under the Apache License,
// Version 2.0. See the LICENSE file for details.

#ifndef BSSL_DART_LIBCXX_VERBOSE_ABORT_H_
#define BSSL_DART_LIBCXX_VERBOSE_ABORT_H_

// Included before every C++ source, see CMakeLists.txt.
//
// With -fno-exceptions, libc++ replaces a throw with a call to
// std::__libcpp_verbose_abort, which lives in the C++ runtime. The libraries
// are linked without that runtime, so abort directly instead. BoringSSL
// reaches such calls through std::string_view::substr, for example in
// crypto/x509/by_dir.cc.
#define _LIBCPP_VERBOSE_ABORT(...) __builtin_abort()

#endif  // BSSL_DART_LIBCXX_VERBOSE_ABORT_H_
