#include <string>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

#include <lvgl.h>
#include <TFT_eSPI.h>

#include "NetData.h"

using namespace std;

const char *AP_NAME = "Router Monitor";

LV_FONT_DECLARE(tencent_w7_22)
LV_FONT_DECLARE(tencent_w7_24)

TFT_eSPI tft = TFT_eSPI();
static lv_disp_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];

// 定义页面
static lv_obj_t *loading_page = NULL;
static lv_obj_t *monitor_page = NULL;

static lv_obj_t *ip_label;
static lv_obj_t *loading_label;

// upload
static lv_obj_t *up_speed_label;
static lv_obj_t *up_speed_unit_label;

// download
static lv_obj_t *down_speed_label;
static lv_obj_t *down_speed_unit_label;

// cpu
static lv_obj_t *cpu_bar;
static lv_obj_t *cpu_value_label;

// memory
static lv_obj_t *mem_bar;
static lv_obj_t *mem_value_label;

// temperature
static lv_obj_t *temp_arc;
static lv_obj_t *temp_value_label;
static lv_style_t temp_arc_style;

static lv_obj_t *chart_network;
static lv_chart_series_t *up_line;
static lv_chart_series_t *down_line;
static lv_coord_t up_serise[10] = {0};
static lv_coord_t down_serise[10] = {0};

static lv_coord_t up_speed_max = 0;
static lv_coord_t down_speed_max = 0;

// 监测数值
double up_speed;
double down_speed;
double cpu_usage;
double mem_usage;
double temp_value;

WiFiManager wm;
static NetDataResponse netdata;

// 屏幕亮度设置，value [0, 256] 越小越亮, 越大越暗
void setBrightness(int value)
{
    pinMode(TFT_BL, INPUT);
    analogWrite(TFT_BL, value);
    pinMode(TFT_BL, OUTPUT);
}

void getCPUUsage()
{
    if (getNetDataInfo("system.cpu", netdata))
    {
        double softirq = netdata.latest_values[0].as<double>();
        double user = netdata.latest_values[1].as<double>();
        double system = netdata.latest_values[2].as<double>();
        double nice = netdata.latest_values[3].as<double>();

        cpu_usage = softirq + user + system + nice;
        Serial.print("CPU Usage: ");
        Serial.println(cpu_usage);
        lv_obj_set_hidden(loading_page, true);
        lv_obj_set_hidden(monitor_page, false);
    }
}

void getMemoryUsage()
{
    if (getNetDataInfo("system.ram", netdata))
    {
        // 获取所有相关的RAM值
        double freeRam = netdata.latest_values[0].as<double>();
        double usedRam = netdata.latest_values[1].as<double>();
        double cachedRam = netdata.latest_values[2].as<double>();
        double buffersRam = netdata.latest_values[3].as<double>();

        // 计算总RAM
        double totalRam = freeRam + usedRam + cachedRam + buffersRam;

        // 计算已使用RAM的百分比
        mem_usage = (usedRam / totalRam) * 100;

        Serial.print("Memory Available: ");
        Serial.println(mem_usage);
    }
}

void getTemperature()
{
    if (getNetDataInfo("sensors.temp_thermal_zone0_thermal_thermal_zone0", netdata))
    {
        temp_value = netdata.latest_values[0].as<double>();
        Serial.print("Temperature: ");
        Serial.println(temp_value);
    }
}
void setSpeedLabel(double speed, lv_obj_t *speed_label, lv_obj_t *unit_label)
{
    const char *unit;
    const char *format;

    if (speed < 100.0)
    {
        format = "%.2f";
        unit = "K/s";
    }
    else if (speed < 1000.0)
    {
        format = "%.1f";
        unit = "K/s";
    }
    else if (speed < 100000.0)
    {
        speed /= 1024.0;
        format = "%.2f";
        unit = "M/s";
    }
    else if (speed < 1000000.0)
    {
        speed /= 1024.0;
        format = "%.1f";
        unit = "M/s";
    }
    else
    {
        speed /= (1024.0 * 1024.0);
        format = "%.2f";
        unit = "G/s";
    }

    lv_label_set_text_fmt(speed_label, format, speed);
    lv_label_set_text(unit_label, unit);
}

void updateNetworkInfoLabel()
{
    setSpeedLabel(up_speed, up_speed_label, up_speed_unit_label);
    setSpeedLabel(down_speed, down_speed_label, down_speed_unit_label);
}

void updateChartRange()
{
    lv_coord_t max_speed = max(down_speed_max, up_speed_max);
    max_speed = max(max_speed, (lv_coord_t)16);
    lv_chart_set_range(chart_network, 0, (lv_coord_t)(max_speed * 1.1));
}

