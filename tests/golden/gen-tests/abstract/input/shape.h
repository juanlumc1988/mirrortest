class Shape {
public:
    virtual ~Shape();
    virtual double area() const = 0;
    void describe();
};
