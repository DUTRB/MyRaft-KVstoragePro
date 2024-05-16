/*
 * @Author: rubo
 * @Date: 2024-05-03 10:58:16
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-09 15:10:19
 * @FilePath: /myRaft/example/raftCoreExample/caller.cpp
 * @Description:
 */

// clerk（外部客户端）调用代码
#include <iostream>
#include "clerk.h"
#include "util.h"

int main()
{
    Clerk client;
    client.Init("test.conf");
    auto start = now();
    int count = 500;
    int tmp = count;
    while (tmp--)
    {
        client.Put("x", std::to_string(tmp));

        std::string get1 = client.Get("x");
        std::printf("get return :{%s}\r\n", get1.c_str());
    }
    return 0;
}