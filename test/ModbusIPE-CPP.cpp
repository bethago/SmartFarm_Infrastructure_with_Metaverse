#include <drogon/drogon.h>
#include <modbus/modbus.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <fstream>
#include <atomic>
#include <ctime>
#include <csignal>
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <functional>

using namespace std::chrono_literals;
using json = nlohmann::json;

constexpr char   SERIAL_PORT[]       = "/dev/ttyXRUSB0";
constexpr int    BAUDRATE            = 115200;
constexpr int    MODBUS_SLAVE_ID     = 1;
constexpr int    HTTP_PORT           = 3002;
constexpr int    MONITOR_INTERVAL_MS = 6000;
constexpr int    NULL_READ_LIMIT     = 20;
constexpr int    HTTP_DELAY_MS       = 100;
const   std::string CSE_BASE_URL     = "http://192.168.0.6:3000/TinyIoT/solar_controller";

static modbus_t*       ctx     = nullptr;
static std::atomic<bool> connected{false};
static std::atomic<int>  nullCnt {0};
static float           SOC     = 0.0f;
static long            tcnt_t  = 0;

struct Reg { const char* module, *name; int addr, len; float scale; };
static const Reg regs[] = {
    {"battery",           "level",      0x311A,1,   1},
    {"battery",           "current",    0x331B,1, 100},
    {"battery",           "voltage",    0x331A,1, 100},
    {"battery",           "power",      0x3106,2, 100},
    {"battery",           "maxvolt",    0x3302,2, 100},
    {"battery",           "minvolt",    0x3303,2, 100},
    {"battery",           "temp",       0x331D,2, 100},

    {"energyGeneration",  "power",      0x3102,2, 100},
    {"energyGeneration",  "current",    0x3101,1, 100},
    {"energyGeneration",  "voltage",    0x3100,1, 100},
    {"energyGeneration",  "daily",      0x330C,2, 100},
    {"energyGeneration",  "monthly",    0x330E,2, 100},
    {"energyGeneration",  "annual",     0x3310,2, 100},
    {"energyGeneration",  "total",      0x3312,2, 100},
    {"energyGeneration",  "maxvolt",    0x3300,2, 100},
    {"energyGeneration",  "minvolt",    0x3301,2, 100},

    {"energyConsumption", "power",      0x310E,2, 100},
    {"energyConsumption", "voltage",    0x310C,1, 100},
    {"energyConsumption", "current",    0x310D,1, 100},
    {"energyConsumption", "daily",      0x3304,2, 100},
    {"energyConsumption", "monthly",    0x3306,2, 100},
    {"energyConsumption", "annual",     0x3308,2, 100},
    {"energyConsumption", "total",      0x330A,2, 100},

    {nullptr,nullptr,0,0,0}
};

static long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void reconnectModbus() {
    if (ctx) {
        modbus_close(ctx);
        modbus_free(ctx);
    }
    ctx = modbus_new_rtu(SERIAL_PORT, BAUDRATE, 'N', 8, 1);
    modbus_set_slave(ctx, MODBUS_SLAVE_ID);

    std::printf("modbustru connect\n");
    if (modbus_connect(ctx) == -1) {
        std::fprintf(stderr, "[ERROR] Modbus connect failed: %s\n",
                     modbus_strerror(errno));
        connected = false;
    } else {
        connected = true;
        nullCnt = 0;
        std::printf("Connected\n");
    }
}

int writeCoil(int addr, int val) {
    if (!connected) reconnectModbus();
    return modbus_write_bit(ctx, addr, val);
}

float calculateSOC(float I, float V, long dt_ms) {
    const float T_samp = 100.0f / 2.71828f - 6.0f;
    const float Cap   = 288000.0f;
    static float integralCurrent = 0.0f;
    tcnt_t += dt_ms;
    if (tcnt_t >= T_samp * 1000.0f) {
        time_t now = std::time(nullptr);
        auto lt = *std::localtime(&now);
        if (lt.tm_hour == 0 && lt.tm_min == 0 && lt.tm_sec < 6)
            integralCurrent = 0.0f;
        integralCurrent += I * T_samp;
        SOC += integralCurrent / Cap;
        tcnt_t -= T_samp * 1000.0f;
    }
    return SOC;
}

static size_t curlWriteCb(void *ptr, size_t size, size_t nmemb, void *up) {
    auto resp = static_cast<std::string*>(up);
    resp->append(static_cast<char*>(ptr), size*nmemb);
    return size * nmemb;
}

