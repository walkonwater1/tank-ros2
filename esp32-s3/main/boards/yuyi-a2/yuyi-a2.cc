#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "codecs/box_audio_codec.h"
#include "emoji_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_lcd_types.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_timer.h>

// 电机控制函数声明（确保C链接）
extern "C" {
#include "motion_control.h"
}

// 电机测试函数声明
#include "motor_test.h"

// 坦克控制函数声明
#include "tank_control.h"
#include "tsp_mqtt_client.h"
#include "ota.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "yuyi-a2"
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_2
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_READ_INTERVAL_MS (10 * 1000) // 10 秒

#define AUDIO_INPUT_REFERENCE true

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class yuyi_a2Board : public WifiBoard
{
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display *display_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    adc_oneshot_unit_handle_t adc1_handle_;
    adc_cali_handle_t adc1_cali_handle_;
    bool adc_calibrated_;
    esp_timer_handle_t battery_timer_;
    int last_battery_voltage_;
    int last_battery_percentage_;
    // 充电器状态：LGS4056 的 CHRG / DONE 都是低有效。
    //   CHRG=0,DONE=1 → 正在充电；CHRG=1,DONE=0 → 已充满；CHRG=1,DONE=1 → 未接充电器。
    bool last_battery_full_ = false;
    int last_charge_pin_ = -1;
    int last_done_pin_ = -1;
    bool led_on_ = false;

    // 电池定时器回调函数
    static void BatteryTimerCallback(void *arg)
    {
        static uint8_t timer_count_;
        timer_count_++;
        if (timer_count_ >= 10)
        {
            yuyi_a2Board *board = static_cast<yuyi_a2Board *>(arg);
            board->UpdateBatteryStatus();
            timer_count_ = 0;
        }

        gpio_set_level(BUILTIN_LED_GPIO, timer_count_ % 2);
    }

    // 更新电池状态
    void UpdateBatteryStatus()
    {
        last_battery_voltage_ = ReadBatteryVoltage();
        last_battery_percentage_ = CalculateBatteryPercentage(last_battery_voltage_);

        if (adc_calibrated_)
        {
            // ESP_LOGI(TAG, "电池状态更新 - 电压: %d mV, 电量: %d%%",
            //          last_battery_voltage_, last_battery_percentage_);
        }
        else
        {
            ESP_LOGI(TAG, "电池ADC未校准，无法准确读取电压");
        }
    }

    void InitializeCodecI2c()
    {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new EmoteDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons()
    {
        boot_button_.OnClick([this]()
                             {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            } });
        boot_button_.OnPressDown([this]()
                                 { Application::GetInstance().StartListening(); });
        boot_button_.OnPressUp([this]()
                               { Application::GetInstance().StopListening(); });
        volume_up_button_.OnClick([this]()
                                  {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume)); });

        volume_up_button_.OnLongPress([this]()
                                      {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME); });

        volume_down_button_.OnClick([this]()
                                    {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume)); });

        volume_down_button_.OnLongPress([this]()
                                        {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED); });
    }

    void InitializeTools()
    {
        // 初始化坦克运动控制系统
        TankControlInit();

        auto& mcp_server = McpServer::GetInstance();
        
        // 添加电机测试工具
        // mcp_server.AddTool("self.motor.test", "运行电机测试序列", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        //     StartMotorTest(this);
        //     return true;
        // });

        // 添加前进工具（5秒后自动停止）
        mcp_server.AddTool("self.tank.forward", "小车向前移动5秒", PropertyList(), [this](const PropertyList&) -> ReturnValue {
            return TankMoveForward();
        });

        // 添加后退工具（5秒后自动停止）
        mcp_server.AddTool("self.tank.backward", "小车向后移动5秒", PropertyList(), [this](const PropertyList&) -> ReturnValue {
            return TankMoveBackward();
        });

        // 添加左转工具（5秒后自动停止）
        mcp_server.AddTool("self.tank.turn_left", "小车向左转5秒", PropertyList(), [this](const PropertyList&) -> ReturnValue {
            return TankTurnLeft();
        });

        // 添加右转工具（5秒后自动停止）
        mcp_server.AddTool("self.tank.turn_right", "小车向右转5秒", PropertyList(), [this](const PropertyList&) -> ReturnValue {
            return TankTurnRight();
        });

        // 添加舞蹈工具
        mcp_server.AddTool("self.tank.dance", "小车跳个舞", PropertyList(), [this](const PropertyList&) -> ReturnValue {
            TankDance();
            return true;
        });

        // 添加停止工具
        mcp_server.AddTool("self.tank.stop", "小车停止", PropertyList(), [this](const PropertyList&) -> ReturnValue {
            return TankStop();
        });
    }

    void InitializeBatteryAdc()
    {
        // 初始化 ADC1 单次采样模式
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle_));

        // 配置 ADC 通道
        adc_oneshot_chan_cfg_t config = {
            .atten = BATTERY_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle_, BATTERY_ADC_CHANNEL, &config));

        // 初始化校准
        adc_cali_handle_t handle = NULL;
        esp_err_t ret = ESP_FAIL;
        adc_calibrated_ = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        if (!adc_calibrated_)
        {
            ESP_LOGI(TAG, "使用曲线拟合校准方案");
            adc_cali_curve_fitting_config_t cali_config = {
                .unit_id = ADC_UNIT_1,
                .chan = BATTERY_ADC_CHANNEL,
                .atten = BATTERY_ADC_ATTEN,
                .bitwidth = ADC_BITWIDTH_DEFAULT,
            };
            ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
            if (ret == ESP_OK)
            {
                adc_calibrated_ = true;
            }
        }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        if (!adc_calibrated_)
        {
            ESP_LOGI(TAG, "使用线性拟合校准方案");
            adc_cali_line_fitting_config_t cali_config = {
                .unit_id = ADC_UNIT_1,
                .atten = BATTERY_ADC_ATTEN,
                .bitwidth = ADC_BITWIDTH_DEFAULT,
            };
            ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
            if (ret == ESP_OK)
            {
                adc_calibrated_ = true;
            }
        }
