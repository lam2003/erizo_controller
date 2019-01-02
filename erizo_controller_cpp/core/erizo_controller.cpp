#include "erizo_controller.h"

DEFINE_LOGGER(ErizoController, "ErizoController");

ErizoController::ErizoController() : redis_(nullptr),
                                     socket_io_(nullptr),
                                     //  ws_tls_(nullptr),
                                     //  ws_(nullptr),
                                     amqp_(nullptr),
                                     amqp_signaling_(nullptr),
                                     thread_pool_(nullptr),
                                     init_(false)
{
}

ErizoController::~ErizoController()
{
}

void ErizoController::asyncTask(const std::function<void()> &func)
{
    std::shared_ptr<erizo::Worker> worker = thread_pool_->getLessUsedWorker();
    worker->task(func);
}

int ErizoController::init()
{
    if (init_)
        return 0;

    thread_pool_ = std::unique_ptr<erizo::ThreadPool>(new erizo::ThreadPool(Config::getInstance()->worker_num_));
    thread_pool_->start();

    redis_ = std::make_shared<RedisHelper>();
    if (redis_->init())
    {
        ELOG_ERROR("Redis initialize failed");
        return 1;
    }

    // ws_tls_ = std::make_shared<WSServer<server_tls>>();
    // if (ws_tls_->init())
    // {
    //     ELOG_ERROR("WebsocketTLS initialize failed");
    //     return 1;
    // }

    // ws_tls_->setOnMessage([&](ClientHandler<server_tls> *client_hdl, const std::string &msg) {
    //     return onMessage(client_hdl, msg);
    // });
    // ws_tls_->setOnShutdown([&](ClientHandler<server_tls> *client_hdl) {

    // });

    // ws_ = std::make_shared<WSServer<server_plain>>();
    // if (ws_->init())
    // {
    //     ELOG_ERROR("Websocket initialize failed");
    //     return 1;
    // }

    // ws_->setOnMessage([&](ClientHandler<server_plain> *client_hdl, const std::string &msg) {
    //     return onMessage(client_hdl, msg);
    // });
    // ws_->setOnShutdown([&](ClientHandler<server_plain> *client_hdl) {

    // });

    socket_io_ = std::make_shared<SocketIOServer>();
    socket_io_->onMessage([&](SocketIOClientHandler *hdl, const std::string &msg) {
        return onMessage(hdl, msg);
    });
    socket_io_->onClose([&](SocketIOClientHandler *hdl) {
        onClose(hdl);
    });
    if (socket_io_->init())
    {
        ELOG_ERROR("SocketIO server initialize failed");
        return 1;
    }

    amqp_ = std::make_shared<AMQPRPC>();
    if (amqp_->init())
    {
        ELOG_ERROR("AMQP initialize failed");
        return 1;
    }

    amqp_signaling_ = std::make_shared<AMQPRecv>();
    if (amqp_signaling_->init([&](const std::string &msg) {
            onSignalingMessage(msg);
        }))
    {
        ELOG_ERROR("AMQPRecv initialize failed");
        return 1;
    }

    init_ = true;
    return 0;
}

void ErizoController::close()
{
    if (!init_)
        return;

    redis_->close();
    redis_.reset();
    redis_ = nullptr;

    socket_io_->close();

    // ws_tls_->close();
    // ws_tls_.reset();
    // ws_tls_ = nullptr;

    // ws_->close();
    // ws_.reset();
    // ws_ = nullptr;

    amqp_->close();
    amqp_.reset();
    amqp_ = nullptr;

    amqp_signaling_->close();
    amqp_signaling_.reset();
    amqp_signaling_ = nullptr;

    thread_pool_->close();
    thread_pool_.reset();
    thread_pool_ = nullptr;

    init_ = false;
}

