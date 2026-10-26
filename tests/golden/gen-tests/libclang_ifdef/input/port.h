class Port {
public:
    void open();
#ifdef USE_DMA
    void transfer(const char* data, unsigned size);
#else
    void transfer(const char* data);
#endif
};