#endif

        adc1_cali_handle_ = handle;
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "ADC校准成功");
        }
        else if (ret == ESP_ERR_NOT_SUPPORTED || !adc_calibrated_)
        {
            ESP_LOGW(TAG, "eFuse未烧录，跳过软件校准");
        }
        else
        {
            ESP_LOGE(TAG, "无效参数或内存不足");
        }
    }

    void InitializeTimer()
    {
        esp_timer_create_args_t timer_args = {
            .callback = &BatteryTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_timer",
            .skip_unhandled_events = false,
        };

        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &battery_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(battery_timer_, 1000 * 1000)); // 微秒为单位
    }

    // 读取电池电压(mV)
    int ReadBatteryVoltage()
    {
        int adc_raw = 0;
        long long sum = 0;
        int voltage = 0;
        for (int i = 0; i < 10; i++)
        {
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle_, BATTERY_ADC_CHANNEL, &adc_raw));
            sum += adc_raw;
        }
        adc_raw = sum / 10;

        // ESP_LOGI(TAG, "电池ADC原始数据: %d", adc_raw);

        if (adc_calibrated_)
        {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle_, adc_raw, &voltage));
        }

        // 分压电阻1:2，所以乘以2
        return voltage * 3;
    }

    // 计算电池电量百分比
    int CalculateBatteryPercentage(int voltage)
    {
        if (!adc_calibrated_ || voltage <= 0)
        {
            return -1; // 返回-1表示无法读取
        }

        // 假设电池电压范围为3.0V-4.2V
        const int min_voltage = 3000; // 3.0V
        const int max_voltage = 4200; // 4.2V

        if (voltage < min_voltage)
        {
            return 0;
        }
        else if (voltage > max_voltage)
        {
            return 100;
        }
        else
        {
            return (voltage - min_voltage) * 100 / (max_voltage - min_voltage);
        }
    }

public:
    yuyi_a2Board() : boot_button_(BOOT_BUTTON_GPIO),
                     volume_up_button_(VOLUME_UP_BUTTON_GPIO),
                     volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
                     last_battery_voltage_(0), last_battery_percentage_(-1)
    {
        ESP_LOGI(TAG, "Initializing yuyi_a2 Board");
        // init gpio led
        gpio_config_t led_config = {
            .pin_bit_mask = 1ULL << BUILTIN_LED_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&led_config));

        InitializeCodecI2c();
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeBatteryAdc();

        // 电机初始化（必须在 InitializeTools 之前）
        motor_ledc_init();
        
        // 确保电机初始状态为停止
        spark_bot_motion_control(0, 0);
        
        // 初始化工具（依赖电机）
        InitializeTools();

        // 独立连接新元 TSP；当前阶段只上报在线状态，不接管电机控制。
        if (TspMqttStart() != ESP_OK) {
            ESP_LOGW(TAG, "TSP MQTT startup failed; the main robot functions remain available");
        }

        // tank-ros2: 默认关闭上电自走。需要厂商演示行为时定义
        // TANK_ENABLE_POWER_ON_DEMO 重新编译即可。
#ifdef TANK_ENABLE_POWER_ON_DEMO
        TankMoveForward();
#else
        // 上电不自走，等待语音或上位机命令
