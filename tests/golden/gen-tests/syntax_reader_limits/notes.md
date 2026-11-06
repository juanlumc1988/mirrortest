What the syntax-tree reader used to get wrong and now reads as libclang does: a comment inside a type
(`unsigned /* cache */ int`, and a `//` comment between the words of `unsigned short`), a trailing return type
that is a function pointer, and an unnamed parameter that is a function pointer or a pointer with a default value
(`const char* = nullptr`). The same declarations are in the parity snippet of `test_clang_frontend.cpp`.
