#pragma once
#include <QObject>

class Gate : public QObject {
    Q_OBJECT
public:
    explicit Gate(Mystery m);
    void close();
signals:
    void closed();
};
