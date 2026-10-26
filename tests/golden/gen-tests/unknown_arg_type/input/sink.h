class Sink {
public:
    int write(int fd, Mystery data, unsigned flags);
    void flush();
};
