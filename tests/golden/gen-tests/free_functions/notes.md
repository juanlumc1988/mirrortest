The free functions of a header get a file of their own, named after the header (`math_functions_test.cpp`, suite `MathFunctionsTest`): one suite
for each namespace, one test for each function. A function declared twice is one; an overload is a test of its own, named by its
parameters; a default argument is left out; `noexcept` is asserted; an operator is called as a function; a function in the global
namespace is called as `::name`; the ones of `extern "C"` are read; a function template is said not to be supported. (The conditions a function is under are a unit test: libclang reads
the branch that builds and the golden is run with it too.)
