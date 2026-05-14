/*
 * DeepSeek API – Unofficial C client (command-line)
 * Copyright (C) 2025 Sanne Karibo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program was created as an educational task and is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <wasmtime.h>

/* ---------- base64 encoder (strips '=' like Python's .strip('=')) ---------- */
static char *base64_encode(const unsigned char *data, size_t len) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = malloc(out_len + 1);
    if (!out) return NULL;
    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t a = i < len ? data[i++] : 0;
        uint32_t b = i < len ? data[i++] : 0;
        uint32_t c = i < len ? data[i++] : 0;
        uint32_t triple = (a << 16) + (b << 8) + c;
        out[j++] = b64[(triple >> 18) & 0x3F];
        out[j++] = b64[(triple >> 12) & 0x3F];
        out[j++] = b64[(triple >>  6) & 0x3F];
        out[j++] = b64[triple & 0x3F];
    }
    /* Remove padding '=' characters */
    while (j > 0 && out[j-1] == '=') j--;
    out[j] = '\0';
    return out;
}

/* ---------- dynamic string builder ---------- */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} DynStr;

static void ds_init(DynStr *ds) {
    ds->data = NULL;
    ds->len = 0;
    ds->cap = 0;
}

static void ds_append(DynStr *ds, const char *s, size_t slen) {
    if (!slen) return;
    if (ds->len + slen + 1 > ds->cap) {
        ds->cap = ds->cap ? ds->cap * 2 : 64;
        while (ds->len + slen + 1 > ds->cap) ds->cap *= 2;
        ds->data = realloc(ds->data, ds->cap);
    }
    memcpy(ds->data + ds->len, s, slen);
    ds->len += slen;
    ds->data[ds->len] = '\0';
}

static void ds_append_str(DynStr *ds, const char *s) {
    ds_append(ds, s, strlen(s));
}

static void ds_free(DynStr *ds) {
    free(ds->data);
    ds_init(ds);
}

/* ---------- deepseek chat structure ---------- */
typedef struct {
    CURL *curl;
    char *authorization;
    char *chat_session_id;
    char *parent_message_id;
    char *wasm_path;             /* path to wasm.wasm */
    int debug;                   /* if set, print HTTP bodies */
} DeepSeekChat;

/* ---------- WASM solver ---------- */
typedef struct {
    int success;                 /* 1 = solved, 0 = failed */
    int answer;                  /* numeric answer */
    char *pow_response;          /* base64 JSON string (malloc'd) */
} PowResult;

