/*
 * @Author: rubo
 * @Date: 2024-05-09 15:09:37
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-12 09:48:35
 * @FilePath: /myRaft/example/rpcExample/callee/friendService.cpp
 * @Description: 提供RPC服务
 */

#include <mprpcchannel.h>
#include <iostream>
#include <string>
#include "rpcExample/friend.pb.h"
#include <vector>
#include "rpcprovider.h"

class FriendService : public fixbug::FiendServiceRpc
{
public:
    // 非 rpc 成员函数，获取用户朋友列表
    std::vector<std::string> GetFriendsList(uint32_t userid)
    {
        std::cout << "Local do GetFriendsList service! userid:" << userid << std::endl;
        std::vector<std::string> vec;
        vec.push_back("TestNum01");
        vec.push_back("TestNum02");
        vec.push_back("TestNum03");
        return vec;
    }

    // 重写基类方法，以适应 rpc 调用
    void GetFriendsList(::google::protobuf::RpcController *controller, 
                        const ::fixbug::GetFriendsListRequest *request,
                        ::fixbug::GetFriendsListResponse *response, 
                        ::google::protobuf::Closure *done)
    {
        uint32_t userid = request->userid();
        // 调用上面定义的 GetFriendsList 方法获取朋友列表
        std::vector<std::string> friendsList = GetFriendsList(userid);
        // 设置响应对象中的 result 字段
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        // 将信息填充到响应对象中
        for (std::string &name : friendsList)
        {
            // 添加
            std::string *p = response->add_friends();
            *p = name;
        }
        // 使用了Closure对象（done参数）作为回调。当方法执行完成后，
        // 调用done->Run()来通知RPC框架调用已经完成，这允许RPC框架进行清理工作并释放资源。
        done->Run();
    }
};

int main(int argc, char **argv)
{
    std::string ip = "127.0.0.1";
    short port = 7788;
    // 接收远程调用
    //auto stub = new fixbug::FiendServiceRpc_Stub(new MprpcChannel(ip, port, false));
    // provider是一个rpc网络服务对象。把UserService对象发布到rpc节点上
    RpcProvider provider;
    // 注册 FriendService() 服务
    provider.NotifyService(new FriendService());

    // 启动一个rpc服务发布节点   Run以后，进程进入阻塞状态，等待远程的rpc调用请求
    provider.Run(1, 7788);
    return 0;
}
