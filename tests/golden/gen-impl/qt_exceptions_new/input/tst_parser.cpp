#include <QtTest>
#include <stdexcept>
#include "parser.h"

class TestParser : public QObject {
    Q_OBJECT
private slots:
    void rejectsEmptyInput();
    void acceptsWords();
};

void TestParser::rejectsEmptyInput() {
    Parser parser;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parser.parse(""));
}

void TestParser::acceptsWords() {
    Parser parser;
    QVERIFY_THROWS_NO_EXCEPTION(parser.parse("word"));
}

QTEST_GUILESS_MAIN(TestParser)
#include "tst_parser.moc"
