// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "account.h"

Account::Account(int, const std::string&) {}
Account::Account(int) {}
long Account::balance() const { return 0; }
void Account::deposit(long) {}
