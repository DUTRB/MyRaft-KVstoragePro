#include "./include/mprpcchannel.h"
#include "./include/mprpccontroller.h"
#include "./include/rpcheader.pb.h"
#include "util.h"
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

/*
header_size + service_name method_name args_size + args
*/
// 所有通过 stub 代理对象调用的 rpc方法，都会走到这里
// 统一通过 rpcChannel 来调用方法，统一做 rpc方法调用的数据数据序列化和网络发送

/**
 * 调用远程方法 重写基类函数
 * @param method 方法描述符
 * @param controller RPC控制器
 * @param request 请求消息
 * @param response 响应消息
 * @param done 回调函数，当RPC调用完成后会被调用
 */
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                              google::protobuf::RpcController *controller,
                              const google::protobuf::Message *request,
                              google::protobuf::Message *response,
                              google::protobuf::Closure *done) {
  // 根据文件描述符结果，尝试重新连接服务器
  if (m_clientFd == -1) {
    std::string errMsg;
    bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);
    if (!rt) {
      DPrintf("[func-MprpcChannel::CallMethod]重连接ip：{%s} port{%d}失败！",
              m_ip.c_str(), m_port);
      controller->SetFailed(errMsg);
      return;
    } else {
      DPrintf("[func-MprpcChannel::CallMethod]连接ip：{%s} port{%d}成功！",
              m_ip.c_str(), m_port);
    }
  }
  // 获取服务和方法名称
  const google::protobuf::ServiceDescriptor *sd = method->service();
  std::string service_name = sd->name();    // service_name
  std::string method_name = method->name(); // method_name

  // 将请求消息 序列化为字符串 args_str 并获取其大小 args_size
  uint32_t args_size{};
  std::string args_str;
  if (request->SerializeToString(&args_str)) {
    args_size = args_str.size();
  } else {
    controller->SetFailed("serialize request error!");
    return;
  }
  // 构建 RPC 头部 设置服务名、方法名、参数大小
  RPC::RpcHeader rpcHeader;
  rpcHeader.set_service_name(service_name);
  rpcHeader.set_method_name(method_name);
  rpcHeader.set_args_size(args_size);
  // 序列化 RPC 头部
  std::string rpc_header_str;
  if (!rpcHeader.SerializeToString(&rpc_header_str)) {
    controller->SetFailed("serialize rpc header error!");
    return;
  }

  // 使用 protobuf 的 CodedOutputStream 来构建发送的数据流
  std::string send_rpc_str; // 用来存储最终发送的数据
  {
    // 创建一个StringOutputStream用于写入send_rpc_str
    google::protobuf::io::StringOutputStream string_output(&send_rpc_str);
    google::protobuf::io::CodedOutputStream coded_output(&string_output);

    // 首先写入header的长度（变长编码）
    coded_output.WriteVarint32(static_cast<uint32_t>(rpc_header_str.size()));

    // 不需要手动写入header_size，因为上面的WriteVarint32已经包含了header的长度信息
    // 然后写入rpc_header本身
    coded_output.WriteString(rpc_header_str);
  }

  // 最后，将请求参数附加到send_rpc_str后面
  send_rpc_str += args_str;

  // 打印调试信息
  //    std::cout << "============================================" <<
  //    std::endl; std::cout << "header_size: " << header_size << std::endl;
  //    std::cout << "rpc_header_str: " << rpc_header_str << std::endl;
  //    std::cout << "service_name: " << service_name << std::endl;
  //    std::cout << "method_name: " << method_name << std::endl;
  //    std::cout << "args_str: " << args_str << std::endl;
  //    std::cout << "============================================" <<
  //    std::endl;

  // 发送rpc请求 通过send系统调用发送
  // 失败会重试连接再发送，重试连接失败会直接return
  while (-1 == send(m_clientFd, send_rpc_str.c_str(), send_rpc_str.size(), 0)) {
    char errtxt[512] = {0};
    sprintf(errtxt, "send error! errno:%d", errno);
    std::cout << "尝试重新连接，对方ip：" << m_ip << " 对方端口：" << m_port
              << std::endl;
    close(m_clientFd);
    m_clientFd = -1;
    std::string errMsg;
    bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);
    if (!rt) {
      controller->SetFailed(errMsg);
      return;
    }
  }

  /*
  从时间节点来说，这里将请求发送过去之后 rpc 服务的提供者就会开始处理，返回的时候就代表着已经返回响应了
  */

  // 接收 rpc 请求的响应值
  char recv_buf[1024] = {0};
  int recv_size = 0;
  if (-1 == (recv_size = recv(m_clientFd, recv_buf, 1024, 0))) {
    close(m_clientFd);
    m_clientFd = -1;
    char errtxt[512] = {0};
    sprintf(errtxt, "recv error! errno:%d", errno);
    controller->SetFailed(errtxt);
    return;
  }

  // 反序列化 rpc 调用的响应数据
  // 尝试将接收到的数据反序列化到响应消息 response
  if (!response->ParseFromArray(recv_buf, recv_size)) {
    char errtxt[1050] = {0};
    sprintf(errtxt, "parse error! response_str:%s", recv_buf);
    controller->SetFailed(errtxt);
    return;
  }
}
/**
   * 连接ip和端口,并设置 m_clientFd
   * @param ip ip地址，本机字节序
   * @param port 端口，本机字节序
   * @param errMsg 返回错误信息
   * @return 成功返回true，否则返回失败信息
 */
bool MprpcChannel::newConnect(const char *ip, uint16_t port, string *errMsg) {
  // 使用 socket 连接
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == clientfd) {
    char errtxt[512] = {0};
    sprintf(errtxt, "create socket error! errno:%d", errno);
    m_clientFd = -1;
    *errMsg = errtxt;
    return false;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(ip);
  // 连接 rpc 服务节点
  if (-1 == connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
    close(clientfd);
    char errtxt[512] = {0};
    sprintf(errtxt, "connect fail! errno:%d", errno);
    m_clientFd = -1;
    *errMsg = errtxt;
    return false;
  }
  m_clientFd = clientfd;
  return true;
}
/**
   * 构造函数 建立到 RPC服务器的连接
   * @param ip 接收IP地址
   * @param port 接收端口
   * @param connectNow 是否立即建立连接
 */
MprpcChannel::MprpcChannel(string ip, short port, bool connectNow)
    : m_ip(ip), m_port(port), m_clientFd(-1) {
  // 使用tcp编程，完成rpc方法的远程调用，使用的是短连接，因此每次都要重新连接上去，待改成长连接。
  // 没有连接或者连接已经断开，那么就要重新连接呢,会一直不断地重试
  // 读取配置文件rpcserver的信息
  // std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip"); uint16_t
  // port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
  // rpc调用方想调用service_name的method_name服务，需要查询zk上该服务所在的host信息
  //  /UserServiceRpc/Login
  if (!connectNow) {
    return;
  }
  // 可以允许延迟连接
  std::string errMsg;
  auto rt = newConnect(ip.c_str(), port, &errMsg);
  int tryCount = 3;
  while (!rt && tryCount--) {
    std::cout << errMsg << std::endl;
    rt = newConnect(ip.c_str(), port, &errMsg);
  }
}