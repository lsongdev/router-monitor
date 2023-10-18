#ifndef __NETDATA_H
#define __NETDATA_H

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <string>

WiFiManagerParameter netdata_host("host", "NetData Host", "192.168.8.1", 40);
WiFiManagerParameter netdata_port("port", "NetData Port", "19999", 6);

class NetDataResponse
{
public:
    int api;
    String id;
    String name;

    int view_update_every;
    int update_every;
    long first_entry;
    long last_entry;
    long before;
    long after;
    String group;
    String options_0;
    String options_1;

    JsonArray dimension_names;
    JsonArray dimension_ids;
    JsonArray latest_values;
    JsonArray view_latest_values;
    int dimensions;
    int points;
    String format;
    JsonArray result;
    double min;
    double max;
};

void parseNetDataResponse(WiFiClient &client, NetDataResponse &data)
{
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, client);

    if (error)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    data.api = doc["api"]; // 1
    data.id = doc["id"].as<String>();
    data.name = doc["name"].as<String>();
    data.view_update_every = doc["view_update_every"]; // 1
    data.update_every = doc["update_every"];           // 1
    data.first_entry = doc["first_entry"];             // 1691505357
    data.last_entry = doc["last_entry"];               // 1691505905
    data.after = doc["after"];                         // 1691505903
    data.before = doc["before"];                       // 1691505904
    data.group = doc["group"].as<String>();            // "average"

    data.options_0 = doc["options"][0].as<String>(); // "jsonwrap"
    data.options_1 = doc["options"][1].as<String>(); // "natural-points"

    data.dimension_names = doc["dimension_names"];
    data.dimension_ids = doc["dimension_ids"];
    data.latest_values = doc["latest_values"];
    data.view_latest_values = doc["view_latest_values"];

    data.dimensions = doc["dimensions"];      // 3
    data.points = doc["points"];              // 2
    data.format = doc["format"].as<String>(); // "array"

    data.result = doc["result"];
    data.min = doc["min"]; // 2.1594684
    data.max = doc["max"]; // 3.7468776
}

/**
 * 从软路由NetData获取监控信息
 * ChartID:
 *  system.cpu - CPU占用率信息
 *  sensors.temp_thermal_zone0_thermal_thermal_zone - CPU 温度信息
 */
bool getNetDataInfoWithDimension(String chartID, NetDataResponse &data, String dimensions_filter)
{
    WiFiClient client;

    const char* NETDATA_HOST = netdata_host.getValue();
    const char* NETDATA_PORT = netdata_port.getValue();

    String path = "/api/v1/data";
    path = path + "?chart=" + chartID;
    path = path + "&format=json";
    path = path + "&points=1";
    path = path + "&gtime=0";
    path = path + "&group=average";
    path = path + "&dimensions=" + dimensions_filter;
    path = path + "&options=s%7Cjsonwrap%7Cnonzero&after=-2";

    // 建立http请求信息
    String httpRequest = "";
    httpRequest = httpRequest + "GET " + path + " HTTP/0.1\r\n";
    httpRequest = httpRequest + "Host: " + NETDATA_HOST + "\r\n";
    httpRequest = httpRequest + "Connection: close\r\n\r\n";

    bool ret = false;
    // 尝试连接服务器
    if (client.connect(NETDATA_HOST, atoi(NETDATA_PORT)))
    {
        // 向服务器发送http请求信息
        client.print(httpRequest);
        Serial.println("Sending request: ");
        Serial.println(httpRequest);

        // 获取并显示服务器响应状态行
        String response_status = client.readStringUntil('\n');
        Serial.print("response_status: ");
        Serial.println(response_status);
        // 使用find跳过HTTP响应头
        if (client.find("\r\n\r\n"))
        {
            Serial.println("Found Header End. Start Parsing.");
        }

        // 利用ArduinoJson库解析NetData返回的信息
        parseNetDataResponse(client, data);
        ret = true;
    }
    else
    {
        Serial.println(" connection failed!");
    }
    // 断开客户端与服务器连接工作
    client.stop();
    return ret;
}

bool getNetDataInfo(String chartID, NetDataResponse &data)
{
    return getNetDataInfoWithDimension(chartID, data, "");
}

#endif
