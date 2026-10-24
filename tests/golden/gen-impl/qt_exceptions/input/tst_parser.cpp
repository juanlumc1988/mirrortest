#include <QtTest>
#include <stdexcept>
#include "parser.h"

class TestParser : public QObject {
    Q_OBJECT
private slots:
    void rejectsEmptyInput();
};

void TestParser::rejectsEmptyInput() {
    Parser parser;
    QVERIFY_EXCEPTION_THROWN(parser.parse(""), std::invalid_argument);
    QVERIFY(parser.ok());
}

QTEST_GUILESS_MAIN(TestParser)
#include "tst_parser.moc"