void ErizoController::onSignalingMessage(const std::string &msg)
{
    asyncTask([=]() {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(msg, root))
            return;

        if (!root.isMember("data") ||
            root["data"].type() != Json::objectValue)
            return;

        Json::Value data = root["data"];
        if (!data.isMember("type") ||
            data["type"].type() != Json::stringValue ||
            !data.isMember("streamId") ||
            data["streamId"].type() != Json::stringValue ||
            !data.isMember("clientId") ||
            data["clientId"].type() != Json::stringValue ||
            !data.isMember("erizoId") ||
            data["erizoId"].type() != Json::stringValue ||
            !data.isMember("agentId") ||
            data["agentId"].type() != Json::stringValue)
            return;

        std::string agent_id = data["agentId"].asString();
        std::string erizo_id = data["erizoId"].asString();
        std::string client_id = data["clientId"].asString();
        std::string stream_id = data["streamId"].asString();
        std::string type = data["type"].asString();

        Json::FastWriter writer;
        std::vector<std::string> events;
        if (!type.compare("started"))
        {
            {
                Json::Value event;
                event[0] = "signaling_message_erizo";
                Json::Value mess;
                mess["agentId"] = agent_id;
                mess["erizoId"] = erizo_id;
                mess["type"] = "initializing";
                Json::Value event_data;
                event_data["streamId"] = stream_id;
                event_data["mess"] = mess;
                event[1] = event_data;
                events.push_back(writer.write(event));
            }
            {
                Json::Value event;
                event[0] = "signaling_message_erizo";
                Json::Value mess;
                mess["type"] = "started";
                Json::Value event_data;
                event_data["streamId"] = stream_id;
                event_data["mess"] = mess;
                event[1] = event_data;
                events.push_back(writer.write(event));
            }
        }
        else if (!type.compare("publisher_answer"))
        {
            if (!data.isMember("sdp") ||
                data["sdp"].type() != Json::stringValue)
                return;

            Json::Value event;
            event[0] = "signaling_message_erizo";
            Json::Value mess;
            mess["type"] = "answer";
            mess["sdp"] = data["sdp"].asString();
            Json::Value event_data;
            event_data["streamId"] = stream_id;
            event_data["mess"] = mess;
            event[1] = event_data;
            events.push_back(writer.write(event));
        }
        else if (!type.compare("subscriber_answer"))
        {
            if (!data.isMember("sdp") ||
                data["sdp"].type() != Json::stringValue)
                return;

            Json::Value event;
            event[0] = "signaling_message_erizo";
            Json::Value mess;
            mess["type"] = "answer";
            mess["sdp"] = data["sdp"].asString();
            mess["erizoId"] = erizo_id;
            Json::Value event_data;
            event_data["peerId"] = stream_id;
            event_data["mess"] = mess;
            event[1] = event_data;
            events.push_back(writer.write(event));
        }

        // std::shared_ptr<ClientHandler<server_plain>> plain_client_hdl = ws_->getClient(client_id);
        // if (plain_client_hdl != nullptr)
        // {
        //     for (const std::string &event : events)
        //         plain_client_hdl->sendEvent(event);
        // }
        // std::shared_ptr<ClientHandler<server_tls>> ssl_client_hdl = ws_tls_->getClient(client_id);
        // if (ssl_client_hdl != nullptr)
        // {
        //     for (const std::string &event : events)
        //         ssl_client_hdl->sendEvent(event);
        // }
        for (const std::string &event : events)
            socket_io_->sendEvent(client_id, event);
    });
}

int ErizoController::getErizo(const std::string &agent_id, const std::string &room_id, std::string &erizo_id)
{
    // std::string queuename = "ErizoAgent_" + agent_id;
    // Json::Value data;
    // data["method"] = "getErizo";
    // data["roomID"] = room_id;

    // int ret;
    // std::atomic<bool> callback_done;
    // int try_time = 3;
    // do
    // {
    //     try_time--;
    //     callback_done = false;
    //     amqp_->addRPC(Config::getInstance()->uniquecast_exchange_, queuename, queuename, data, [&](const Json::Value &root) {
    //         if (root.type() == Json::nullValue)
    //         {
    //             ret = 1;
    //             callback_done = true;
    //             return;
    //         }
    //         if (!root.isMember("erizo_id") ||
    //             root["erizo_id"].type() != Json::stringValue)
    //         {
    //             ret = 1;
    //             callback_done = true;
    //             return;
    //         }

    //         erizo_id = root["erizo_id"].asString();
    //         ret = 0;
    //         callback_done = true;
    //     });
    //     while (!callback_done)
    //         usleep(0);
    // } while (ret && try_time);

    int ret = 0;
    erizo_id = "2222222222";
    return ret;
}

int ErizoController::addPublisher(const std::string &erizo_id,
                                  const std::string &client_id,
                                  const std::string &stream_id,
                                  const std::string &label)
{
    std::string queuename = "Erizo_" + erizo_id;
    Json::Value data;
    data["method"] = "addPublisher";
    Json::Value args;
    args[0] = client_id;
    args[1] = stream_id;
    args[2] = label;
    args[3] = amqp_signaling_->getReplyTo();
    data["args"] = args;

    int ret;
    std::atomic<bool> callback_done;
    int try_time = 3;
    do
    {
        try_time--;
        callback_done = false;
        amqp_->addRPC(Config::getInstance()->uniquecast_exchange_, queuename, queuename, data, [&](const Json::Value &root) {
            if (root.type() == Json::nullValue)
            {
                ret = 1;
                callback_done = true;
                return;
            }
            if (!root.isMember("ret") ||
                root["ret"].type() != Json::intValue)
            {
                ret = 1;
                callback_done = true;
                return;
            }

            ret = root["ret"].asInt();
            callback_done = true;
        });
        while (!callback_done)
            usleep(0);
    } while (ret && try_time);
    return ret;
}

