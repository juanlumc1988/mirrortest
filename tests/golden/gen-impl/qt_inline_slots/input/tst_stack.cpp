#include <QtTest>
#include "stack.h"

class TestStack : public QObject {
    Q_OBJECT
    Stack stack_;
private slots:
    void startsEmpty() {
        QVERIFY(stack_.empty());
        QCOMPARE(stack_.size(), 0u);
    }
    void pushGrowsIt() {
        stack_.push(7);
        QCOMPARE(stack_.size(), 1u);
        QCOMPARE(stack_.top(), 7);
    }
};

QTEST_APPLESS_MAIN(TestStack)
#include "tst_stack.moc"