void sendDataToCSE(const std::string &module,
                   const std::string &name,
                   float val) {
    std::string url = CSE_BASE_URL + "/" + module + "/" + name;
    std::string body = "{\"m2m:cin\":{\"con\":\"" +
                       std::to_string(val) + "\"}}";

    std::printf("cinURL: %s, data: %.2f\n", url.c_str(), val);

    CURL *curl = curl_easy_init();
    if (!curl) return;
    std::string resp;
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Accept: application/json");
    hdrs = curl_slist_append(hdrs, "X-M2M-RI: create_cin");
    hdrs = curl_slist_append(hdrs, "X-M2M-Origin: Csolar_controller");
    hdrs = curl_slist_append(hdrs, "X-M2M-RVI: 2a");
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json;ty=4");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_perform(curl);

    if (!resp.empty()) std::puts(resp.c_str());
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
}

void monitorThreadFunc() {
    using clock = std::chrono::steady_clock;
    std::vector<std::string> modules = {"battery","energyGeneration","energyConsumption"};
    while (true) {
        auto start = clock::now();
        for (const auto &module : modules) {
            std::map<std::string, float> values;
            for (const auto &r : regs) {
                if (!r.module) break;
                if (module != r.module) continue;
                if (!connected) reconnectModbus();
                uint16_t buf[2] = {0};
                int rc = modbus_read_input_registers(ctx, r.addr, r.len, buf);
                if (rc == r.len) {
                    float v = (r.scale == 1.0f ? buf[0] : buf[0] / r.scale);
                    if (std::strcmp(r.name, "level") == 0) {
                        v = calculateSOC(values["current"], values["voltage"], MONITOR_INTERVAL_MS);
                    }
                    values[r.name] = v;
                }
            }
            std::ostringstream oss;
            oss << "{ ";
            bool first=true;
            for (auto &p : values) {
                if (!first) oss << ", "; first=false;
                oss << p.first << ": " << p.second;
            }
            oss << " }";
            std::printf("read  %s  : %s\n", module.c_str(), oss.str().c_str());

            if (values.empty()) {
                if (++nullCnt >= NULL_READ_LIMIT) {
                    std::printf("[WARN] Too many null reads, reconnecting...\n");
                    reconnectModbus();
                }
            } else {
                nullCnt = 0;
            }
            for (auto &p : values) {
                sendDataToCSE(module, p.first, p.second);
                std::this_thread::sleep_for(std::chrono::milliseconds(HTTP_DELAY_MS));
            }
        }
        auto elapsed = clock::now() - start;
        auto wait = std::chrono::milliseconds(MONITOR_INTERVAL_MS) - elapsed;
        if (wait > std::chrono::milliseconds(0)) std::this_thread::sleep_for(wait);
    }
}

class WriteHandler : public drogon::HttpController<WriteHandler> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WriteHandler::write, "/write", drogon::Post);
    METHOD_LIST_END

    void write(const drogon::HttpRequestPtr &req, std::function<void(drogon::HttpResponsePtr)> &&callback)
    {
        long tr = nowMs();
        auto j = json::parse(req->getBody(), nullptr, false);
        if (j.is_discarded()) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            callback(std::move(resp));
            return;
        }
        auto &fc = j["m2m:sgn"]["m2m:nev"]["m2m:rep"]["m2m:fcnt"];

        if (fc.contains("charging")) {
            printf("IF charging\n");
            int v = fc["charging"].get<int>();
            int rc = writeCoil(0x0000, v);
            std::printf("charging:%d\n", rc);
        }
        if (fc.contains("discharging") && fc.contains("t1")) {
            printf("IF discharging\n");
            long t1 = fc["t1"].get<long>();
            int  v  = fc["discharging"].get<int>();
            long t2 = nowMs();
            int  rc = writeCoil(0x0002, v);
            long t3 = nowMs();
            std::ofstream ofs("time_data.csv", std::ios::app);
            ofs << t1 << ',' << t2 << ',' << t3 << ',' << tr << ',' << v << '\n';
            std::printf("discharging:%d\n", rc);
        }
        auto resp = drogon::HttpResponse::newHttpResponse();
        callback(std::move(resp));
    }
};

class ReconnectHandler : public drogon::HttpController<ReconnectHandler> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ReconnectHandler::reconnect, "/reconnect", drogon::Post);
    METHOD_LIST_END

    void reconnect(const drogon::HttpRequestPtr &, std::function<void(drogon::HttpResponsePtr)> &&callback)
    {
        std::printf("reconnect slave req\n");
        reconnectModbus();
        std::printf("reconnected the slave\n");
        auto resp = drogon::HttpResponse::newHttpResponse();
        callback(std::move(resp));
    }
};

void signalHandler(int) {
    drogon::app().quit();
    if (ctx) {
        modbus_close(ctx);
        modbus_free(ctx);
    }
    std::exit(0);
}

int main() {
    std::signal(SIGINT, signalHandler);
    reconnectModbus();

    std::thread(monitorThreadFunc).detach();

    auto &app = drogon::app();
    app.addListener("0.0.0.0", HTTP_PORT);
    std::printf("Example app listening on port %d!\n", HTTP_PORT);
    app.run();
    return 0;
}
