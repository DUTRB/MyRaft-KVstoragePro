/*
 * @Author: rubo
 * @Date: 2024-05-12 10:28:33
 * @LastEditors: HUAWEI-Ubuntu ruluy0205@163.com
 * @LastEditTime: 2024-05-15 22:11:33
 * @FilePath: /myRaft/src/fiber/utils.cpp
 * @Description: 
 */
#include "./include/utils.hpp"

namespace monsoon {
pid_t GetThreadId() { return syscall(SYS_gettid); }

u_int32_t GetFiberId() {
  // TODO
  return 0;
}
}  // namespace monsoon