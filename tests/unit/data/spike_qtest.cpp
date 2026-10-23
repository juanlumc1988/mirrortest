// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Parser spike input (docs/design.md, R8). Deliberately not compilable: it
// references classes that do not exist and exercises the shapes the
// frontends must handle. Do not "fix" it.

#include <QtTest>
#include "foo.h"

class TestFoo : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void size_data();
    void size();
    void inlineCase() { QVERIFY(foo.empty()); }
private:
    Foo foo;
};

void TestFoo::size_data()
{
    QTest::addColumn<int>("value");
    QTest::addColumn<QString>("name");
    QTest::newRow("zero") << 0 << QString("a");
}

void TestFoo::size()
{
    QFETCH(int, value);
    QFETCH(QString, name);
    Foo f(value);
    QCOMPARE(f.size(), value);
    QCOMPARE(f.name(), QStringLiteral("a"));
    QVERIFY(f.size() > 0);
    QVERIFY_THROWS_EXCEPTION(std::logic_error, f.parse(name));
    QVERIFY_EXCEPTION_THROWN(f.parse(name), std::logic_error);
}

QTEST_GUILESS_MAIN(TestFoo)
#include "tst_foo.moc"
