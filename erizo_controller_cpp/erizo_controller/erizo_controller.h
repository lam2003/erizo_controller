#ifndef ERIZO_CONTROLLER_H
#define ERIZO_CONTROLLER_H

#include <thread>
#include <memory>
#include <atomic>

#include <json/json.h>

#include "websocket/server.hpp"
#include "redis/redis_helper.h"
#include "rabbitmq/amqp_rpc.h"
#include "rabbitmq/amqp_rpc_boardcast.h"

struct ErizoAgent
{
  std::string id;
  std::string ip;
  int timeout;
};

class ErizoController
{
  DECLARE_LOGGER();

public:
  ErizoController();
  ~ErizoController();

  int init();
  void close();

private:
  void getErizoAgents(const Json::Value &root);
  int daptch(const std::string &msg, std::string &reply_msg);


  
  Json::Value handleToken(const Json::Value &root);

private:
  std::shared_ptr<RedisHelper> redis_;
  std::shared_ptr<WSServer<server_tls>> ws_tls_;
  std::shared_ptr<WSServer<server_plain>> ws_;
  std::shared_ptr<AMQPRPC> amqp_;
  std::shared_ptr<AMQPRPCBoardcast> amqp_boardcast_;

  std::atomic<bool> run_;
  std::unique_ptr<std::thread> keeplive_thread_;

  std::mutex agents_map_mux_;
  std::map<std::string, ErizoAgent> agents_map_;
};

#endif