int ErizoController::addSubscriber(const std::string &erizo_id,
                                   const std::string &client_id,
                                   const std::string &stream_id,
                                   const std::string &label)
{
    std::string queuename = "Erizo_" + erizo_id;
    Json::Value data;
    data["method"] = "addSubscriber";
    Json::Value args;
    args[0] = client_id;
    args[1] = stream_id;
    args[2] = label;
    args[3] = amqp_signaling_->getReplyTo();
    data["args"] = args;

    int ret;
    std::atomic<bool> callback_done;
    int try_time = 3;
    do
    {
        try_time--;
        callback_done = false;
        amqp_->addRPC(Config::getInstance()->uniquecast_exchange_, queuename, queuename, data, [&](const Json::Value &root) {
            if (root.type() == Json::nullValue)
            {
                ret = 1;
                callback_done = true;
                return;
            }
            if (!root.isMember("ret") ||
                root["ret"].type() != Json::intValue)
            {
                ret = 1;
                callback_done = true;
                return;
            }
            ret = root["ret"].asInt();
            callback_done = true;
        });
        while (!callback_done)
            usleep(0);
    } while (ret && try_time);
    return ret;
}

int ErizoController::processSignaling(const std::string &erizo_id,
                                      const std::string &client_id,
                                      const std::string &stream_id,
                                      const Json::Value &msg)
{
    std::string queuename = "Erizo_" + erizo_id;
    Json::Value data;
    data["method"] = "processSignaling";
    Json::Value args;
    args[0] = client_id;
    args[1] = stream_id;
    args[2] = msg;
    data["args"] = args;

    int ret;
    std::atomic<bool> callback_done;
    int try_time = 3;
    do
    {
        try_time--;
        callback_done = false;
        amqp_->addRPC(Config::getInstance()->uniquecast_exchange_, queuename, queuename, data, [&](const Json::Value &root) {
            if (root.type() == Json::nullValue)
            {
                ret = 1;
                callback_done = true;
                return;
            }
            if (!root.isMember("ret") ||
                root["ret"].type() != Json::intValue)
            {
                ret = 1;
                callback_done = true;
                return;
            }
            ret = root["ret"].asInt();
            callback_done = true;
        });
        while (!callback_done)
            usleep(0);
    } while (ret && try_time);
    return ret;
}

void ErizoController::onClose(SocketIOClientHandler *hdl)
{
}

std::string ErizoController::onMessage(SocketIOClientHandler *hdl, const std::string &msg)
{
    Client &client = hdl->getClient();
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(msg, root))
    {
        ELOG_ERROR("Message parse failed");
        return "disconnect";
    }

    if (root.type() != Json::arrayValue ||
        root.size() < 2 ||
        root[0].type() != Json::stringValue ||
        root[1].type() != Json::objectValue)
    {
        ELOG_ERROR("Event format error");
        return "disconnect";
    }
    Json::Value reply = Json::nullValue;
    std::string event = root[0].asString();
    Json::Value data = root[1];
    if (!event.compare("token"))
        reply = handleToken(client, data);
    else if (!event.compare("publish"))
        reply = handlePublish(client, data);
    else if (!event.compare("subscribe"))
        reply = handleSubscribe(client, data);
    else if (!event.compare("signaling_message"))
        reply = handleSignaling(client, data);

    if (reply != Json::nullValue)
    {
        if (reply.type() == Json::arrayValue && reply.size() == 0)
            return "keep";

        Json::FastWriter writer;
        return writer.write(reply);
    }
    return "disconnect";
}

