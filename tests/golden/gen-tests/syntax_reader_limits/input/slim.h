#ifndef SLIM_H
#define SLIM_H

namespace acme {

class Slim {
public:
    Slim();

    unsigned /* cache */ int size() const;
    long /* wide */ long big() const;
    unsigned // up to the end of the line
        short small() const;

    auto fn() const -> int (*)(int);
    auto ptr() const -> int*;

    void on(int (*)(int));
    void with(const char* = nullptr, char* = nullptr, int times = 1);
};

int apply(int (*)(int), int value);
int total(const int* = nullptr);

}  // namespace acme

#endif
