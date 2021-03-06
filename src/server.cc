// Copyright (C) 2014 sails Authors.
// All rights reserved.
//
// Filename: server.cc
//
// Author: sailsxu <sailsxu@gmail.com>
// Created: 2014-10-13 17:10:20



#include "src/server.h"
#include "src/handle_rpc.h"
#include "src/saf_const.h"
#include "src/service_register.h"
#include "sails/base/time_t.h"

namespace sails {

Server::Server():sails::net::EpollServer<sails::ReqMessage>() {
  // 得到配置的模块
  config.get_modules(&modules_name);
  // 注册模块
  std::map<std::string, std::string>::iterator iter;
  for (iter = modules_name.begin(); iter != modules_name.end()
              ; ++iter) {
    if (!iter->second.empty()) {
      moduleLoad.load(iter->second);
    }
  }
  // 设置ip限制
  deny_list = config.DenyIPList();
  allow_list = config.AllowIPList();
}


bool Server::isIpAllow(const std::string& ip) {
  for (const std::string& pat : allow_list) {
    if (pat == "all" || ipMatch(ip, pat)) {
      return true;
    }
  }

  for (std::string& pat : deny_list) {
    if (pat == "all" || ipMatch(ip, pat)) {
      return false;
    }
  }
  return true;
}

bool Server::ipMatch(const std::string& ip, const std::string& pat) {
  if (ip.empty() || pat.empty()) {
    return false;
  }
  if (pat.find('*') == std::string::npos) {
    return ip == pat;
  }

  std::string::size_type ipIndex = 0;
  std::string::size_type patIndex = 0;
  do {
    if (pat[patIndex] == '*') {
      if (ip[ipIndex] == '.') {
        return false;
      }
      while (ipIndex < ip.size() && ip[ipIndex] != '.') {
        ++ipIndex;
      }
      patIndex++;
    } else {
      if (pat[patIndex] != ip[ipIndex]) {
        return false;
      }
      ++ipIndex;
      ++patIndex;
    }
  } while (ipIndex < ip.size() && patIndex < pat.size());

  return ipIndex == ip.size() && patIndex == pat.size();
}

sails::ReqMessage* Server::Parse(
    std::shared_ptr<sails::net::Connector> connector) {

  if (connector->readable() < 4) {
    return NULL;
  }
  const char* buffer = connector->peek();
  int packetLen = *(reinterpret_cast<const int*>(buffer));
  if (connector->readable() < packetLen + 4) {
    return NULL;
  }
  if (packetLen > MAX_PACKET_LEN) {  // 最大10k
    connector->retrieve(connector->readable());
    return NULL;
  }
  // printf("parse packet len:%d, readable:%d\n",
  // packetLen, connector->readable());

  ReqMessage *data =  reinterpret_cast<ReqMessage*>(malloc(sizeof(ReqMessage)));
  new(data) ReqMessage();
  data->recvTime = sails::base::TimeT::getNowMs();
  data->reqData = std::string(buffer+4, packetLen);
  connector->retrieve(packetLen + 4);
  return data;
}

Server::~Server() {
  modules_name.clear();
  moduleLoad.unload();
}


void Server::handle(
    const sails::net::TagRecvData<sails::ReqMessage> &recvData) {

  RequestPacket request;
  sails::ResponsePacket response;
  if (request.ParseFromString(recvData.data->reqData)) {
    // 检测是否已经超时
    int spendtime = (sails::base::TimeT::getNowMs()
                     - recvData.data->recvTime) / 1000;
    if (request.timeout() > 0 && spendtime > request.timeout()) {
      // 说明在等待队列中阻塞太久了，直接返回
      response.set_ret(ErrorCode::ERR_TIMEOUT);
    } else {
      base::HandleChain<sails::RequestPacket*,
                      sails::ResponsePacket*> handle_chain;
      HandleRPC proto_decode;
      handle_chain.add_handle(&proto_decode);
      handle_chain.do_handle(&request, &response);
    }
  } else {
    // 出错
    response.set_ret(ErrorCode::ERR_PARSER);
  }

  std::string response_body = response.SerializeAsString();

  int length = response_body.length();
  char* sendBuf = reinterpret_cast<char*>(malloc(length + 4));
  memcpy(sendBuf, reinterpret_cast<char*>(&length), 4);
  memcpy(sendBuf+4, response_body.c_str(), length);

  send_data = send_data + length + 4;
  send(sendBuf, length + 4,
       recvData.ip, recvData.port, recvData.uid, recvData.fd);

  // 设置结束时间
  recvData.data->endTime = sails::base::TimeT::getNowMs();

  if (response.ret() == ErrorCode::ERR_SUCCESS) {
    ServiceRegister::instance()->IncreaseCallTimes(
        request.servicename(), 1, 0, 1,
        recvData.data->endTime-recvData.data->recvTime,
        recvData.data->reqData.length() + 4,
        length + 4);
  } else {
    ServiceRegister::instance()->IncreaseCallTimes(
        request.servicename(), 1, 1, 0,
        recvData.data->endTime-recvData.data->recvTime,
        recvData.data->reqData.length() + 4,
        length + 4);
  }

  free(sendBuf);
}

}  // namespace sails
