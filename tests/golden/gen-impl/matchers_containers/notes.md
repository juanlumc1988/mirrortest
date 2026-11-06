`ElementsAre`, `UnorderedElementsAre`, `Contains` and `Each` say what the elements are, and the result is a
`std::vector` of them (design 7.6): of the type of the arguments, of the type a matcher compares with (`Gt(0.5)`,
`StartsWith("m")`), a `std::pair` for `Pair`, a vector of vectors for a nested `ElementsAre`. `SizeIs` beside one of them
does not get in the way, `WhenSorted` and `.Times(n)` pass the container through. The note says that the vector is a
guess: a `std::set` or a `std::list` would match as well.
