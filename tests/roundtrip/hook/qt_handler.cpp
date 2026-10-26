// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What a project using stub_policy "hook" with Qt Test writes once: a handler for
// mt_not_implemented() that makes the running slot fail and returns, installed
// before main. Linked into the program of a round trip (-DMT_HANDLER=...).

#include <QtTest>

#include "mt_not_implemented.h"

namespace {

void fail_the_slot(const char* signature) {
    QTest::qFail(signature, __FILE__, __LINE__);
}

struct Install {
    Install() { mt_set_not_implemented_handler(&fail_the_slot); }
} install;

}  // namespace
