
#ifndef UTIL_H
#define UTIL_H

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <condition_variable> // pthread_condition_t
#include <functional>
#include <iostream>
#include <mutex> // pthread_mutex_t
#include <queue>
#include <random>
#include <sstream>
#include <thread>
#include "config.h"

/* ---------------------------------- 延迟执行 ---------------------------------- */
// 实现类似 延迟执行的功能 调用持久化函数
// 确保每个DeferClass对象都拥有唯一的、与其生命周期紧密相关的操作，并且这个操作只会在该对象被销毁时被执行一次
template <class F>
class DeferClass
{
public:
    // 右值引用构造函数
    DeferClass(F &&f) : m_func(std::forward<F>(f)) {}
    DeferClass(const F &f) : m_func(f) {}
    // 直到对象离开作用域并销毁时调用析构函数
    ~DeferClass() { m_func(); }

    DeferClass(const DeferClass &e) = delete;
    DeferClass &operator=(const DeferClass &e) = delete;

private:
    F m_func;
};

#define _CONCAT(a, b) a##b
#define _MAKE_DEFER_(line) DeferClass _CONCAT(defer_placeholder, line) = [&]()

#undef DEFER
#define DEFER _MAKE_DEFER_(__LINE__)

// 示例：使用 defer 宏操作创建一个延迟执行的lambda
// DEFER({
//         std::cout << "Exiting scope..." << std::endl;
//     });

/* ----------------------------------- 常用操作函数 ----------------------------------- */
void DPrintf(const char *format, ...);

void myAssert(bool condition, std::string message = "Assertion failed!");

// 字符串格式化操作 简单的将所有参数转换为字符串并连接
template <typename... Args>
std::string format(const char *format_str, Args... args)
{
    std::stringstream ss;
    int _[] = {((ss << args), 0)...};
    (void)_;
    return ss.str();
}

// 调用 chrono 库进行时间操作和延迟
std::chrono::_V2::system_clock::time_point now();

std::chrono::milliseconds getRandomizedElectionTimeout();

void sleepNMilliseconds(int N);

/* ------------------------------- 异步写日志的日志队列 ------------------------------- */

// 两个对锁的管理用到了RAII的思想，防止中途出现问题而导致资源无法释放的问题！！！
// std::lock_guard 和 std::unique_lock 都是 C++11 中用来管理互斥锁的工具类。
// 它们都封装了 RAII（Resource Acquisition Is Initialization）技术，使得互斥锁在需要时自动加锁，在不需要时自动解锁，从而避免了很多手动加锁和解锁的繁琐操作。
// std::lock_guard 是一个模板类，它的模板参数是一个互斥量类型。当创建一个 std::lock_guard
// 对象时，它会自动地对传入的互斥量进行加锁操作，并在该对象被销毁时对互斥量进行自动解锁操作。std::lock_guard
// 不能手动释放锁，因为其所提供的锁的生命周期与其绑定对象的生命周期一致。

// std::unique_lock 也是一个模板类，同样的，其模板参数也是互斥量类型。不同的是，std::unique_lock 提供了更灵活的锁管理功能。可以通过
// lock()、unlock()、try_lock() 等方法手动控制锁的状态。当然，std::unique_lock 也支持 RAII
// 技术，即在对象被销毁时会自动解锁。另外， std::unique_lock 还支持超时等待和可中断等待的操作。

// read is blocking!!!
template <typename T>
class LockQueue
{
public:
    // 多个worker线程都会写日志queue
    void Push(const T &data)
    {
        std::lock_guard<std::mutex> lock(m_mutex); // 使用lock_gurad，即RAII的思想保证锁正确释放（自动锁定和解锁)
        m_queue.push(data);
        m_condvariable.notify_one(); // 唤醒等待线程
    }

    // 一个线程读日志queue，写日志文件
    T Pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_queue.empty())
        {
            // 日志队列为空，线程进入wait状态
            m_condvariable.wait(lock); // 这里用unique_lock是因为lock_guard不支持解锁，而unique_lock支持
        }
        T data = m_queue.front();
        m_queue.pop();
        return data;
    }
    // 移除并返回一个元素，如果在指定超时时间内队列仍为空 则返回
    bool timeOutPop(int timeout, T *ResData) // 添加一个超时时间参数，默认为 50 毫秒
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        // 获取当前时间点，并计算出超时时刻
        auto now = std::chrono::system_clock::now();
        auto timeout_time = now + std::chrono::milliseconds(timeout);

        // 在超时之前，不断检查队列是否为空
        while (m_queue.empty())
        {
            // 如果已经超时了，就返回一个空对象
            if (m_condvariable.wait_until(lock, timeout_time) == std::cv_status::timeout)
            {
                return false;
            }
            else
            {
                continue;
            }
        }

        T data = m_queue.front();
        m_queue.pop();
        *ResData = data;
        return true;
    }

private:
    std::queue<T> m_queue; // 存储日志数据的队列
    std::mutex m_mutex;
    std::condition_variable m_condvariable; // 条件变量 在队列为空时阻塞读取线程，并在有新数据时被唤醒
};

/* ---------------------------- kv 传递给 raft 的命令 --------------------------- */
class Op
{
public:
    // Field names must start with capital letters, 字段名称以大写开头
    // otherwise RPC will break.
    std::string Operation; // 操作类型 "Get" "Put" "Append"
    std::string Key;
    std::string Value;
    std::string ClientId; // 客户端号码 唯一标志符
    int RequestId;        // 客户端号码请求的Request的序列号，为了保证 线性一致性
                          
public:
    // todo
    // 为了协调raftRPC中的command只设置成了string,这个的限制就是正常字符中不能包含|
    // 当然后期可以换成更高级的序列化方法，比如protobuf

    // 序列化 将数据结构或对象转换为可以存储或传输的格式
    std::string asString() const
    {
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);  // 使用 boost 库函数

        // write class instance to archive
        oa << *this;
        // close archive
        return ss.str();
    }
    // 反序列化
    bool parseFromString(std::string str)
    {
        std::stringstream iss(str);
        boost::archive::text_iarchive ia(iss);
        // read class state from archive
        ia >> *this;
        // todo : 解析失败如何处理，要看一下boost库
        return true; 
    }

public:
    // 重载 << 运算符 自定义输出格式
    friend std::ostream &operator<<(std::ostream &os, const Op &obj)
    {
        os << "[MyClass:Operation{" + obj.Operation + "},Key{" + obj.Key + "},Value{" + obj.Value + "},ClientId{" +
                  obj.ClientId + "},RequestId{" + std::to_string(obj.RequestId) + "}"; 
        return os;
    }

private:
    // boost 库为了实现序列化所要求的成员模板函数
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive &ar, const unsigned int version)
    {
        ar & Operation;
        ar & Key;
        ar & Value;
        ar & ClientId;
        ar & RequestId;
    }
};

// kvserver reply err to clerk
const std::string OK = "OK";
const std::string ErrNoKey = "ErrNoKey";
const std::string ErrWrongLeader = "ErrWrongLeader";

//获取可用端口
bool isReleasePort(unsigned short usPort);

bool getReleasePort(short &port);


#endif //  UTIL_H