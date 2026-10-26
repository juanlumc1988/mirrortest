// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#ifndef MT_ROUNDTRIP_MYSTERY_H
#define MT_ROUNDTRIP_MYSTERY_H

struct Mystery {
    explicit Mystery(int);   // no default constructor: nobody could build one blindly
};

#endif
