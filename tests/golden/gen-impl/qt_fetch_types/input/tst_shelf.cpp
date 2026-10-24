#include <QtTest>
#include <vector>
#include "shelf.h"

class TestShelf : public QObject {
    Q_OBJECT
private slots:
    void holdsTheBooks_data();
    void holdsTheBooks();
};

void TestShelf::holdsTheBooks_data() {
    QTest::addColumn<unsigned long long>("capacity");
    QTest::addColumn<QString>("label");
    QTest::addColumn<std::vector<int>>("ids");
    QTest::addColumn<acme::Book>("book");
}

void TestShelf::holdsTheBooks() {
    QFETCH(unsigned long long, capacity);
    QFETCH(QString, label);
    QFETCH(std::vector<int>, ids);
    QFETCH(acme::Book, book);
    acme::Shelf shelf(label);
    QVERIFY(shelf.add(book));
    QCOMPARE(shelf.capacity(), capacity);
    QCOMPARE(shelf.count(), 3u);
    QVERIFY(ids.empty());
    QCOMPARE(book.title(), "Dune");
}

QTEST_GUILESS_MAIN(TestShelf)
#include "tst_shelf.moc"
