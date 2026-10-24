#include <string>

class Account {
public:
    Account(int id, const std::string& owner);
    explicit Account(int id);
    long balance() const;
    void deposit(long amount);
};
