#ifndef PORT_H
#define PORT_H

class Port {
public:
    void open();
#ifdef USE_DMA
    void transfer(const char* data, unsigned size);
#elif defined(USE_PIO) && PIO_WIDTH > 8
    void transfer(const char* data, unsigned size, int width);
#else
    void transfer(const char* data);
#endif
#ifndef NO_STATS
#ifdef VERBOSE
    int stats() const;
#endif
#endif
};

#ifdef EXTRA
class Extra {
public:
    int value() const;
};
#endif

#endif