static PowResult solve_wasm(const char *algorithm, const char *challenge,
                            const char *salt, long expire_at, double difficulty,
                            const char *signature, const char *target_path,
                            const char *wasm_path) {
    PowResult result = {0, 0, NULL};

    /* set up wasmtime */
    wasm_engine_t *engine = wasm_engine_new();
    wasm_store_t *store = wasm_store_new(engine);
    wasmtime_module_t *module = NULL;
    wasmtime_linker_t *linker = wasmtime_linker_new(engine);
    wasmtime_instance_t instance;
    FILE *f = fopen(wasm_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open WASM file: %s\n", wasm_path);
        goto cleanup;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *wasm_bytes = malloc(fsize);
    fread(wasm_bytes, 1, fsize, f);
    fclose(f);

    wasmtime_error_t *err = wasmtime_module_new(engine, wasm_bytes, fsize, &module);
    free(wasm_bytes);
    if (err) {
        fprintf(stderr, "Failed to compile WASM module\n");
        wasmtime_error_delete(err);
        goto cleanup;
    }

    err = wasmtime_linker_instantiate(linker, store, module, &instance, NULL);
    if (err) {
        fprintf(stderr, "Failed to instantiate WASM\n");
        wasmtime_error_delete(err);
        goto cleanup;
    }

    /* get exports */
    wasmtime_extern_t alloc_extern, stack_ptr_extern, solve_extern, mem_extern;
    bool ok = wasmtime_instance_export_get(store, &instance, "__wbindgen_export_0", 19, &alloc_extern) &&
              wasmtime_instance_export_get(store, &instance, "__wbindgen_add_to_stack_pointer", 31, &stack_ptr_extern) &&
              wasmtime_instance_export_get(store, &instance, "wasm_solve", 10, &solve_extern) &&
              wasmtime_instance_export_get(store, &instance, "memory", 6, &mem_extern);
    if (!ok) {
        fprintf(stderr, "Missing WASM exports\n");
        goto cleanup;
    }

    wasmtime_func_t alloc_func = alloc_extern.of.func;
    wasmtime_func_t stack_ptr_func = stack_ptr_extern.of.func;
    wasmtime_func_t solve_func = solve_extern.of.func;
    wasmtime_memory_t memory = mem_extern.of.memory;

    /* prepare strings */
    char n_str[256];
    snprintf(n_str, sizeof(n_str), "%s_%ld_", salt, expire_at);
    const uint8_t *challenge_data = (const uint8_t *)challenge;
    size_t challenge_len = strlen(challenge);
    const uint8_t *n_data = (const uint8_t *)n_str;
    size_t n_len = strlen(n_str);

    /* call alloc */
    wasmtime_val_t alloc_args[2] = {
        {.kind = WASMTIME_I32, .of.i32 = (int32_t)challenge_len},
        {.kind = WASMTIME_I32, .of.i32 = 1}
    };
    wasmtime_val_t alloc_result[1];
    wasmtime_func_call(store, alloc_func, alloc_args, 2, alloc_result, 1, NULL);
    int32_t challenge_ptr = alloc_result[0].of.i32;

    alloc_args[0].of.i32 = (int32_t)n_len;
    wasmtime_func_call(store, alloc_func, alloc_args, 2, alloc_result, 1, NULL);
    int32_t n_ptr = alloc_result[0].of.i32;

    /* write memory */
    uint8_t *mem_data = wasmtime_memory_data(store, &memory);
    memcpy(mem_data + challenge_ptr, challenge_data, challenge_len);
    memcpy(mem_data + n_ptr, n_data, n_len);

    /* adjust stack pointer */
    wasmtime_val_t sp_args[1] = { {.kind = WASMTIME_I32, .of.i32 = -16} };
    wasmtime_val_t sp_result[1];
    wasmtime_func_call(store, stack_ptr_func, sp_args, 1, sp_result, 1, NULL);
    int32_t stack_ptr = sp_result[0].of.i32;

    /* call wasm_solve */
    wasmtime_val_t solve_args[6] = {
        {.kind = WASMTIME_I32, .of.i32 = stack_ptr},
        {.kind = WASMTIME_I32, .of.i32 = challenge_ptr},
        {.kind = WASMTIME_I32, .of.i32 = (int32_t)challenge_len},
        {.kind = WASMTIME_I32, .of.i32 = n_ptr},
        {.kind = WASMTIME_I32, .of.i32 = (int32_t)n_len},
        {.kind = WASMTIME_F64, .of.f64 = difficulty}
    };
    wasmtime_val_t solve_results[0];
    wasmtime_func_call(store, solve_func, solve_args, 6, solve_results, 0, NULL);

    /* read results from stack */
    int32_t status = *(int32_t *)(mem_data + stack_ptr);
    double answer_f64 = *(double *)(mem_data + stack_ptr + 8);

    if (status == 0) {
        answer_f64 = 0.0;  /* None in Python */
    }

    /* restore stack */
    sp_args[0].of.i32 = 16;
    wasmtime_func_call(store, stack_ptr_func, sp_args, 1, sp_result, 1, NULL);

    if (status != 0) {
        int answer_int = (int)answer_f64;
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "algorithm", algorithm);
        cJSON_AddStringToObject(json, "challenge", challenge);
        cJSON_AddStringToObject(json, "salt", salt);
        cJSON_AddNumberToObject(json, "answer", answer_int);
        cJSON_AddStringToObject(json, "signature", signature);
        cJSON_AddStringToObject(json, "target_path", target_path);
        char *json_str = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);

        char *b64 = base64_encode((unsigned char *)json_str, strlen(json_str));
        free(json_str);
        result.success = 1;
        result.answer = answer_int;
        result.pow_response = b64;  /* caller must free */
    }

cleanup:
    if (module) wasmtime_module_delete(module);
    wasmtime_linker_delete(linker);
    wasm_store_delete(store);
    wasm_engine_delete(engine);
    return result;
}

