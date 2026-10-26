#pragma once
#include <QObject>
#include <QString>

class Slider : public QObject {
    Q_OBJECT
public:
    Slider();
signals:
    void changed(int value);
    void changed(const QString& text);
    void released();
};
