#include "label.h"

void check() {
    Label label;
    QCOMPARE(label.text(), "hello");
    QCOMPARE(label.length(), 5u);
}
