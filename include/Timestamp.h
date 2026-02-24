#pragma once

#include <string>

class Timestamp
{
public:
    Timestamp() = default;
    explicit Timestamp(int64_t microSecondsSinceEpoch) noexcept
        : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}
    static Timestamp now(); // 静态工厂方法, now() 的职责是创造一个新的 Timestamp 对象，它在调用时根本还没有实例存在
    std::string toString() const;

private:
    int64_t microSecondsSinceEpoch_ = 0;
};
