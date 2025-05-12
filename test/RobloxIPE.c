// Compile: gcc -o ipe_tree ipe_tree.c -lmicrohttpd -lcjson
// sudo apt update
// sudo apt install libmicrohttpd-dev libcjson-dev build-essential

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <cjson/cJSON.h>

// notification delay
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
//

#define PORT 3002
#define MAX_POST_SIZE 8192

char *json_data = NULL;

// notification delay
uint64_t current_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)(ts.tv_sec) * 1000 + (ts.tv_nsec / 1000000);
}
//

void insert_path_to_json(cJSON *root, const char *path, const char *value) {
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    cJSON *current = root;

    while (token != NULL) {
        cJSON *child = cJSON_GetObjectItem(current, token);
        if (!child) {
            child = cJSON_CreateObject();
            cJSON_AddItemToObject(current, token, child);
        }
        current = child;
        token = strtok(NULL, "/");
    }
    cJSON_ReplaceItemInParent(current, cJSON_CreateString(value));
    free(path_copy);
}

void update_json_tree(const char *post_body) {
    cJSON *root_json = cJSON_Parse(post_body);
    if (!root_json) return;

    cJSON *sgn = cJSON_GetObjectItem(root_json, "m2m:sgn");
    if (!sgn) { cJSON_Delete(root_json); return; }

    const char *sur = cJSON_GetStringValue(cJSON_GetObjectItem(sgn, "sur"));
    cJSON *rep = cJSON_GetObjectItem(cJSON_GetObjectItem(sgn, "nev"), "rep");
    const char *con = NULL;
    // notification delay
    uint64_t ct_time = 0;
    //

    if (rep) {
        cJSON *cin = cJSON_GetObjectItem(rep, "m2m:cin");
        if (cin) {
            con = cJSON_GetStringValue(cJSON_GetObjectItem(cin, "con"));
            // notification delay
            cJSON *ct_item = cJSON_GetObjectItem(cin, "ct");
            if (cJSON_IsString(ct_item)) {
                sscanf(ct_item->valuestring, "%" SCNu64, &ct_time);
            }
            //
        }
    }

    if (sur && con) {
        const char *rel_start = strchr(sur, '/');
        if (rel_start) rel_start = strchr(rel_start + 1, '/');
        if (rel_start) {
            char *rel_path = strdup(rel_start + 1);
            char *last_slash = strrchr(rel_path, '/');
            if (last_slash) *last_slash = '\0';

            cJSON *json_tree = json_data ? cJSON_Parse(json_data) : cJSON_CreateObject();
            insert_path_to_json(json_tree, rel_path, con);

            free(json_data);
            json_data = cJSON_PrintUnformatted(json_tree);
            cJSON_Delete(json_tree);

            // notification delay
            uint64_t recv_time = current_time_ms();
            if (ct_time > 0) {
                uint64_t delay = recv_time - ct_time;
                FILE *fp = fopen("notification_delay.csv", "a");
                if (fp) {
                    fprintf(fp, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", ct_time, recv_time, delay);
                    fclose(fp);
                }
            }
            //

            free(rel_path);
        }
    }

    cJSON_Delete(root_json);
}

static int request_handler(void *cls, struct MHD_Connection *connection,
                           const char *url, const char *method,
                           const char *version, const char *upload_data,
                           size_t *upload_data_size, void **con_cls) {
    static char post_data[MAX_POST_SIZE];
    static size_t data_offset = 0;

    if (strcmp(method, "POST") == 0 && strcmp(url, "/notify") == 0) {
        if (*upload_data_size > 0) {
            if (data_offset + *upload_data_size < MAX_POST_SIZE) {
                memcpy(post_data + data_offset, upload_data, *upload_data_size);
                data_offset += *upload_data_size;
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            post_data[data_offset] = '\0';
            update_json_tree(post_data);
            data_offset = 0;

            struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(response, "X-M2M-RSC", "2000");
            MHD_add_response_header(response, "X-M2M-RI", "vrq_response");
            MHD_add_response_header(response, "Content-Type", "application/json");
            int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);
            return ret;
        }
    }

    if (strcmp(method, "GET") == 0 && strcmp(url, "/data") == 0) {
        const char *response_str = json_data ? json_data : "{}";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(response_str),
                                                (void*)response_str, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    const char *not_found = "Not Found";
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(not_found),
                                            (void*)not_found, MHD_RESPMEM_PERSISTENT);
    int ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

int main() {
    struct MHD_Daemon *daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
                                                  &request_handler, NULL, MHD_OPTION_END);
    if (!daemon) return 1;

    printf("IPE Tree Server running at port %d...\n", PORT);
    getchar();
    MHD_stop_daemon(daemon);
    free(json_data);
    return 0;
}