lv_coord_t updateNetSeries(lv_coord_t *series, double speed)
{
    lv_coord_t max = series[0];
    for (int index = 0; index < 9; index++)
    {
        series[index] = series[index + 1];
    }
    series[9] = (lv_coord_t)speed;
    for (int i = 0; i < 10; i++)
    {
        if (max < series[i])
            max = series[i];
    }
    return max;
}

void getNetworkReceived()
{
    if (getNetDataInfoWithDimension("net.pppoe_wan", netdata, "received"))
    {
        double receivedBits = netdata.latest_values[0].as<double>();
        Serial.print("Received: ");
        Serial.println(receivedBits);

        down_speed = receivedBits / 8.0; // byte = 8 bit
        down_speed_max = updateNetSeries(down_serise, down_speed);
        lv_chart_set_points(chart_network, down_line, down_serise);
    }
}

void getNetworkSent()
{
    if (getNetDataInfoWithDimension("net.pppoe_wan", netdata, "sent"))
    {
        double sentBits = netdata.latest_values[0].as<double>();
        Serial.print("Sent: ");
        Serial.println(sentBits);

        up_speed = -1 * sentBits / 8.0;
        up_speed_max = updateNetSeries(up_serise, up_speed);
        lv_chart_set_points(chart_network, up_line, up_serise);
    }
}

