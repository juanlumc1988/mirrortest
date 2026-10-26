#pragma once
#include <QObject>

namespace acme {
namespace net {

class Socket : public QObject {
    Q_OBJECT
public:
    Socket();
    void open();
signals:
    void opened();
};

}  // namespace net
}  // namespace acme
