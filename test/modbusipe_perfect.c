
// Compile with: gcc -o modbusipe modbusipe_perfect.c -lmodbus -lmicrohttpd -lcurl -lpthread
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <modbus.h>
#include <microhttpd.h>
#include <curl/curl.h>
#include "cJSON.h"

#define SERIAL_PORT "/dev/ttyXRUSB0"
#define BAUDRATE 115200
#define MODBUS_SLAVE_ID 1
#define PORT 3002
#define MONITOR_INTERVAL 6000
#define NULL_READ_LIMIT 20

modbus_t *ctx;
int connected = 0;
int null_cnt = 0;
float SOC = 0;
long tcnt_t = 0;
pthread_t monitor_thread;

typedef struct {
    const char *module;
    const char *name;
    int address;
    int type;
    int length;
    float scale;
} Register;

Register registers[] = {
    {"battery", "current", 0x331B, 3, 1, 100},
    {"battery", "voltage", 0x331A, 3, 1, 100},
    {"battery", "level", 0x311A, 3, 1, 1},
    {NULL, NULL, 0, 0, 0, 0}
};

void reconnect_modbus() {
    if (ctx) modbus_close(ctx);
    ctx = modbus_new_rtu(SERIAL_PORT, BAUDRATE, 'N', 8, 1);
    modbus_set_slave(ctx, MODBUS_SLAVE_ID);
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Modbus connect failed: %s\n", modbus_strerror(errno));
        connected = 0;
        return;
    }
    connected = 1;
    null_cnt = 0;
    printf("[INFO] Modbus connected\n");
}

int write_coil(int address, int value) {
    if (!connected) reconnect_modbus();
    return modbus_write_bit(ctx, address, value);
}

void log_discharging_data(long t1, long tr, int value) {
    long t2 = (long)(time(NULL) * 1000);
    usleep(10000);
    long t3 = (long)(time(NULL) * 1000);
    FILE *f = fopen("time_data.csv", "a");
    if (f) {
        fprintf(f, "%ld,%ld,%ld,%ld,%d\n", t1, t2, t3, tr, value);
        fclose(f);
    }
}

void send_data_to_cse(const char *module, const char *name, float value) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    char url[256];
    snprintf(url, sizeof(url), "http://192.168.0.6:3000/TinyIoT/solar_controller/%s/%s", module, name);

    char json[256];
    snprintf(json, sizeof(json), "{\"m2m:cin\":{\"con\":\"%.2f\"}}", value);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "X-M2M-RI: create_cin");
    headers = curl_slist_append(headers, "X-M2M-Origin: Csolar_controller");
    headers = curl_slist_append(headers, "Content-Type: application/json;ty=4");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
}

float calculate_soc(float I, float V, float interval_ms) {
    const float T_samp = 100 / 2.71828 - 6;
    const float BatteryCapacity = 288000;
    static float intergralCurrent = 0;

    tcnt_t += interval_ms;
    if (tcnt_t >= T_samp * 1000) {
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        if (lt->tm_hour == 0 && lt->tm_min == 0 && lt->tm_sec < 6) {
            intergralCurrent = 0;
        }
        intergralCurrent += I * T_samp;
        SOC += intergralCurrent / BatteryCapacity;
        tcnt_t -= T_samp * 1000;
    }
    return SOC;
}

void *monitor_function(void *arg) {
    uint16_t tab[2];
    float current = 0, voltage = 0;
    while (1) {
        int read_success = 0;
        for (int i = 0; registers[i].module != NULL; ++i) {
            if (!connected) reconnect_modbus();
            if (modbus_read_input_registers(ctx, registers[i].address, registers[i].length, tab) == registers[i].length) {
                float val = tab[0] / registers[i].scale;
                if (strcmp(registers[i].name, "current") == 0) current = val;
                if (strcmp(registers[i].name, "voltage") == 0) voltage = val;
                if (strcmp(registers[i].name, "level") == 0) val = calculate_soc(current, voltage, MONITOR_INTERVAL);
                send_data_to_cse(registers[i].module, registers[i].name, val);
                read_success = 1;
            }
        }

        if (!read_success) {
            null_cnt++;
            if (null_cnt >= NULL_READ_LIMIT) {
                printf("[WARN] Too many null reads, reconnecting...\n");
                reconnect_modbus();
            }
        } else {
            null_cnt = 0;
        }

        usleep(MONITOR_INTERVAL * 1000);
    }
    return NULL;
}

int handle_request(void *cls, struct MHD_Connection *connection,
                   const char *url, const char *method,
                   const char *version, const char *upload_data,
                   size_t *upload_data_size, void **con_cls) {
    static int post_received = 0;
    static char *buffer = NULL;

    if (strcmp(method, "POST") != 0) return MHD_NO;

    if (*upload_data_size > 0) {
        buffer = realloc(buffer, *upload_data_size + 1);
        memcpy(buffer, upload_data, *upload_data_size);
        buffer[*upload_data_size] = '\0';

        if (strcmp(url, "/write") == 0) {
            cJSON *root = cJSON_Parse(buffer);
            cJSON *sgn = cJSON_GetObjectItem(root, "m2m:sgn");
            cJSON *nev = cJSON_GetObjectItem(sgn, "m2m:nev");
            cJSON *rep = cJSON_GetObjectItem(nev, "m2m:rep");
            cJSON *fcnt = cJSON_GetObjectItem(rep, "m2m:fcnt");

            cJSON *charging = cJSON_GetObjectItem(fcnt, "charging");
            cJSON *discharging = cJSON_GetObjectItem(fcnt, "discharging");
            cJSON *t1json = cJSON_GetObjectItem(fcnt, "t1");

            if (charging) write_coil(0x0000, charging->valueint);
            if (discharging && t1json) {
                write_coil(0x0002, discharging->valueint);
                long tr = (long)(time(NULL) * 1000);
                log_discharging_data(t1json->valuedouble, tr, discharging->valueint);
            }
            cJSON_Delete(root);
        } else if (strcmp(url, "/reconnect") == 0) {
            reconnect_modbus();
        }

        *upload_data_size = 0;
        post_received = 1;
        return MHD_YES;
    } else if (post_received) {
        struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        free(buffer);
        buffer = NULL;
        post_received = 0;
        return ret;
    }

    return MHD_YES;
}

int main() {
    reconnect_modbus();
    pthread_create(&monitor_thread, NULL, monitor_function, NULL);
    struct MHD_Daemon *daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL, &handle_request, NULL, MHD_OPTION_END);
    if (!daemon) {
        fprintf(stderr, "HTTP server failed to start\n");
        return 1;
    }
    printf("[INFO] Server running on port %d...\n", PORT);
    getchar();
    MHD_stop_daemon(daemon);
    modbus_close(ctx);
    modbus_free(ctx);
    return 0;
}
