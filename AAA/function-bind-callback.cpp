#include <iostream>
#include <functional>

// 方法1：用 std::function（通用，但有性能开销）
void method1(std::function<void(int)> callback) {
    std::cout << "方法1: std::function" << std::endl;
    callback(100);
}

// 方法2：用模板（性能最好，推荐！）
template<typename Func>
void method2(Func callback) {
    std::cout << "方法2: 模板" << std::endl;
    callback(200);
}

// 方法3：用函数指针（只能接受函数指针，不能接受 lambda 捕获）
void method3(void (*callback)(int)) {
    std::cout << "方法3: 函数指针" << std::endl;
    callback(300);
}

// 普通函数
void normalFunc(int x) {
    std::cout << "  普通函数收到: " << x << std::endl;
}

// STL 的仿函数（functor）：重载了 operator() 的类
class MyFunctor {
public:
    void operator()(int x) const {
        std::cout << "  仿函数收到: " << x << std::endl;
    }
};

int main() {
    std::cout << "=== std::function 是什么？===\n" << std::endl;
    std::cout << "std::function<void(int)> 的意思：" << std::endl;
    std::cout << "  - 可以存储任何「可调用对象」" << std::endl;
    std::cout << "  - void(int) 表示：返回值void，参数int" << std::endl;
    std::cout << "  - 来自 <functional> 头文件\n" << std::endl;
    
    std::cout << "=== 三种写法对比 ===\n" << std::endl;
    
    // std::function 可以接受所有类型
    method1(normalFunc);                          // 普通函数 ✓
    method1([](int x) { 
        std::cout << "  lambda收到: " << x << std::endl; 
    });                                           // lambda ✓
    method1(MyFunctor());                         // 仿函数 ✓
    
    std::cout << std::endl;
    
    // 模板也可以接受所有类型（性能更好）
    method2(normalFunc);
    method2([](int x) { 
        std::cout << "  lambda收到: " << x << std::endl; 
    });
    method2(MyFunctor());
    
    std::cout << std::endl;
    
    // 函数指针只能接受普通函数和无捕获的 lambda
    method3(normalFunc);
    method3([](int x) {                           // 无捕获的 lambda 可以
        std::cout << "  lambda收到: " << x << std::endl; 
    });
    // method3(MyFunctor());  // ✗ 编译错误！
    
    std::cout << "\n=== 仿函数(functor) vs std::function ===\n" << std::endl;
    std::cout << "仿函数(functor)：" << std::endl;
    std::cout << "  - 重载了 operator() 的类" << std::endl;
    std::cout << "  - 可以像函数一样调用" << std::endl;
    std::cout << "  - STL 算法里很多（如 std::less, std::greater）\n" << std::endl;
    
    MyFunctor func;
    func(999);  // 像函数一样调用
    
    std::cout << "\nstd::function：" << std::endl;
    std::cout << "  - 是一个「容器」，可以装各种可调用对象" << std::endl;
    std::cout << "  - 可以装：普通函数、lambda、仿函数、bind结果\n" << std::endl;
    
    std::function<void(int)> f;
    f = normalFunc;      // 装普通函数
    f(1);
    f = MyFunctor();     // 装仿函数
    f(2);
    f = [](int x) { std::cout << "  装了lambda: " << x << std::endl; };
    f(3);
    
    std::cout << "\n=== 总结 ===\n" << std::endl;
    std::cout << "1. std::function<返回值(参数)> - 万能容器，装各种可调用对象" << std::endl;
    std::cout << "2. 模板更高效，推荐用：template<typename F> void foo(F callback)" << std::endl;
    std::cout << "3. 仿函数 = 行为像函数的对象（重载了operator()）" << std::endl;
    std::cout << "4. lambda 本质上就是编译器生成的匿名仿函数" << std::endl;
    
    return 0;
}