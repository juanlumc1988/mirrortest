class Vec {
public:
    bool operator==(const Vec& other) const;
    bool operator!=(const Vec& other) const;
    int& operator[](int index);
    Vec& operator+=(const Vec& other);
    int operator()(int x) const;
    explicit operator bool() const;
    operator double() const;
};
