namespace acme {
namespace net {

class Socket {
public:
    void open();
    int fd() const;
};

class Packet {
public:
    unsigned size() const;
};

}  // namespace net
}  // namespace acme
