#pragma once

namespace acme {
namespace hw {

class SystemClock {
public:
    virtual ~SystemClock() = default;
    virtual long now() const = 0;
};

}  // namespace hw
}  // namespace acme
