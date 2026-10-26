#include <QObject>
#include <QString>

class Model : public QObject {
    Q_OBJECT
public:
    explicit Model(QObject* parent = nullptr);
    QString title() const;
public slots:
    void setTitle(const QString& title);
signals:
    void titleChanged(const QString& title);
};
