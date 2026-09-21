/**
 * ISAE Monitor - AI Provider Integration
 *
 * Faithful C port of Python isae_monitor/classify/providers.py.
 *
 * Behavior preserved:
 *   - Gemini endpoint uses x-goog-api-key HEADER (not ?key=) so the key
 *     does not leak via URLs.
 *   - Gemini payload includes generationConfig.thinkingConfig.thinkingLevel=low
 *     and a responseSchema restricting the JSON to {"category": "..."}.
 *   - Gemini 400 special-case: retry once with generationConfig stripped
 *     down to just {"maxOutputTokens": 512}.
 *   - Gemini response is read defensively: promptFeedback.blockReason,
 *     empty candidates, missing content.parts, empty text all produce
 *     a structured error instead of an index-out-of-bounds crash.
 *   - OpenRouter endpoint uses Authorization: Bearer + HTTP-Referer +
 *     X-Title headers.
 *   - OpenRouter model defaults to meta-llama/llama-3.3-70b-instruct.
 *   - response_format={"type":"json_object"} so the model emits JSON.
 *   - Both responses parsed via providers_extract_category() which
 *     accepts JSON {category:"..."}, JSON "string", or a bare label
 *     (after stripping markdown fences / "json" prefix).
 */

#include "isae_monitor/providers.h"
#include "isae_monitor/departments.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define GEMINI_ENDPOINT_FMT \
    "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent"
#define OPENROUTER_ENDPOINT "https://openrouter.ai/api/v1/chat/completions"

#define MAX_OUTPUT_TOKENS 512
#define MAX_PROMPT_LEN 16384
#define MAX_RAW_RESPONSE_LEN 8192

/* ------------------------------------------------------------------ */
/* Response lifecycle                                                  */
/* ------------------------------------------------------------------ */

void ai_response_init(ai_response_t* resp) {
    if (!resp) return;
    resp->raw_text = NULL;
    resp->status = 0;
    resp->ok = false;
    resp->error[0] = '\0';
}

void ai_response_cleanup(ai_response_t* resp) {
    if (!resp) return;
    free(resp->raw_text);
    ai_response_init(resp);
}

/* ------------------------------------------------------------------ */
/* Prompt construction                                                 */
/* ------------------------------------------------------------------ */

char* providers_build_prompt(const announcement_t* ann) {
    if (!ann) return NULL;
    char* categories = departments_describe_for_prompt();
    if (!categories) return NULL;

    const char* title = ann->title ? ann->title : "";
    const char* summary = ann->summary && ann->summary[0] ? ann->summary : "(no summary)";

    /* Sized generously; titles can be 500+ chars. */
    size_t need = MAX_PROMPT_LEN + strlen(title) + strlen(summary) + strlen(categories) + 256;
    char* prompt = (char*)malloc(need);
    if (!prompt) { free(categories); return NULL; }

    int n = snprintf(prompt, need,
        "You route announcements for ISSAE / Cnam Liban, a Lebanese "
        "higher-education institute whose notices are published in French and Arabic.\n\n"
        "Announcement title: %s\n"
        "Announcement summary: %s\n\n"
        "Assign exactly one category:\n"
        "%s\n"
        "Rules:\n"
        "- Departments are distinct. Something being \"engineering\" does not make it "
        "Informatique; Génie Civil, Génie Électrique, Génie Mécanique and Génie des "
        "Procédés are all engineering too.\n"
        "- If a notice names a department explicitly, use that department.\n"
        "- If it applies to the whole institute (fees, registration, closures, "
        "institute-wide exam calendars), use \"general\".\n"
        "- If it is not aimed at students at all (job adverts, tenders), use \"other\".\n"
        "- Arabic notices follow the same rules.\n\n"
        "Respond with JSON only: {\"category\": \"<one of the labels above>\"}",
        title, summary, categories);

    free(categories);
    if (n < 0 || (size_t)n >= need) {
        free(prompt);
        return NULL;
    }
    return prompt;
}

