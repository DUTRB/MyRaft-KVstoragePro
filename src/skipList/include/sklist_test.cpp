/*
 * @Author: rubo
 * @Date: 2024-05-02 11:51:32
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-06 14:48:57
 * @FilePath: /myRaft/src/skipList/include/sklist_test.cpp
 * @Description: 此文件为 skipList.h 测试文件，主要测试实现跳表相关功能
 */
#include <iostream>
#include "skipList.h"
#define FILE_PATH "../config/dumpFile"

int main()
{
    SkipList<int, std::string> skip_list(6);

    // 目前只支持 int类型 key, 若为其他类型，需添加自定义比较函数
    skip_list.insert_element(1, "one");
    skip_list.insert_element(3, "three");
    skip_list.insert_element(7, "seven");
    skip_list.insert_element(8, "eight");
    skip_list.insert_element(9, "nine");
    skip_list.insert_element(19, "ten");
    skip_list.insert_element(19, "ninteen");

    std::cout << "skiplist size:" << skip_list.size() << std::endl;
    // 保存文件
    //skip_list.dumpFile();

    // skip_list.loadFile();

    // 查找节点
    skip_list.search_element(9);
    skip_list.search_element(18);
    // 打印全部节点
    skip_list.print_list();
    // 删除节点
    skip_list.delete_element(3);
    skip_list.delete_element(7);

    std::cout << "skip_list size:" << skip_list.size() << std::endl;

    skip_list.print_list();
}