/* ---------- HTTP helper: memory callback ---------- */
struct mem_chunk {
    char *data;
    size_t size;
};

static size_t write_mem_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct mem_chunk *mem = (struct mem_chunk *)userp;
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

/* ---------- API methods ---------- */
static int create_chat_session(DeepSeekChat *chat) {
    char url[512];
    snprintf(url, sizeof(url), "https://chat.deepseek.com/api/v0/chat_session/create");

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "accept: */*");
    headers = curl_slist_append(headers, "content-type: application/json");
    char auth[256];
    snprintf(auth, sizeof(auth), "authorization: %s", chat->authorization);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "x-app-version: 20241129.1");
    headers = curl_slist_append(headers, "x-client-locale: zh_CN");
    headers = curl_slist_append(headers, "x-client-platform: web");
    headers = curl_slist_append(headers, "x-client-version: 1.5.0");
    headers = curl_slist_append(headers, "x-debug-lite-model-channel: prod");
    headers = curl_slist_append(headers, "x-debug-model-channel: prod");
    headers = curl_slist_append(headers, "user-agent: Mozilla/5.0 (X11; Linux x86_64; rv:109.0) Gecko/20100101 Firefox/142.0");

    curl_easy_setopt(chat->curl, CURLOPT_URL, url);
    curl_easy_setopt(chat->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(chat->curl, CURLOPT_POSTFIELDS, "{}");
    curl_easy_setopt(chat->curl, CURLOPT_TIMEOUT, 30L);

    struct mem_chunk response = {0};
    curl_easy_setopt(chat->curl, CURLOPT_WRITEFUNCTION, write_mem_callback);
    curl_easy_setopt(chat->curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(chat->curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) { free(response.data); return 0; }

    long http_code = 0;
    curl_easy_getinfo(chat->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) { free(response.data); return 0; }

    cJSON *json = cJSON_Parse(response.data);
    free(response.data);
    if (!json) return 0;

    int ok = 0;
    cJSON *code = cJSON_GetObjectItem(json, "code");
    if (code && code->valueint == 0) {
        cJSON *data = cJSON_GetObjectItem(json, "data");
        if (data) {
            cJSON *biz = cJSON_GetObjectItem(data, "biz_data");
            if (biz) {
                cJSON *id = cJSON_GetObjectItem(biz, "id");
                if (id && id->valuestring) {
                    free(chat->chat_session_id);
                    chat->chat_session_id = strdup(id->valuestring);
                    ok = 1;
                }
            }
        }
    }
    cJSON_Delete(json);
    return ok;
}

static cJSON *create_pow_challenge(DeepSeekChat *chat) {
    char url[512];
    snprintf(url, sizeof(url), "https://chat.deepseek.com/api/v0/chat/create_pow_challenge");

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "accept: */*");
    headers = curl_slist_append(headers, "content-type: application/json");
    char auth[256];
    snprintf(auth, sizeof(auth), "authorization: %s", chat->authorization);
    headers = curl_slist_append(headers, auth);
    if (chat->chat_session_id) {
        char ref[512];
        snprintf(ref, sizeof(ref), "referrer: https://chat.deepseek.com/a/chat/s/%s", chat->chat_session_id);
        headers = curl_slist_append(headers, ref);
    }
    headers = curl_slist_append(headers, "x-app-version: 20241129.1");
    headers = curl_slist_append(headers, "x-client-locale: zh_CN");
    headers = curl_slist_append(headers, "x-client-platform: web");
    headers = curl_slist_append(headers, "x-client-version: 1.5.0");
    headers = curl_slist_append(headers, "x-debug-lite-model-channel: prod");
    headers = curl_slist_append(headers, "x-debug-model-channel: prod");
    headers = curl_slist_append(headers, "user-agent: Mozilla/5.0 (X11; Linux x86_64; rv:109.0) Gecko/20100101 Firefox/142.0");

    curl_easy_setopt(chat->curl, CURLOPT_URL, url);
    curl_easy_setopt(chat->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(chat->curl, CURLOPT_POSTFIELDS, "{\"target_path\":\"/api/v0/chat/completion\"}");
    curl_easy_setopt(chat->curl, CURLOPT_TIMEOUT, 30L);

    struct mem_chunk response = {0};
    curl_easy_setopt(chat->curl, CURLOPT_WRITEFUNCTION, write_mem_callback);
    curl_easy_setopt(chat->curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(chat->curl);
    curl_slist_free_all(headers);
    if (res != CURLE_OK) { free(response.data); return NULL; }

    long http_code = 0;
    curl_easy_getinfo(chat->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) { free(response.data); return NULL; }

    cJSON *json = cJSON_Parse(response.data);
    free(response.data);
    if (!json) return NULL;

    cJSON *challenge = NULL;
    cJSON *code = cJSON_GetObjectItem(json, "code");
    if (code && code->valueint == 0) {
        cJSON *data = cJSON_GetObjectItem(json, "data");
        if (data) {
            cJSON *biz = cJSON_GetObjectItem(data, "biz_data");
            if (biz) {
                challenge = cJSON_GetObjectItem(biz, "challenge");
                if (challenge) challenge = cJSON_Duplicate(challenge, 1);
            }
        }
    }
    cJSON_Delete(json);
    return challenge;
}

static char *generate_client_stream_id(void) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char date[16];
    strftime(date, sizeof(date), "%Y%m%d", &tm);
    char hex[17];
    for (int i = 0; i < 16; i++) hex[i] = "0123456789abcdef"[rand() % 16];
    hex[16] = '\0';
    char *id = malloc(32);
    snprintf(id, 32, "%s-%s", date, hex);
    return id;
}

/* ---------- SSE stream parser state ---------- */
typedef enum {
    EVT_UNKNOWN,
    EVT_READY,
    EVT_UPDATE_SESSION,
    EVT_TITLE,
    EVT_FINISH,
    EVT_CLOSE
} SSEEvent;

typedef struct {
    SSEEvent event;
    char generate_mode[16];          /* "THINK", "RESPONSE", "SEARCH", "TIP" or "" */
    DynStr think;
    DynStr respond;
    cJSON *citation;                 /* maps cite_index -> {url, title, ...} */
    char *msgid;
    char *parid;
    char *reqid;
    char *resid;
    int tokencount;
    char *title;
    double thinktime;
    int printing;                    /* whether to output to stdout */
} StreamCtx;

static void parse_output(StreamCtx *ctx, cJSON *data, const char *raw_line);

static void send_to_sd(StreamCtx *ctx, const char *text) {
    if (ctx->printing) fputs(text, stdout);
}

static void parse_output(StreamCtx *ctx, cJSON *data, const char *raw_line) {
    if (!data) return;
    if (cJSON_IsBool(data)) return;

    if (cJSON_IsString(data)) {
        const char *s = data->valuestring;
        if (!strcmp(ctx->generate_mode, "THINK")) ds_append_str(&ctx->think, s);
        else if (!strcmp(ctx->generate_mode, "RESPONSE")) ds_append_str(&ctx->respond, s);
        else if (!strcmp(ctx->generate_mode, "TIP")) { /* ignore */ }
        else {
            fprintf(stderr, "Unexpected string in mode %s\nData: %s\n", ctx->generate_mode, raw_line);
        }
        send_to_sd(ctx, s);
        return;
    }

    if (cJSON_IsArray(data)) {
        cJSON *item;
        cJSON_ArrayForEach(item, data) parse_output(ctx, item, raw_line);
        return;
    }

    if (cJSON_IsObject(data)) {
        /* citation object during SEARCH */
        if (!strcmp(ctx->generate_mode, "SEARCH") && cJSON_HasObjectItem(data, "url")) {
            int idx = cJSON_GetObjectItem(data, "cite_index") ?
                      cJSON_GetObjectItem(data, "cite_index")->valueint : 0;
            char key[16];
            snprintf(key, sizeof(key), "%d", idx);
            cJSON_AddItemToObject(ctx->citation, key, cJSON_Duplicate(data, 1));
            if (ctx->printing) {
                char buf[1024];
                const char *title = cJSON_GetObjectItem(data, "title") ?
                                    cJSON_GetObjectItem(data, "title")->valuestring :
                                    cJSON_GetObjectItem(data, "site_name") ?
                                    cJSON_GetObjectItem(data, "site_name")->valuestring : "UNKNOWN";
                const char *site = cJSON_GetObjectItem(data, "site_name") ?
                                   cJSON_GetObjectItem(data, "site_name")->valuestring : "UNKNOWN";
                const char *url = cJSON_GetObjectItem(data, "url")->valuestring;
                const char *snippet = cJSON_GetObjectItem(data, "snippet") ?
                                      cJSON_GetObjectItem(data, "snippet")->valuestring : "";
                snprintf(buf, sizeof(buf), "%d. [%s - %s](%s)\n> %s\n", idx, title, site, url, snippet);
                send_to_sd(ctx, buf);
            }
            return;
        }

        /* single-key dict with 'v' */
        if (cJSON_HasObjectItem(data, "v") && cJSON_GetObjectItem(data, "v") && data->child->next == NULL) {
            parse_output(ctx, cJSON_GetObjectItem(data, "v"), raw_line);
            return;
        }
        /* single-key dict with 'response' */
        if (cJSON_HasObjectItem(data, "response") && data->child->next == NULL) {
            parse_output(ctx, cJSON_GetObjectItem(data, "response"), raw_line);
            return;
        }
        /* message_id / parent_id */
        if (cJSON_HasObjectItem(data, "message_id")) {
            free(ctx->msgid);
            ctx->msgid = strdup(cJSON_GetObjectItem(data, "message_id")->valuestring);
        }
        if (cJSON_HasObjectItem(data, "parent_id")) {
            free(ctx->parid);
            ctx->parid = strdup(cJSON_GetObjectItem(data, "parent_id")->valuestring);
        }
        /* updated_at (ignored) */
        if (cJSON_HasObjectItem(data, "updated_at") && data->child->next == NULL) return;

        /* 'p' key with path */
        if (cJSON_HasObjectItem(data, "p")) {
            const char *p = cJSON_GetObjectItem(data, "p")->valuestring;
            const char *tp = strrchr(p, '/');
            tp = tp ? tp + 1 : p;
            if (!strcmp(tp, "response") || !strcmp(tp, "content") ||
                !strcmp(tp, "fragment") || !strcmp(tp, "fragments")) {
                parse_output(ctx, cJSON_GetObjectItem(data, "v"), raw_line);
            } else if (!strcmp(tp, "status")) {
                if (ctx->printing) {
                    send_to_sd(ctx, "\n\n-----\n");
                    send_to_sd(ctx, cJSON_GetObjectItem(data, "v")->valuestring);
                }
            } else if (!strcmp(tp, "accumulated_token_usage")) {
                ctx->tokencount = cJSON_GetObjectItem(data, "v")->valueint;
            } else if (!strcmp(tp, "elapsed_secs")) {
                ctx->thinktime = cJSON_GetObjectItem(data, "v")->valuedouble;
            } else if (!strcmp(tp, "results") && !strcmp(ctx->generate_mode, "SEARCH")) {
                parse_output(ctx, cJSON_GetObjectItem(data, "v"), raw_line);
            } else if (!strcmp(tp, "has_pending_fragment") || !strcmp(tp, "conversation_mode") ||
                       !strcmp(tp, "quasi_status")) {
                /* ignore */
            } else {
                fprintf(stderr, "Unknown 'p' field: %s\nData: %s\n", tp, raw_line);
            }
            return;
        }

        /* 'type' key for mode changes */
        if (cJSON_HasObjectItem(data, "type") && cJSON_GetObjectItem(data, "type")->valuestring) {
            const char *type = cJSON_GetObjectItem(data, "type")->valuestring;
            if (strcmp(ctx->generate_mode, type) != 0) {
                strncpy(ctx->generate_mode, type, sizeof(ctx->generate_mode)-1);
                if (ctx->printing) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "\n\n-----\nSTART %s\n", type);
                    send_to_sd(ctx, buf);
                }
            }
            if (cJSON_HasObjectItem(data, "content"))
                parse_output(ctx, cJSON_GetObjectItem(data, "content"), raw_line);
            return;
        }

        fprintf(stderr, "Cannot parse dict: %s\nData: %s\n", cJSON_PrintUnformatted(data), raw_line);
        return;
    }

    fprintf(stderr, "Unrecognizable type\nData: %s\n", raw_line);
}

