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

#define PORT 3003
#define MAX_POST_SIZE 8192

char *json_data = NULL;

struct ConnectionInfo {
    char buffer[MAX_POST_SIZE];
    size_t offset;
};

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
    cJSON *parent = NULL;
    char *prev_token = NULL;

    while (token != NULL) {
        parent = current;
        prev_token = token;

        cJSON *child = cJSON_GetObjectItem(current, token);
        if (!child) {
            child = cJSON_CreateObject();
            cJSON_AddItemToObject(current, token, child);
        }
        current = child;
        token = strtok(NULL, "/");
    }

    if (parent && prev_token) {
        cJSON_ReplaceItemInObject(parent, prev_token, cJSON_CreateString(value));
    }

    free(path_copy);
}

void update_json_tree(const char *post_body) {
    cJSON *root_json = cJSON_Parse(post_body);
    if (!root_json) {
        fprintf(stderr, "[ERROR] Failed to parse JSON\n");
        return;
    }

    cJSON *sgn = cJSON_GetObjectItem(root_json, "m2m:sgn");
    if (!sgn) {
        fprintf(stderr, "[WARN] 'm2m:sgn' not found in message\n");
        cJSON_Delete(root_json);
        return;
    }

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
        const char *rel_path_start = strchr(sur, '/');
            if (rel_path_start) {
                rel_path_start++;
                char *rel_path = strdup(rel_path_start);
                char *last_slash = strrchr(rel_path, '/');
                if (last_slash) *last_slash = '\0';
            printf("[INFO] Received notification: con=%s | sur=%s | path=%s\n", con, sur, rel_path);

            cJSON *json_tree = json_data ? cJSON_Parse(json_data) : cJSON_CreateObject();
            insert_path_to_json(json_tree, rel_path, con);

            free(json_data);
            json_data = cJSON_PrintUnformatted(json_tree);
            cJSON_Delete(json_tree);

            // notification delay
            uint64_t recv_time = current_time_ms();
            if (ct_time > 0) {
                uint64_t delay = recv_time - ct_time;
                printf("[INFO] ct=%" PRIu64 ", recv_time=%" PRIu64 ", delay=%" PRIu64 " ms\n",
                        ct_time, recv_time, delay);
                FILE *fp = fopen("notification_delay.csv", "a");
                if (fp) {
                    fprintf(fp, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", ct_time, recv_time, delay);
                    fclose(fp);
                }
            } else {
                fprintf(stderr, "[WARN] Missing or invalid ct value\n");
            }
            //

            free(rel_path);
        }
    } else {
        fprintf(stderr, "[WARN] sur or con is missing in notification\n");
    }

    cJSON_Delete(root_json);
}

static enum MHD_Result request_handler(void *cls, struct MHD_Connection *connection,
                           const char *url, const char *method,
                           const char *version, const char *upload_data,
                           size_t *upload_data_size, void **con_cls) {
    if (strcmp(method, "POST") == 0 && strcmp(url, "/notify") == 0) {
        if (*con_cls == NULL) {
            struct ConnectionInfo *ci = calloc(1, sizeof(struct ConnectionInfo));
            *con_cls = ci;
            return MHD_YES;
        }

        struct ConnectionInfo *ci = *con_cls;

        if (*upload_data_size != 0) {
            if (ci->offset + *upload_data_size < MAX_POST_SIZE) {
                memcpy(ci->buffer + ci->offset, upload_data, *upload_data_size);
                ci->offset += *upload_data_size;
            }
            *upload_data_size = 0;
            return MHD_YES;
        }

        ci->buffer[ci->offset] = '\0';
        printf("[HTTP] POST /notify received (%zu bytes)\n", ci->offset);
        printf("[DEBUG] POST Body: %s\n", ci->buffer);
        update_json_tree(ci->buffer);

        struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "X-M2M-RSC", "2000");
        MHD_add_response_header(response, "X-M2M-RI", "vrq_response");
        MHD_add_response_header(response, "Content-Type", "application/json");
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        free(ci);
        *con_cls = NULL;
        return ret == MHD_YES ? MHD_YES : MHD_NO;
    }

//    if (strcmp(method, "GET") == 0 && strcmp(url, "/data") == 0) {
//        printf("[HTTP] GET /data requested\n");
//        const char *base_data = json_data ? json_data : "{}";
//        const char *housecnt_str = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "housecnt");
//        int housecnt = atoi(housecnt_str);
//        size_t bufsize = strlen(base_data) * housecnt + housecnt + 10;
//        char *combined = malloc(bufsize);
//        if (!combined) {
//            fprintf(stderr, "[ERROR] Memory allocation failed\n");
//            return MHD_NO;
//        }
//        combined[0] = '\0';
//        strcat(combined, "{");
//        for (int i = 0; i < housecnt; i++) {
//            strcat(combined, base_data);
//            if (i < housecnt - 1) strcat(combined, ",");
//        }
//        strcat(combined, "}");
//        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(combined),
//                                                (void *)combined, MHD_RESPMEM_MUST_FREE);
//        MHD_add_response_header(response, "Content-Type", "application/json");
//        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
//        MHD_destroy_response(response);
//        return MHD_YES;
//    }

    if (strcmp(method, "GET") == 0 && strcmp(url, "/data") == 0) {
        printf("[HTTP] GET /data requested\n");
        const char *response_str = json_data ? json_data : "{}";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(response_str),
                                                (void*)response_str, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return MHD_YES;
    }

    printf("[WARN] Unknown request: %s %s\n", method, url);
    const char *not_found = "Not Found";
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(not_found),
                                            (void*)not_found, MHD_RESPMEM_PERSISTENT);
    int ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return MHD_YES;
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
