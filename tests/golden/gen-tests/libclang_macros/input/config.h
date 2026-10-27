#define GETTER(type, name) type name() const;
#define SETTER(type, name) void set_##name(type value);
#define CONFIG_API __attribute__((visibility("default")))

class CONFIG_API Config {
public:
    GETTER(int, size)
    GETTER(const char*, label)
    SETTER(int, size)
};
