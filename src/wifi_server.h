#pragma once
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include "config.h"

class PlotterServer {
    using CmdCallback = std::function<void(const char* line)>;

public:
    void begin(const char* ssid, const char* pass, bool apMode);
    void setCmdCallback(CmdCallback cb) { _cmdCb = cb; }
    void broadcast(const char* msg);
    void handleClient();
    bool hasClient() { return _hasClient; }

private:
    AsyncWebServer _server{HTTP_PORT};
    AsyncWebSocket _ws{"/ws"};
    CmdCallback _cmdCb;
    bool _hasClient{false};

    void setupPages();
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);
};
