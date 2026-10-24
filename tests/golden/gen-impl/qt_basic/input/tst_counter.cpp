#include <QtTest>
#include "acme/counter.h"

class TestCounter : public QObject {
    Q_OBJECT
private slots:
    void startsAtTheInitialValue();
    void incrementsByOne_data();
    void incrementsByOne();
};

void TestCounter::startsAtTheInitialValue() {
    acme::Counter counter(5);
    QCOMPARE(counter.value(), 5);
    QVERIFY(counter.enabled());
    QCOMPARE(counter.name(), "counter");
}

void TestCounter::incrementsByOne_data() {
    QTest::addColumn<int>("start");
    QTest::addColumn<int>("expected");
    QTest::newRow("zero") << 0 << 1;
    QTest::newRow("ten") << 10 << 11;
}

void TestCounter::incrementsByOne() {
    QFETCH(int, start);
    QFETCH(int, expected);
    acme::Counter counter(start);
    counter.increment();
    QCOMPARE(counter.value(), expected);
    QBENCHMARK { counter.reset(); }
}

QTEST_GUILESS_MAIN(TestCounter)
#include "tst_counter.moc"