/* Write callback for SSE streaming */
struct send_msg_ctx {
    StreamCtx sse;
    char linebuf[8192];
    size_t linebuf_pos;
    DeepSeekChat *chat;
    int *ok;              /* output flag */
    cJSON **result;       /* final JSON object */
};

static size_t sse_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct send_msg_ctx *sc = userdata;
    size_t total = size * nmemb;
    const char *data = ptr;

    for (size_t i = 0; i < total; i++) {
        if (data[i] == '\n') {
            sc->linebuf[sc->linebuf_pos] = '\0';
            char *line = sc->linebuf;

            if (strncmp(line, "data: ", 6) == 0) {
                cJSON *json = cJSON_Parse(line + 6);
                if (json) {
                    switch (sc->sse.event) {
                        case EVT_UPDATE_SESSION:
                            parse_output(&sc->sse, json, line);
                            break;
                        case EVT_FINISH:
                        case EVT_CLOSE:
                            break;
                        case EVT_TITLE: {
                            cJSON *content = cJSON_GetObjectItem(json, "content");
                            if (content) {
                                free(sc->sse.title);
                                sc->sse.title = strdup(content->valuestring);
                            }
                            break;
                        }
                        case EVT_READY: {
                            cJSON *req = cJSON_GetObjectItem(json, "request_message_id");
                            if (req) {
                                free(sc->sse.reqid);
                                sc->sse.reqid = strdup(req->valuestring);
                            }
                            cJSON *res = cJSON_GetObjectItem(json, "response_message_id");
                            if (res) {
                                free(sc->sse.resid);
                                sc->sse.resid = strdup(res->valuestring);
                            }
                            break;
                        }
                        default:
                            fprintf(stderr, "Unknown event: %d\nData: %s\n", sc->sse.event, line);
                    }
                    cJSON_Delete(json);
                }
            } else if (strncmp(line, "event: ", 7) == 0) {
                if (strcmp(line+7, "update_session") == 0) sc->sse.event = EVT_UPDATE_SESSION;
                else if (strcmp(line+7, "finish") == 0) sc->sse.event = EVT_FINISH;
                else if (strcmp(line+7, "close") == 0) sc->sse.event = EVT_CLOSE;
                else if (strcmp(line+7, "title") == 0) sc->sse.event = EVT_TITLE;
                else if (strcmp(line+7, "ready") == 0) sc->sse.event = EVT_READY;
                else sc->sse.event = EVT_UNKNOWN;
            } else {
                fprintf(stderr, "Unrecognizable line: %s\n", line);
            }

            sc->linebuf_pos = 0;
        } else if (data[i] != '\r') {
            if (sc->linebuf_pos < sizeof(sc->linebuf)-1)
                sc->linebuf[sc->linebuf_pos++] = data[i];
        }
    }
    return total;
}

