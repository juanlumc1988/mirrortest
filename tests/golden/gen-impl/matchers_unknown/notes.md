`SizeIs` and `IsEmpty` say nothing of the type of what they match, `ElementsAre(1, 2.5)` has arguments of two types and
`ElementsAre(IsOdd(), IsEven())` has matchers of the test's own, which are not read (they are not functions to generate):
the error names the matcher and, when there is one, why. The call that another matcher types (`count`) is still generated.