/* ------------------------------------------------------------------ */
/* Category extraction (mirrors Python _extract_category)              */
/* ------------------------------------------------------------------ */

/* Strip leading whitespace, surrounding backticks (markdown fences),
 * and a leading literal "json" prefix. */
static void clean_response(const char* in, char* out, size_t out_size) {
    if (!in || !out || out_size == 0) { if (out && out_size) out[0] = '\0'; return; }
    size_t len = strlen(in);
    /* ltrim */
    while (len > 0 && isspace((unsigned char)*in)) { in++; len--; }
    /* Strip leading backticks */
    while (len >= 1 && *in == '`') { in++; len--; }
    /* Strip leading "json" prefix (case-insensitive) */
    if (len >= 4 && strncasecmp(in, "json", 4) == 0) {
        in += 4; len -= 4;
        while (len > 0 && isspace((unsigned char)*in)) { in++; len--; }
    }
    /* rtrim trailing backticks and whitespace */
    while (len > 0 && (in[len - 1] == '`' || isspace((unsigned char)in[len - 1]))) len--;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, in, len);
    out[len] = '\0';
}

const char* providers_extract_category(const char* raw_text) {
    static char cleaned[MAX_RAW_RESPONSE_LEN];
    if (!raw_text || !*raw_text) return NULL;
    clean_response(raw_text, cleaned, sizeof(cleaned));
    if (!*cleaned) return NULL;

    /* Try JSON parse. */
    cJSON* root = cJSON_Parse(cleaned);
    if (root) {
        const char* result = NULL;
        if (cJSON_IsObject(root)) {
            cJSON* cat = cJSON_GetObjectItem(root, "category");
            if (cJSON_IsString(cat)) result = departments_resolve(cat->valuestring);
        } else if (cJSON_IsString(root)) {
            result = departments_resolve(root->valuestring);
        }
        if (result) {
            /* Copy into the static buffer so the caller doesn't need to
             * manage lifetime (matches Python's string-return semantics). */
            strncpy(cleaned, result, sizeof(cleaned) - 1);
            cleaned[sizeof(cleaned) - 1] = '\0';
            cJSON_Delete(root);
            return cleaned;
        }
        cJSON_Delete(root);
    }

    /* Not JSON: accept only if the whole response is one known label
     * (after stripping surrounding quotes and trailing dots). */
    char token[MAX_CATEGORY_NAME];
    size_t tlen = strlen(cleaned);
    if (tlen == 0) return NULL;
    if (tlen >= sizeof(token)) tlen = sizeof(token) - 1;
    /* Strip surrounding double-quotes and trailing periods. */
    size_t start = 0, end = tlen;
    if (cleaned[0] == '"') start = 1;
    if (end > start && cleaned[end - 1] == '"') end--;
    while (end > start && cleaned[end - 1] == '.') end--;
    size_t new_len = end - start;
    memcpy(token, cleaned + start, new_len);
    token[new_len] = '\0';
    for (char* p = token; *p; p++) *p = (char)tolower((unsigned char)*p);

    /* Direct match against canonical keys or pseudo-categories. Copy
     * the result into the static buffer so callers can hold the pointer. */
    if (departments_is_valid_category(token)) {
        strncpy(cleaned, token, sizeof(cleaned) - 1);
        cleaned[sizeof(cleaned) - 1] = '\0';
        return cleaned;
    }
    /* Try alias resolution. */
    const char* resolved = departments_resolve(token);
    if (resolved) {
        strncpy(cleaned, resolved, sizeof(cleaned) - 1);
        cleaned[sizeof(cleaned) - 1] = '\0';
        return cleaned;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Gemini response reading                                            */
/* ------------------------------------------------------------------ */

static void set_err(ai_response_t* resp, long status, const char* msg) {
    resp->ok = false;
    resp->status = status;
    strncpy(resp->error, msg, sizeof(resp->error) - 1);
    resp->error[sizeof(resp->error) - 1] = '\0';
}

/* Concatenate all parts[0..].text from candidate.content.parts. */
static char* read_gemini_text(cJSON* root, char* err_out, size_t err_size) {
    if (!cJSON_IsObject(root)) {
        snprintf(err_out, err_size, "unexpected Gemini response shape");
        return NULL;
    }
    cJSON* pf = cJSON_GetObjectItem(root, "promptFeedback");
    if (pf && cJSON_IsObject(pf)) {
        cJSON* br = cJSON_GetObjectItem(pf, "blockReason");
        if (br && cJSON_IsString(br)) {
            snprintf(err_out, err_size, "blocked: %s", br->valuestring);
            return NULL;
        }
    }
    cJSON* candidates = cJSON_GetObjectItem(root, "candidates");
    if (!candidates || !cJSON_IsArray(candidates) || cJSON_GetArraySize(candidates) == 0) {
        snprintf(err_out, err_size, "Gemini returned no candidates");
        return NULL;
    }
    cJSON* candidate = cJSON_GetArrayItem(candidates, 0);
    cJSON* content = cJSON_GetObjectItem(candidate, "content");
    cJSON* parts = content ? cJSON_GetObjectItem(content, "parts") : NULL;
    if (!parts || !cJSON_IsArray(parts)) {
        /* Maybe finishReason is the diagnostic. */
        cJSON* fr = cJSON_GetObjectItem(candidate, "finishReason");
        const char* reason = (fr && cJSON_IsString(fr)) ? fr->valuestring : "unknown";
        snprintf(err_out, err_size, "Gemini returned empty text (finishReason=%s)", reason);
        return NULL;
    }
    /* Concatenate all parts' .text */
    size_t total = 0;
    cJSON* part;
    cJSON_ArrayForEach(part, parts) {
        cJSON* t = cJSON_GetObjectItem(part, "text");
        if (t && cJSON_IsString(t)) total += strlen(t->valuestring);
    }
    if (total == 0) {
        cJSON* fr = cJSON_GetObjectItem(candidate, "finishReason");
        const char* reason = (fr && cJSON_IsString(fr)) ? fr->valuestring : "unknown";
        snprintf(err_out, err_size, "Gemini returned empty text (finishReason=%s)", reason);
        return NULL;
    }
    char* buf = (char*)malloc(total + 1);
    if (!buf) { snprintf(err_out, err_size, "OOM reading Gemini text"); return NULL; }
    buf[0] = '\0';
    cJSON_ArrayForEach(part, parts) {
        cJSON* t = cJSON_GetObjectItem(part, "text");
        if (t && cJSON_IsString(t)) strcat(buf, t->valuestring);
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* Gemini call with 400 special-case                                   */
/* ------------------------------------------------------------------ */

static cJSON* build_gemini_payload(bool full) {
    cJSON* root = cJSON_CreateObject();
    cJSON* contents = cJSON_CreateArray();
    /* 'text' is set by the caller after this function returns; we leave
     * the contents array empty here. */
    cJSON_AddItemToObject(root, "contents", contents);

    cJSON* gen_cfg = cJSON_CreateObject();
    cJSON_AddNumberToObject(gen_cfg, "maxOutputTokens", MAX_OUTPUT_TOKENS);
    if (full) {
        cJSON_AddStringToObject(gen_cfg, "responseMimeType", "application/json");
        cJSON* schema = cJSON_CreateObject();
        cJSON_AddStringToObject(schema, "type", "OBJECT");
        cJSON* props = cJSON_CreateObject();
        cJSON* cat_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(cat_prop, "type", "STRING");
        /* enum = list of valid categories */
        cJSON* enum_arr = cJSON_CreateArray();
        cJSON_AddItemToArray(enum_arr, cJSON_CreateString(CATEGORY_GENERAL));
        for (size_t i = 0; i < departments_count(); i++) {
            const department_t* d = departments_get_at(i);
            cJSON_AddItemToArray(enum_arr, cJSON_CreateString(d->key));
        }
        cJSON_AddItemToObject(cat_prop, "enum", enum_arr);
        cJSON_AddItemToObject(props, "category", cat_prop);
        cJSON_AddItemToObject(schema, "properties", props);
        cJSON* required = cJSON_CreateArray();
        cJSON_AddItemToArray(required, cJSON_CreateString("category"));
        cJSON_AddItemToObject(schema, "required", required);
        cJSON_AddItemToObject(gen_cfg, "responseSchema", schema);

        cJSON* tc = cJSON_CreateObject();
        cJSON_AddStringToObject(tc, "thinkingLevel", "low");
        cJSON_AddItemToObject(gen_cfg, "thinkingConfig", tc);
    }
    cJSON_AddItemToObject(root, "generationConfig", gen_cfg);
    return root;
}

isae_error_t providers_call_gemini(const char* prompt,
                                   const char* api_key,
                                   const char* model,
                                   const http_client_config_t* http_cfg,
                                   ai_response_t* response) {
    if (!prompt || !api_key || !model || !response) return ISAE_ERR_INVALID_PARAM;

    char url[512];
    int n = snprintf(url, sizeof(url), GEMINI_ENDPOINT_FMT, model);
    if (n < 0 || (size_t)n >= sizeof(url)) return ISAE_ERR_INVALID_PARAM;

    /* Header: x-goog-api-key: <key> */
    char auth_header[MAX_API_KEY_LEN + 32];
    snprintf(auth_header, sizeof(auth_header), "x-goog-api-key: %s", api_key);
    const char* headers[] = { auth_header, NULL };

    char err_buf[256];
    isae_error_t err;

    for (int full_attempt = 1; full_attempt >= 0; full_attempt--) {
        cJSON* payload = build_gemini_payload(full_attempt == 1);
        /* Insert the prompt into the placeholder. */
        cJSON* contents = cJSON_GetObjectItem(payload, "contents");
        cJSON* msg_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_obj, "role", "user");
        cJSON* parts = cJSON_CreateArray();
        cJSON* part = cJSON_CreateObject();
        cJSON_AddStringToObject(part, "text", prompt);
        cJSON_AddItemToArray(parts, part);
        cJSON_AddItemToObject(msg_obj, "parts", parts);
        cJSON_AddItemToArray(contents, msg_obj);

        char* body = cJSON_PrintUnformatted(payload);
        cJSON_Delete(payload);
        if (!body) return ISAE_ERR_MEMORY;

        http_response_t http_resp;
        http_response_init(&http_resp);
        err = http_post_json(url, body, &http_resp, http_cfg, headers);
        free(body);

        long status = http_resp.status_code;
        if (err == ISAE_OK) {
            cJSON* root = cJSON_Parse(http_resp.body ? http_resp.body : "");
            if (root) {
                char* text = read_gemini_text(root, err_buf, sizeof(err_buf));
                cJSON_Delete(root);
                if (text) {
                    response->raw_text = text;
                    response->ok = true;
                    response->status = status;
                    http_response_cleanup(&http_resp);
                    return ISAE_OK;
                }
                set_err(response, status, err_buf);
                http_response_cleanup(&http_resp);
                /* A parse failure isn't a 400 retry trigger. */
                return ISAE_ERR_PARSE;
            }
            set_err(response, status, "Gemini returned non-JSON body");
            http_response_cleanup(&http_resp);
            return ISAE_ERR_PARSE;
        }

        /* HTTP failure. */
        if (status == 400 && full_attempt == 1) {
            /* Retry once with the stripped-down payload. */
            http_response_cleanup(&http_resp);
            continue;
        }
        snprintf(err_buf, sizeof(err_buf), "gemini: HTTP %ld for generativelanguage.googleapis.com", status);
        set_err(response, status, err_buf);
        http_response_cleanup(&http_resp);
        return ISAE_ERR_HTTP;
    }
    return ISAE_ERR_HTTP;
}

/* ------------------------------------------------------------------ */
/* OpenRouter                                                          */
/* ------------------------------------------------------------------ */

static char* read_openrouter_text(cJSON* root, char* err_out, size_t err_size) {
    if (!cJSON_IsObject(root)) {
        snprintf(err_out, err_size, "unexpected OpenRouter response shape");
        return NULL;
    }
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        char preview[256];
        const char* body = cJSON_PrintUnformatted(root);
        if (body) {
            strncpy(preview, body, sizeof(preview) - 1);
            preview[sizeof(preview) - 1] = '\0';
            free((void*)body);
        } else {
            preview[0] = '\0';
        }
        snprintf(err_out, err_size, "OpenRouter returned no choices: %s", preview);
        return NULL;
    }
    cJSON* choice0 = cJSON_GetArrayItem(choices, 0);
    cJSON* message = cJSON_GetObjectItem(choice0, "message");
    cJSON* content = message ? cJSON_GetObjectItem(message, "content") : NULL;
    if (!content || !cJSON_IsString(content) || !content->valuestring[0]) {
        snprintf(err_out, err_size, "OpenRouter returned empty content");
        return NULL;
    }
    return strdup(content->valuestring);
}

isae_error_t providers_call_openrouter(const char* prompt,
                                        const char* api_key,
                                        const char* model,
                                        const http_client_config_t* http_cfg,
                                        ai_response_t* response) {
    if (!prompt || !api_key || !model || !response) return ISAE_ERR_INVALID_PARAM;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON* messages = cJSON_CreateArray();
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", prompt);
    cJSON_AddItemToArray(messages, msg);
    cJSON_AddItemToObject(root, "messages", messages);
    cJSON_AddNumberToObject(root, "max_tokens", MAX_OUTPUT_TOKENS);
    cJSON_AddNumberToObject(root, "temperature", 0);
    cJSON* rf = cJSON_CreateObject();
    cJSON_AddStringToObject(rf, "type", "json_object");
    cJSON_AddItemToObject(root, "response_format", rf);

    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ISAE_ERR_MEMORY;

    char auth_header[MAX_API_KEY_LEN + 32];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    const char* headers[] = {
        auth_header,
        "HTTP-Referer: https://github.com/E-Vex/monreqAI-CNAM-Liban",
        "X-Title: ISAE Announcements Monitor",
        NULL,
    };

    http_response_t http_resp;
    http_response_init(&http_resp);
    isae_error_t err = http_post_json(OPENROUTER_ENDPOINT, body, &http_resp, http_cfg, headers);
    free(body);

    char err_buf[256];
    if (err == ISAE_OK) {
        cJSON* parsed = cJSON_Parse(http_resp.body ? http_resp.body : "");
        if (parsed) {
            char* text = read_openrouter_text(parsed, err_buf, sizeof(err_buf));
            cJSON_Delete(parsed);
            if (text) {
                response->raw_text = text;
                response->ok = true;
                response->status = http_resp.status_code;
                http_response_cleanup(&http_resp);
                return ISAE_OK;
            }
            set_err(response, http_resp.status_code, err_buf);
            http_response_cleanup(&http_resp);
            return ISAE_ERR_PARSE;
        }
        set_err(response, http_resp.status_code, "OpenRouter returned non-JSON body");
        http_response_cleanup(&http_resp);
        return ISAE_ERR_PARSE;
    }
    snprintf(err_buf, sizeof(err_buf), "openrouter: HTTP %ld for openrouter.ai", http_resp.status_code);
    set_err(response, http_resp.status_code, err_buf);
    http_response_cleanup(&http_resp);
    return ISAE_ERR_HTTP;
}