Json::Value ErizoController::handleToken(Client &client, const Json::Value &root)
{
    client.room_id = "test_room_id";
    client.agent_id = "1111111111";

    if (redis_->addClient(client.room_id, client.id))
    {
        ELOG_ERROR("addClient failed");
        return Json::nullValue;
    }

    std::vector<Publisher> publishers;
    if (redis_->getAllPublisher(client.room_id, publishers))
    {
        ELOG_ERROR("getAllPublisher failed");
        return Json::nullValue;
    }

    Json::Value data;
    for (const Publisher &publisher : publishers)
    {
        Json::Value temp;
        temp["id"] = publisher.id;
        temp["audio"] = true;
        temp["video"] = true;
        temp["data"] = true;
        temp["label"] = publisher.label;
        temp["screen"] = Json::stringValue;
        data["streams"].append(temp);
    }
    data["id"] = client.room_id;
    data["clientId"] = client.id;
    data["singlePC"] = false;
    data["defaultVideoBW"] = 300;
    data["maxVideoBW"] = 300;
    Json::Value ice_data;
    ice_data["url"] = "stun:stun.l.google.com:19302";
    Json::Value ice;
    ice[0] = ice_data;
    data["iceServers"] = ice;
    Json::Value reply;
    reply[0] = "success";
    reply[1] = data;
    return reply;
}

Json::Value ErizoController::handlePublish(Client &client, const Json::Value &root)
{
    int ret;

    if (!root.isMember("label") ||
        root["label"].type() != Json::stringValue)
    {
        ELOG_ERROR("Publish data format error");
        return Json::nullValue;
    }

    ret = getErizo(client.agent_id, client.room_id, client.erizo_id);
    if (ret)
    {
        ELOG_ERROR("getErizo failed,client %s", client.id);
        return Json::nullValue;
    }

    std::string label = root["label"].asString();
    Publisher publisher;
    publisher.id = Utils::getStreamID();
    publisher.erizo_id = client.erizo_id;
    publisher.agent_id = client.agent_id;
    publisher.label = label;
    client.publishers.push_back(publisher);

    if (addPublisher(publisher.erizo_id, client.id, publisher.id, label))
    {
        ELOG_ERROR("addPublisher failed,client %s,publisher %s", client.id, publisher.id);
        return Json::nullValue;
    }

    if (redis_->addPublisher(client.room_id, publisher))
    {
        ELOG_ERROR("Add publisher to redis failed");
        return Json::nullValue;
    }

    Json::Value reply;
    reply[0] = publisher.id;
    reply[1] = publisher.erizo_id;
    return reply;
}

Json::Value ErizoController::handleSubscribe(Client &client, const Json::Value &root)
{
    if (!root.isMember("streamId") ||
        root["streamId"].type() != Json::stringValue)
    {
        ELOG_ERROR("Publish data format error");
        return Json::nullValue;
    }

    std::string stream_id = root["streamId"].asString();
    Publisher publisher;
    if (redis_->getPublisher(client.room_id, stream_id, publisher))
    {
        ELOG_ERROR("Publisher not exist");
        return Json::nullValue;
    }

    Subscriber subscriber;
    subscriber.id = stream_id;
    subscriber.erizo_id = client.erizo_id;
    subscriber.agent_id = client.agent_id;
    client.subscribers.push_back(subscriber);

    if (addSubscriber(client.erizo_id, client.id, subscriber.id, publisher.label))
    {
        ELOG_ERROR("addSubscriber failed");
        return Json::nullValue;
    }

    Json::Value reply;
    reply[0] = true;
    reply[1] = subscriber.erizo_id;
    return reply;
}

Json::Value ErizoController::handleSignaling(Client &client, const Json::Value &root)
{
    if (!root.isMember("streamId") ||
        root["streamId"].type() != Json::stringValue ||
        !root.isMember("msg") ||
        root["msg"].type() != Json::objectValue)
    {
        ELOG_ERROR("Signaling message data format error");
        return Json::nullValue;
    }

    std::string erizo_id = client.erizo_id;
    std::string client_id = client.id;
    std::string stream_id = root["streamId"].asString();

    if (processSignaling(erizo_id, client_id, stream_id, root["msg"]))
    {
        ELOG_ERROR("processSignaling failed,client %s,stream %s", client_id, stream_id);
        return Json::nullValue;
    }

    return Json::arrayValue;
}

// int ErizoController::notifyNewPublisher(const std::string &client_id, const std::string &stream_id)
// {
//     std::shared_ptr<ClientHandler<server_plain>> plain_client_hdl = ws_->getClient(client_id);
//     if (plain_client_hdl != nullptr)
//     {
//         std::vector<std::string> client_ids;
//         Client &client = plain_client_hdl->getClient();
//         if (redis_->getAllClient(client.room_id, client_ids))
//         {
//             ELOG_ERROR("Redis getAllClient failed");
//             return 1;
//         }
//     }
//     return 0;
// }

// Client &ErizoController::getClient(const std::string &client_id)
// {

// }