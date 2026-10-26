// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Parser spike input (docs/design.md, R8). Deliberately not compilable: it
// references classes that do not exist and exercises the shapes the
// frontends must handle. Do not "fix" it.

#pragma once
#include <string>
#include <QObject>
namespace acme { namespace core {
class FOO_EXPORT Widget : public QObject, private Base
{
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
public:
    explicit Widget(QObject* parent = nullptr);
    ~Widget() override;
    Widget(const Widget&) = delete;
    int value() const noexcept;
    void setValue(int v);
    static Widget* create(const std::string& name, std::size_t n = 0u);
    virtual void apply(const std::vector<int>& xs) = 0;
    bool operator==(const Widget& o) const;
    explicit operator bool() const;
    template <typename T> T as() const;
    Q_INVOKABLE void ping();
public slots:
    void reset();
signals:
    void valueChanged(int);
protected:
    void hidden();
private:
    int value_ = 0;
    struct Item { int x; };
    Item item() const;
};
template <typename T>
class Box { public: T get() const; };
enum class Color { Red, Green };
int freeFunc(int a, double b);
} }
