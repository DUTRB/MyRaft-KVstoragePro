/*
 * @Author: rubo
 * @Date: 2024-05-09 15:09:37
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-12 09:59:07
 * @FilePath: /myRaft/example/rpcExample/caller/callFriendService.cpp
 * @Description:  使用自定义RPC框架发起远程过程调用，并处理调用结果。
 *                展示了RPC调用的基本流程，包括设置请求参数、发起调用、检查调用状态和处理响应数据。
 */
#include <iostream>
// #include "mprpcapplication.h"
#include "rpcExample/friend.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcprovider.h"

int main(int argc, char **argv)
{
    std::string ip = "127.0.1.1";
    short port = 7788;

    // 演示调用远程发布的rpc方法
    // 注册进自己写的channel类，channel类用于自定义发送格式和负责序列化等操作
    fixbug::FiendServiceRpc_Stub stub(new MprpcChannel(ip, port, true)); 
    // rpc方法的请求
    fixbug::GetFriendsListRequest request;
    request.set_userid(1000);
    // rpc方法的响应
    fixbug::GetFriendsListResponse response;
    // 控制器对象，用于管理RPC调用的状态和错误信息
    MprpcController controller;
    // 长连接测试 每次请求间隔 5s
    int count = 10;
    while (count--)
    {
        std::cout << " 倒数第 " << count << " 次发起 RPC 请求：" << std::endl;
        // stub.GetFriendsList调用是同步的，客户端在发送请求后会等待服务端的响应。
        stub.GetFriendsList(&controller, &request, &response, nullptr);
        // RpcChannel->RpcChannel::callMethod 集中来做所有rpc方法调用的参数序列化和网络发送

        // 一次rpc调用完成，读调用的结果
        // rpc调用是否失败由框架来决定（rpc调用失败 ！= 业务逻辑返回false）
        // rpc和业务本质上是隔离的
        if (controller.Failed())
        {
            std::cout << controller.ErrorText() << std::endl;
        }
        else
        {
            // 响应正常
            if (0 == response.result().errcode())
            {
                // 打印测试输出
                std::cout << "RPC GetFriendsList response success!" << std::endl;
                int size = response.friends_size();
                for (int i = 0; i < size; i++)
                {
                    std::cout << "index:" << (i + 1) << " name:" << response.friends(i) << std::endl;
                }
            }
            else
            {
                // 这里不是rpc失败，
                // 而是业务逻辑的返回值是失败
                std::cout << "rpc GetFriendsList response error : " << response.result().errmsg() << std::endl;
            }
        }
        sleep(5);
    }
    return 0;
}
