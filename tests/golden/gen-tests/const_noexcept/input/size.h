class Buffer {
public:
    unsigned size() const noexcept;
    bool full() const;
    void clear() noexcept;
    int& at(unsigned index);
    const int& at(unsigned index) const;
};