#endif

        // 初始读取一次电量
        UpdateBatteryStatus();

        // 启动定时器定
        InitializeTimer();
    }

    ~yuyi_a2Board()
    {
        TspMqttStop();

        // 停止定时器
        esp_timer_stop(battery_timer_);
        esp_timer_delete(battery_timer_);

        // 释放ADC资源
        ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle_));

        if (adc_calibrated_)
        {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(adc1_cali_handle_));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
            ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(adc1_cali_handle_));
#endif
        }
    }

    virtual Led *GetLed() override
    {
        static NoLed led;
        return &led;
    }

    virtual AudioCodec *GetAudioCodec() override
    {
        // static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        //                                     AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
        //                                     AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, true);
        static BoxAudioCodec audio_codec(
            codec_i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override
    {
        return display_;
    }

    // 获取最后一次读取的电池电压(mV)
    int GetBatteryVoltage()
    {
        return last_battery_voltage_;
    }

    // 获取最后一次计算的电池电量百分比
    virtual bool GetBatteryLevel(int &level, bool &charging, bool &discharging)
    {
        level = last_battery_percentage_;

        // LGS4056（TP4056 同族）：CHRG / DONE 均为开漏、低有效。
        //   (CHRG=0, DONE=1) 正在充电
        //   (CHRG=1, DONE=0) 充电完成 —— **已充满，不是放电**
        //   (CHRG=1, DONE=1) 未接充电器，电池供电
        //   (0,0) 两个状态脚同时拉低，硬件上不应出现；按放电兜底。
        // ⚠️ 旧代码把 (1,0) 判成 discharging=true，于是"充了一整天"在界面上
        //    一直显示放电中、电量 84%。2026-11-26 修正。
        const int charge_level = gpio_get_level(CHARGING_GPIO);
        const int done_level = gpio_get_level(DONE_GPIO);
        last_charge_pin_ = charge_level;
        last_done_pin_ = done_level;
        last_battery_full_ = (charge_level == 1 && done_level == 0);

        if (last_battery_full_)
        {
            // 充电器已经判满：以充电器为准，电压在负载/回弹下可能只有 4.0 V 左右，
            // 不能再按 3.0–4.2 V 线性换算成 84%。
            level = 100;
            charging = false;
            discharging = false;
        }
        else if (charge_level == 0 && done_level == 1)
        {
            charging = true;
            discharging = false;
        }
        else
        {
            charging = false;
            discharging = true;
        }

        return true;
    }

    // 供 TSP 遥测/诊断使用
    bool IsBatteryFull() const { return last_battery_full_; }
    int GetChargePinLevel() const { return last_charge_pin_; }
    int GetDonePinLevel() const { return last_done_pin_; }

    // 电机控制接口
    void ControlMotor(float x, float y)
    {
        spark_bot_motion_control(x, y);
    }

    void SetMotorSpeedCoefficients(float coefficient)
    {
        set_motor_speed_coefficients(coefficient);
    }

    void PerformDance()
    {
        spark_bot_dance();
    }

    void StopMotors()
    {
        spark_bot_motion_control(0, 0);
    }

    // 直接控制左右电机速度
    void SetMotorSpeeds(int left_speed, int right_speed)
    {
        // 限制速度在-100到100之间
        if (left_speed > MOTOR_SPEED_MAX) left_speed = MOTOR_SPEED_MAX;
        if (left_speed < -MOTOR_SPEED_MAX) left_speed = -MOTOR_SPEED_MAX;
        if (right_speed > MOTOR_SPEED_MAX) right_speed = MOTOR_SPEED_MAX;
        if (right_speed < -MOTOR_SPEED_MAX) right_speed = -MOTOR_SPEED_MAX;
        
        // 转换为x,y控制
        float y = (left_speed + right_speed) / 2.0f / MOTOR_SPEED_MAX;
        float x = (left_speed - right_speed) / 2.0f / MOTOR_SPEED_MAX;
        spark_bot_motion_control(x, y);
    }
};

// ---------------------------------------------------------------------------
// C entry points for tsp_mqtt_client.c (that file is plain C).
//
// All of these only read/write values the board already owns.  They are called
// from the TSP MQTT tasks at 1 Hz, so they must stay cheap and must never block.
// ---------------------------------------------------------------------------
extern "C" {

bool TankGetPowerStatus(int *voltage_mv, int *percentage,
                        bool *charging, bool *discharging,
                        bool *full, int *charge_pin, int *done_pin)
{
    auto &board = static_cast<yuyi_a2Board &>(Board::GetInstance());
    int level = -1;
    bool ch = false;
    bool dis = false;
    const bool known = board.GetBatteryLevel(level, ch, dis);

    if (voltage_mv != nullptr) {
        *voltage_mv = board.GetBatteryVoltage();
    }
    if (percentage != nullptr) {
        *percentage = known ? level : -1;
    }
    if (charging != nullptr) {
        *charging = ch;
    }
    if (discharging != nullptr) {
        *discharging = dis;
    }
    if (full != nullptr) {
        *full = board.IsBatteryFull();
    }
    if (charge_pin != nullptr) {
        *charge_pin = board.GetChargePinLevel();
    }
    if (done_pin != nullptr) {
        *done_pin = board.GetDonePinLevel();
    }
    return known;
}

int TankGetVolume(void)
{
    auto *codec = Board::GetInstance().GetAudioCodec();
    return codec != nullptr ? codec->output_volume() : -1;
}

bool TankSetVolume(int value)
{
    if (value < 0 || value > 100) {
        return false;
    }
    auto *codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        return false;
    }
    codec->SetOutputVolume(value);
    return true;
}

bool TankGetWifiStatus(char *ssid, size_t ssid_cap,
                       int *rssi, int *channel,
                       char *ip, size_t ip_cap,
                       bool *connected, bool *config_mode)
{
    auto &station = WifiStation::GetInstance();
    const bool up = station.IsConnected();

    if (ssid != nullptr && ssid_cap > 0) {
        std::snprintf(ssid, ssid_cap, "%s", station.GetSsid().c_str());
    }
    if (rssi != nullptr) {
        *rssi = up ? static_cast<int>(station.GetRssi()) : 0;
    }
    if (channel != nullptr) {
        *channel = up ? static_cast<int>(station.GetChannel()) : 0;
    }
    if (ip != nullptr && ip_cap > 0) {
        std::snprintf(ip, ip_cap, "%s", station.GetIpAddress().c_str());
    }
    if (connected != nullptr) {
        *connected = up;
    }
    if (config_mode != nullptr) {
        *config_mode = static_cast<WifiBoard &>(Board::GetInstance()).IsWifiConfigMode();
    }
    return up;
}

size_t TankGetMcpToolNames(const char **names, size_t max)
{
    // Static so the returned c_str() pointers stay valid until the next call.
    // Only the TSP telemetry task calls this, once per second.
    static std::vector<std::string> cached;
    cached = McpServer::GetInstance().GetToolNames();

    const size_t count = cached.size() < max ? cached.size() : max;
    for (size_t i = 0; i < count; ++i) {
        names[i] = cached[i].c_str();
    }
    return count;
}

static std::atomic<bool> s_upgrade_in_progress{false};

int TankGetDeviceState(void)
{
    return static_cast<int>(Application::GetInstance().GetDeviceState());
}

bool TankIsFirmwareUpgrading(void)
{
    // s_upgrade_in_progress 在任务启动时先置位；状态机要 3 秒后才进
    // kDeviceStateUpgrading。两个都看，平台从下发那一刻就能显示"升级中"。
    return s_upgrade_in_progress.load()
        || Application::GetInstance().GetDeviceState() == kDeviceStateUpgrading;
}

// ---------------------------------------------------------------------------
// TSP-triggered firmware upgrade.
//
// The upgrade itself is the same path the xiaozhi cloud / MCP tool uses:
// Application::UpgradeFirmware() -> Ota::Upgrade() -> write the inactive OTA
// slot -> esp_ota_set_boot_partition() -> reboot.  It must NOT run in the MQTT
// event task (download can take minutes), so this wrapper only starts a task
// and the TSP handler acks immediately.
// ---------------------------------------------------------------------------
static void TankOtaTask(void *arg)
{
    auto *url = static_cast<std::string *>(arg);
    Ota ota;
    if (!Application::GetInstance().UpgradeFirmware(ota, *url)) {
        // UpgradeFirmware() also resets this on its failure path; keep a second
        // guard here so a future early-return cannot leave the TSP state stuck.
        Application::GetInstance().SetDeviceState(kDeviceStateIdle);
    }
    delete url;
    s_upgrade_in_progress.store(false);
    vTaskDelete(nullptr);
}

bool TankStartFirmwareUpgrade(const char *url)
{
    if (url == nullptr || url[0] == '\0') {
        return false;
    }
    bool expected = false;
    if (!s_upgrade_in_progress.compare_exchange_strong(expected, true)) {
        return false;
    }

    auto *copy = new std::string(url);
    if (xTaskCreate(TankOtaTask, "tsp_ota", 12288, copy, 5, nullptr) != pdPASS) {
        delete copy;
        s_upgrade_in_progress.store(false);
        return false;
    }
    return true;
}

}  // extern "C"

DECLARE_BOARD(yuyi_a2Board);
