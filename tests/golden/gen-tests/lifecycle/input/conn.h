class Conn {
public:
    Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    ~Conn();
    void send();
protected:
    void retry();
private:
    void reset();
};
