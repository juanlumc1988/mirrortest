`EXPECT_THAT` with the matchers that say what the value is (design 7.6): `Eq` and the other five comparisons,
`StrEq`, `HasSubstr`, `StartsWith`, `EndsWith`, `IsTrue`, `NotNull`, `DoubleNear`, `FloatEq`, and `AllOf` and `Not`
over them. The matchers over containers have their own case, `matchers_containers`.
