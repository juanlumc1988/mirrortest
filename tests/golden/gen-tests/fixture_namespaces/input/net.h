#pragma once

namespace acme {

class Socket {
public:
    void open();
};

class Packet {
public:
    explicit Packet(unsigned size);
    unsigned size() const;
};

}  // namespace acme