/* Display flushing */
void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors(&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

// task循环执行的函数
static void update(lv_task_t *task)
{
    getCPUUsage();
    getMemoryUsage();
    getTemperature();
    getNetworkReceived();
    getNetworkSent();
    updateChartRange();
    lv_chart_refresh(chart_network);

    updateNetworkInfoLabel();

    lv_label_set_text(ip_label, WiFi.localIP().toString().c_str());
    lv_bar_set_value(cpu_bar, cpu_usage, LV_ANIM_OFF);
    lv_label_set_text_fmt(cpu_value_label, "%2.1f%%", cpu_usage);

    lv_bar_set_value(mem_bar, mem_usage, LV_ANIM_OFF);
    lv_label_set_text_fmt(mem_value_label, "%2.0f%%", mem_usage);

    lv_label_set_text_fmt(temp_value_label, "%2.0f°C", temp_value);
    uint16_t end_value = 120 + 300 * temp_value / 100.0f;
    lv_color_t arc_color = temp_value > 75 ? lv_color_hex(0xff5d18) : lv_color_hex(0x50ff7d);
    lv_style_set_line_color(&temp_arc_style, LV_STATE_DEFAULT, arc_color);
    lv_obj_add_style(temp_arc, LV_ARC_PART_INDIC, &temp_arc_style);
    lv_arc_set_end_angle(temp_arc, end_value);

    Serial.print("⚠ Memory Usage:");
    Serial.println(ESP.getFreeHeap());
}

void saveConfigCallback()
{
    lv_label_set_text(loading_label, "Saved");
    lv_obj_set_hidden(loading_page, true);
    lv_obj_set_hidden(monitor_page, false);
}

void setup()
{
    Serial.begin(9600);
    setBrightness(180);

    // wm.resetSettings();
    wm.addParameter(&netdata_host);
    wm.addParameter(&netdata_port);
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setConfigPortalBlocking(false);
    bool state = wm.autoConnect(AP_NAME);

    tft.begin();
    tft.setRotation(0);

    lv_init();
    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);

    /*Initialize the display*/
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // 使用默认字体
    static lv_style_t font_default;
    lv_style_init(&font_default);
    lv_style_set_text_font(&font_default, LV_STATE_DEFAULT, &lv_font_unscii_8);

    static lv_style_t font_22;
    lv_style_init(&font_22);
    lv_style_set_text_font(&font_22, LV_STATE_DEFAULT, &tencent_w7_22);

    static lv_style_t font_24;
    lv_style_init(&font_24);
    lv_style_set_text_font(&font_24, LV_STATE_DEFAULT, &tencent_w7_24);

    static lv_style_t iconfont;
    lv_style_init(&iconfont);
    lv_style_set_text_font(&iconfont, LV_STATE_DEFAULT, &iconfont_symbol);

    // loading
    loading_page = lv_cont_create(lv_scr_act(), NULL);
    lv_obj_set_size(loading_page, 240, 240);
    lv_obj_set_style_local_bg_color(loading_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_border_color(loading_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_radius(loading_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    // spinner
    lv_obj_t *spinner = lv_spinner_create(loading_page, NULL);
    lv_obj_set_size(spinner, 100, 100);
    lv_obj_align(spinner, NULL, LV_ALIGN_CENTER, 0, 0);

    loading_label = lv_label_create(loading_page, NULL);
    lv_obj_add_style(loading_label, LV_LABEL_PART_MAIN, &font_default);
    lv_label_set_text(loading_label, "Loading ...");
    lv_obj_set_width(loading_label, lv_obj_get_width(lv_scr_act()));
    lv_obj_align(loading_label, NULL, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_auto_realign(loading_label, true);
    lv_obj_set_hidden(loading_page, false);

    // monitor
    monitor_page = lv_cont_create(lv_scr_act(), NULL);
    lv_obj_set_size(monitor_page, 240, 240);
    lv_obj_t *bg = lv_obj_create(monitor_page, NULL);
    lv_obj_clean_style_list(bg, LV_OBJ_PART_MAIN);
    lv_obj_set_style_local_bg_opa(bg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_100);
    lv_color_t bg_color = lv_color_hex(0x7381a2);
    lv_obj_set_style_local_bg_color(bg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, bg_color);
    lv_obj_set_size(bg, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_hidden(monitor_page, true);

    lv_color_t cont_color = lv_color_hex(0x081418);
    lv_obj_t *cont = lv_cont_create(monitor_page, NULL);
    lv_obj_set_auto_realign(cont, true);
    lv_obj_set_width(cont, 230);
    lv_obj_set_height(cont, 120);
    lv_obj_set_pos(cont, 5, 5);

    lv_cont_set_fit(cont, LV_FIT_TIGHT);
    lv_cont_set_layout(cont, LV_LAYOUT_COLUMN_MID);
    lv_obj_set_style_local_border_color(cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_bg_color(cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, cont_color);

    ip_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(ip_label, 10, 220);
    lv_label_set_text(ip_label, "0.0.0.0");

    lv_obj_t *up_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(up_label, 10, 18);
    lv_obj_add_style(up_label, LV_LABEL_PART_MAIN, &iconfont);
    lv_label_set_text(up_label, CUSTOM_SYMBOL_UPLOAD);
    lv_color_t speed_label_color = lv_color_hex(0x838a99);
    lv_obj_set_style_local_text_color(up_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);

    lv_obj_t *down_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(down_label, 120, 18);
    lv_obj_add_style(down_label, LV_LABEL_PART_MAIN, &iconfont);
    lv_label_set_text(down_label, CUSTOM_SYMBOL_DOWNLOAD);
    speed_label_color = lv_color_hex(0x838a99);
    lv_obj_set_style_local_text_color(down_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);

    // Upload & Download Speed Display
    up_speed_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(up_speed_label, 30, 15);
    lv_label_set_text(up_speed_label, "56.78");
    lv_obj_add_style(up_speed_label, LV_LABEL_PART_MAIN, &font_22);
    lv_obj_set_style_local_text_color(up_speed_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    up_speed_unit_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(up_speed_unit_label, 90, 18);
    lv_label_set_text(up_speed_unit_label, "K/S");
    lv_obj_set_style_local_text_color(up_speed_unit_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, speed_label_color);

    down_speed_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(down_speed_label, 142, 15);
    lv_label_set_text(down_speed_label, "12.34");
    lv_obj_add_style(down_speed_label, LV_LABEL_PART_MAIN, &font_22);
    lv_obj_set_style_local_text_color(down_speed_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    down_speed_unit_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(down_speed_unit_label, 202, 18);
    lv_label_set_text(down_speed_unit_label, "M/S");
    lv_obj_set_style_local_text_color(down_speed_unit_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, speed_label_color);

    /*Create a chart_network*/
    chart_network = lv_chart_create(monitor_page, NULL);
    lv_obj_set_size(chart_network, 220, 70);
    lv_obj_align(chart_network, NULL, LV_ALIGN_CENTER, 0, -40);
    lv_chart_set_type(chart_network, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_network, 0, 4096);
    lv_chart_set_point_count(chart_network, 10);
    lv_chart_set_update_mode(chart_network, LV_CHART_UPDATE_MODE_SHIFT);

    /*Add a faded are effect*/
    lv_obj_set_style_local_bg_opa(chart_network, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_OPA_50); /*Max. opa.*/
    lv_obj_set_style_local_bg_grad_dir(chart_network, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_obj_set_style_local_bg_main_stop(chart_network, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 255); /*Max opa on the top*/
    lv_obj_set_style_local_bg_grad_stop(chart_network, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0);   /*Transparent on the bottom*/

    /*Add two data series*/
    up_line = lv_chart_add_series(chart_network, LV_COLOR_RED);
    down_line = lv_chart_add_series(chart_network, LV_COLOR_GREEN);

    // /*Directly set points on 'down_line'*/
    lv_chart_set_points(chart_network, up_line, up_serise);
    lv_chart_set_points(chart_network, down_line, down_serise);

    lv_chart_refresh(chart_network); /*Required after direct set*/

    // 绘制进度条 CPU 占用
    lv_obj_t *cpu_title = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(cpu_title, 5, 140);
    lv_label_set_text(cpu_title, "CPU");
    lv_obj_set_style_local_text_color(cpu_title, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    cpu_value_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(cpu_value_label, 85, 135);
    lv_label_set_text(cpu_value_label, "34%");
    lv_obj_add_style(cpu_value_label, LV_LABEL_PART_MAIN, &font_22);
    lv_obj_set_style_local_text_color(cpu_value_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    cpu_bar = lv_bar_create(monitor_page, NULL);
    lv_obj_set_size(cpu_bar, 130, 10);
    lv_obj_set_pos(cpu_bar, 5, 160);

    lv_color_t cpu_bar_bg_color = lv_color_hex(0x1e3644);
    lv_color_t cpu_bar_indic_color = lv_color_hex(0x63d0fc);
    lv_obj_set_style_local_bg_color(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cpu_bar_bg_color);
    lv_obj_set_style_local_bg_color(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, cpu_bar_indic_color);
    lv_obj_set_style_local_border_width(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_width(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 2);

    lv_obj_set_style_local_border_color(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_border_color(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_border_side(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM);
    lv_obj_set_style_local_radius(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_radius(cpu_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 0);

    // 绘制内存占用
    lv_obj_t *men_title = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(men_title, 5, 180);
    lv_label_set_text(men_title, "Memory");
    lv_obj_set_style_local_text_color(men_title, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    mem_value_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(mem_value_label, 85, 175);
    lv_label_set_text(mem_value_label, "42%");
    lv_obj_add_style(mem_value_label, LV_LABEL_PART_MAIN, &font_22);
    lv_obj_set_style_local_text_color(mem_value_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    mem_bar = lv_bar_create(monitor_page, NULL);
    lv_obj_set_pos(mem_bar, 5, 200);
    lv_obj_set_size(mem_bar, 130, 10);
    lv_obj_set_style_local_bg_color(mem_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cpu_bar_bg_color);
    lv_obj_set_style_local_bg_color(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, cpu_bar_indic_color);
    lv_obj_set_style_local_border_width(mem_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(mem_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_border_width(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_border_side(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM);
    lv_obj_set_style_local_radius(mem_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_radius(mem_bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 0);

    // 绘制温度表盘
    static lv_style_t arc_style;
    lv_style_reset(&arc_style);
    lv_style_init(&arc_style);
    lv_style_set_bg_opa(&arc_style, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_style_set_border_opa(&arc_style, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    lv_style_set_line_width(&arc_style, LV_STATE_DEFAULT, 100);
    lv_style_set_line_color(&arc_style, LV_STATE_DEFAULT, lv_color_hex(0x081418));
    lv_style_set_line_rounded(&arc_style, LV_STATE_DEFAULT, false);

    lv_style_init(&temp_arc_style);
    lv_style_set_line_width(&temp_arc_style, LV_STATE_DEFAULT, 5);
    lv_style_set_pad_left(&temp_arc_style, LV_STATE_DEFAULT, 5);
    lv_style_set_line_color(&temp_arc_style, LV_STATE_DEFAULT, lv_color_hex(0xff5d18));

    temp_arc = lv_arc_create(monitor_page, NULL);
    lv_arc_set_bg_angles(temp_arc, 0, 360);
    lv_arc_set_start_angle(temp_arc, 120);
    lv_obj_set_pos(temp_arc, 125, 120);
    lv_obj_set_size(temp_arc, 125, 125);
    lv_arc_set_end_angle(temp_arc, 420);
    lv_obj_add_style(temp_arc, LV_ARC_PART_BG, &arc_style);
    lv_obj_add_style(temp_arc, LV_ARC_PART_INDIC, &temp_arc_style);

    temp_value_label = lv_label_create(monitor_page, NULL);
    lv_obj_set_pos(temp_value_label, 160, 170);
    lv_label_set_text(temp_value_label, "72℃");
    lv_obj_add_style(temp_value_label, LV_LABEL_PART_MAIN, &font_24);
    lv_obj_set_style_local_text_color(temp_value_label, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    lv_task_create(update, 1000, LV_TASK_PRIO_MID, 0);

    if (state)
    {
        Serial.println("Ready");
    }
    else
    {
        lv_label_set_text(loading_label, AP_NAME);
    }
}

void loop()
{
    lv_task_handler();
    wm.process();
}