/* ---------- main send_message ---------- */
cJSON *send_message(DeepSeekChat *chat, const char *message,
                    int printing, int thinking_enabled, int search_enabled) {
    if (!chat->chat_session_id)
        if (!create_chat_session(chat)) return NULL;

    cJSON *challenge_data = create_pow_challenge(chat);
    if (!challenge_data) return NULL;

    const char *algorithm = cJSON_GetObjectItem(challenge_data, "algorithm")->valuestring;
    const char *challenge = cJSON_GetObjectItem(challenge_data, "challenge")->valuestring;
    const char *salt = cJSON_GetObjectItem(challenge_data, "salt")->valuestring;
    long expire_at = (long)cJSON_GetObjectItem(challenge_data, "expire_at")->valuedouble;
    double difficulty = cJSON_GetObjectItem(challenge_data, "difficulty")->valuedouble;
    const char *signature = cJSON_GetObjectItem(challenge_data, "signature")->valuestring;
    const char *target_path = cJSON_GetObjectItem(challenge_data, "target_path")->valuestring;

    PowResult pow = solve_wasm(algorithm, challenge, salt, expire_at, difficulty,
                               signature, target_path, chat->wasm_path);
    cJSON_Delete(challenge_data);
    if (!pow.success) return NULL;

    /* build headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "accept: text/event-stream");
    headers = curl_slist_append(headers, "content-type: application/json");
    char auth[256];
    snprintf(auth, sizeof(auth), "authorization: %s", chat->authorization);
    headers = curl_slist_append(headers, auth);
    char pow_hdr[512];
    snprintf(pow_hdr, sizeof(pow_hdr), "x-ds-pow-response: %s", pow.pow_response);
    headers = curl_slist_append(headers, pow_hdr);
    if (chat->chat_session_id) {
        char ref[512];
        snprintf(ref, sizeof(ref), "referrer: https://chat.deepseek.com/a/chat/s/%s", chat->chat_session_id);
        headers = curl_slist_append(headers, ref);
    }
    headers = curl_slist_append(headers, "x-app-version: 20241129.1");
    headers = curl_slist_append(headers, "x-client-locale: zh_CN");
    headers = curl_slist_append(headers, "x-client-platform: web");
    headers = curl_slist_append(headers, "x-client-version: 1.5.0");
    headers = curl_slist_append(headers, "x-debug-lite-model-channel: prod");
    headers = curl_slist_append(headers, "x-debug-model-channel: prod");
    headers = curl_slist_append(headers, "user-agent: Mozilla/5.0 (X11; Linux x86_64; rv:109.0) Gecko/20100101 Firefox/142.0");

    /* build JSON body */
    char *stream_id = generate_client_stream_id();
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_session_id", chat->chat_session_id);
    cJSON_AddStringToObject(body, "parent_message_id", chat->parent_message_id ? chat->parent_message_id : "");
    cJSON_AddStringToObject(body, "prompt", message);
    cJSON_AddItemToObject(body, "ref_file_ids", cJSON_CreateArray());
    cJSON_AddBoolToObject(body, "thinking_enabled", thinking_enabled);
    cJSON_AddBoolToObject(body, "search_enabled", search_enabled);
    cJSON_AddStringToObject(body, "client_stream_id", stream_id);
    cJSON_AddBoolToObject(body, "stream", 1);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    free(stream_id);

    curl_easy_setopt(chat->curl, CURLOPT_URL, "https://chat.deepseek.com/api/v0/chat/completion");
    curl_easy_setopt(chat->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(chat->curl, CURLOPT_POSTFIELDS, body_str);
    curl_easy_setopt(chat->curl, CURLOPT_TIMEOUT, 60L);

    /* SSE context */
    struct send_msg_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.sse.printing = printing;
    ctx.sse.citation = cJSON_CreateObject();
    ctx.sse.generate_mode[0] = '\0';
    ctx.chat = chat;
    ctx.linebuf_pos = 0;

    curl_easy_setopt(chat->curl, CURLOPT_WRITEFUNCTION, sse_write_cb);
    curl_easy_setopt(chat->curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(chat->curl);
    curl_slist_free_all(headers);
    free(body_str);
    free(pow.pow_response);

    if (res != CURLE_OK) {
        cJSON_Delete(ctx.sse.citation);
        ds_free(&ctx.sse.think);
        ds_free(&ctx.sse.respond);
        free(ctx.sse.msgid);
        free(ctx.sse.title);
        return NULL;
    }

    long http_code = 0;
    curl_easy_getinfo(chat->curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        cJSON_Delete(ctx.sse.citation);
        ds_free(&ctx.sse.think);
        ds_free(&ctx.sse.respond);
        free(ctx.sse.msgid);
        free(ctx.sse.title);
        return NULL;
    }

    if (ctx.sse.msgid) {
        free(chat->parent_message_id);
        chat->parent_message_id = strdup(ctx.sse.msgid);
    }

    /* build result JSON */
    cJSON *ret = cJSON_CreateObject();
    if (thinking_enabled) {
        cJSON_AddNumberToObject(ret, "thinktime", ctx.sse.thinktime);
        cJSON_AddStringToObject(ret, "thought", ctx.sse.think.data ? ctx.sse.think.data : "");
    }
    if (search_enabled) {
        cJSON_AddItemToObject(ret, "citation", ctx.sse.citation);
    } else {
        cJSON_Delete(ctx.sse.citation);
    }
    cJSON_AddBoolToObject(ret, "thinking_enabled", thinking_enabled);
    cJSON_AddBoolToObject(ret, "search_enabled", search_enabled);
    cJSON_AddStringToObject(ret, "response", ctx.sse.respond.data ? ctx.sse.respond.data : "");
    if (ctx.sse.title && strlen(ctx.sse.title))
        cJSON_AddStringToObject(ret, "title", ctx.sse.title);

    ds_free(&ctx.sse.think);
    ds_free(&ctx.sse.respond);
    free(ctx.sse.msgid);
    free(ctx.sse.title);

    return ret;
}

/* ---------- init / cleanup ---------- */
DeepSeekChat *deepseek_chat_new(const char *ds_session_id, const char *authorization,
                                const char *wasm_path) {
    DeepSeekChat *chat = calloc(1, sizeof(DeepSeekChat));
    curl_global_init(CURL_GLOBAL_ALL);
    chat->curl = curl_easy_init();
    chat->authorization = strdup(authorization);
    chat->wasm_path = strdup(wasm_path ? wasm_path : "wasm.wasm");

    /* set cookie */
    char cookie[512];
    snprintf(cookie, sizeof(cookie), "ds_session_id=%s", ds_session_id);
    curl_easy_setopt(chat->curl, CURLOPT_COOKIE, cookie);

    return chat;
}

void deepseek_chat_free(DeepSeekChat *chat) {
    if (!chat) return;
    curl_easy_cleanup(chat->curl);
    free(chat->authorization);
    free(chat->chat_session_id);
    free(chat->parent_message_id);
    free(chat->wasm_path);
    free(chat);
}

/* ============================================================
 *  CLI main
 * ============================================================ */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s init <ds_session> <ds_token>\n", argv[0]);
        fprintf(stderr, "       %s <message>\n", argv[0]);
        return 1;
    }

    /* --- init command --- */
    if (strcmp(argv[1], "init") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s init <ds_session> <ds_token>\n", argv[0]);
            return 1;
        }
        const char *home = getenv("HOME");
        if (!home) home = ".";

        char config_path[1024];
        snprintf(config_path, sizeof(config_path), "%s/.deepseek_config", home);

        cJSON *config = cJSON_CreateObject();
        cJSON_AddStringToObject(config, "ds_session", argv[2]);
        cJSON_AddStringToObject(config, "ds_token", argv[3]);
        char *json_str = cJSON_Print(config);
        cJSON_Delete(config);

        FILE *f = fopen(config_path, "w");
        if (!f) {
            perror("fopen config for writing");
            free(json_str);
            return 1;
        }
        fprintf(f, "%s\n", json_str);
        fclose(f);
        free(json_str);
        printf("Config saved to %s\n", config_path);
        return 0;
    }

    /* --- chat command --- */
    const char *home = getenv("HOME");
    if (!home) home = ".";

    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/.deepseek_config", home);

    FILE *f = fopen(config_path, "r");
    if (!f) {
        fprintf(stderr, "Config file not found. Run '%s init <session> <token>' first.\n", argv[0]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *file_content = malloc(fsize + 1);
    fread(file_content, 1, fsize, f);
    file_content[fsize] = '\0';
    fclose(f);

    cJSON *config = cJSON_Parse(file_content);
    free(file_content);
    if (!config) {
        fprintf(stderr, "Failed to parse config.\n");
        return 1;
    }
    cJSON *session = cJSON_GetObjectItem(config, "ds_session");
    cJSON *token = cJSON_GetObjectItem(config, "ds_token");
    if (!cJSON_IsString(session) || !cJSON_IsString(token)) {
        fprintf(stderr, "Invalid config: missing ds_session or ds_token.\n");
        cJSON_Delete(config);
        return 1;
    }

    /* Combine all remaining arguments into message */
    char message[4096] = {0};
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(message, " ");
        strcat(message, argv[i]);
    }

    srand(time(NULL));

    DeepSeekChat *chat = deepseek_chat_new(session->valuestring, token->valuestring, "wasm.wasm");
    if (!chat) {
        fprintf(stderr, "Failed to create chat.\n");
        cJSON_Delete(config);
        return 1;
    }

    // Stream the reply (printing=1), don't print the full JSON after.
    cJSON *resp = send_message(chat, message, 1, 0, 0);
    if (resp) {
        printf("\n");   // final newline after streamed text
        cJSON_Delete(resp);
    } else {
        fprintf(stderr, "Request failed.\n");
    }

    deepseek_chat_free(chat);
    cJSON_Delete(config);
    return 0;
}
