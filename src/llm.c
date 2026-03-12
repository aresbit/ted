/**
 * llm.c - Minimal curl-based LLM bridge for TED
 *
 * Environment variables (priority order):
 * 1) TED_LLM_API_URL + TED_LLM_API_KEY (+ optional TED_LLM_MODEL)
 * 2) DEEPSEEK_API_KEY  -> https://api.deepseek.com/v1/chat/completions
 * 3) KIMI_API_KEY      -> https://api.moonshot.cn/v1/chat/completions
 */

#include "ted.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const c8 *url;
    const c8 *key;
    const c8 *model;
} llm_config_t;

static bool llm_load_config(llm_config_t *cfg, sp_str_t *reason) {
    const c8 *url = getenv("TED_LLM_API_URL");
    const c8 *key = getenv("TED_LLM_API_KEY");
    const c8 *model = getenv("TED_LLM_MODEL");

    if (url && url[0] && key && key[0]) {
        cfg->url = url;
        cfg->key = key;
        cfg->model = (model && model[0]) ? model : "gpt-4.1-mini";
        if (reason) *reason = sp_str_lit("");
        return true;
    }

    const c8 *deepseek = getenv("DEEPSEEK_API_KEY");
    if (deepseek && deepseek[0]) {
        cfg->url = "https://api.deepseek.com/v1/chat/completions";
        cfg->key = deepseek;
        cfg->model = "deepseek-chat";
        if (reason) *reason = sp_str_lit("");
        return true;
    }

    const c8 *kimi = getenv("KIMI_API_KEY");
    if (kimi && kimi[0]) {
        cfg->url = "https://api.moonshot.cn/v1/chat/completions";
        cfg->key = kimi;
        cfg->model = "kimi-k2-0711-preview";
        if (reason) *reason = sp_str_lit("");
        return true;
    }

    if (reason) {
        *reason = sp_str_lit("set TED_LLM_API_URL + TED_LLM_API_KEY (or DEEPSEEK_API_KEY / KIMI_API_KEY)");
    }
    return false;
}

static sp_str_t json_escape(sp_str_t in) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    for (u32 i = 0; i < in.len; i++) {
        c8 c = in.data[i];
        switch (c) {
            case '\\': sp_str_builder_append_cstr(&b, "\\\\"); break;
            case '"': sp_str_builder_append_cstr(&b, "\\\""); break;
            case '\n': sp_str_builder_append_cstr(&b, "\\n"); break;
            case '\r': sp_str_builder_append_cstr(&b, "\\r"); break;
            case '\t': sp_str_builder_append_cstr(&b, "\\t"); break;
            default:
                if ((unsigned char)c < 32) {
                    sp_str_builder_append_cstr(&b, " ");
                } else {
                    sp_str_builder_append_c8(&b, c);
                }
                break;
        }
    }
    return sp_str_builder_to_str(&b);
}

static sp_str_t shell_single_quote_escape(sp_str_t in) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    sp_str_builder_append_c8(&b, '\'');
    for (u32 i = 0; i < in.len; i++) {
        c8 c = in.data[i];
        if (c == '\'') {
            sp_str_builder_append_cstr(&b, "'\\''");
        } else {
            sp_str_builder_append_c8(&b, c);
        }
    }
    sp_str_builder_append_c8(&b, '\'');
    return sp_str_builder_to_str(&b);
}

static sp_str_t buffer_to_text_limited(u32 max_bytes) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    u32 used = 0;
    for (u32 i = 0; i < E.buffer.line_count; i++) {
        sp_str_t line = E.buffer.lines[i].text;
        if (used + line.len + 1 > max_bytes) {
            u32 keep = (used < max_bytes) ? (max_bytes - used) : 0;
            if (keep > 0 && keep <= line.len) {
                sp_str_builder_append(&b, sp_str_sub(line, 0, (s32)keep));
            }
            sp_str_builder_append_cstr(&b, "\n...[truncated]");
            break;
        }
        sp_str_builder_append(&b, line);
        if (i + 1 < E.buffer.line_count) sp_str_builder_append_c8(&b, '\n');
        used += line.len + 1;
    }
    return sp_str_builder_to_str(&b);
}

static sp_str_t extract_json_string_field(sp_str_t json, const c8 *field) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t out = sp_str_builder_from_writer(&writer);
    c8 pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", field);

    c8 *start = strstr(json.data, pat);
    if (!start) return sp_str_lit("");
    c8 *p = strchr(start, ':');
    if (!p) return sp_str_lit("");
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return sp_str_lit("");
    p++;

    bool esc = false;
    while (*p) {
        c8 c = *p++;
        if (esc) {
            switch (c) {
                case 'n': sp_str_builder_append_c8(&out, '\n'); break;
                case 'r': sp_str_builder_append_c8(&out, '\r'); break;
                case 't': sp_str_builder_append_c8(&out, '\t'); break;
                case '"': sp_str_builder_append_c8(&out, '"'); break;
                case '\\': sp_str_builder_append_c8(&out, '\\'); break;
                default: sp_str_builder_append_c8(&out, c); break;
            }
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') break;
        sp_str_builder_append_c8(&out, c);
    }
    return sp_str_builder_to_str(&out);
}

static bool run_curl_json(sp_str_t url, sp_str_t key, sp_str_t payload, sp_str_t *response, sp_str_t *error) {
    c8 payload_path[256];
    snprintf(payload_path, sizeof(payload_path), "/data/data/com.termux/files/home/tmp/ted_llm_%d.json", (int)getpid());

    FILE *pf = fopen(payload_path, "wb");
    if (!pf) {
        if (error) *error = sp_str_lit("failed to write payload temp file");
        return false;
    }
    fwrite(payload.data, 1, payload.len, pf);
    fclose(pf);

    sp_str_t q_url = shell_single_quote_escape(url);
    sp_str_t auth = sp_format("Authorization: Bearer {}", SP_FMT_STR(key));
    sp_str_t q_auth = shell_single_quote_escape(auth);
    sp_str_t q_file = shell_single_quote_escape(sp_str_from_cstr(payload_path));
    sp_str_t cmd = sp_format(
        "curl -sS -X POST {} -H 'Content-Type: application/json' -H {} --data-binary @{} 2>&1",
        SP_FMT_STR(q_url), SP_FMT_STR(q_auth), SP_FMT_STR(q_file));

    FILE *fp = popen(cmd.data, "r");
    if (!fp) {
        remove(payload_path);
        if (error) *error = sp_str_lit("failed to spawn curl");
        return false;
    }

    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    c8 buf[512];
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        sp_str_builder_append_cstr(&b, buf);
    }
    int rc = pclose(fp);
    remove(payload_path);

    sp_str_t out = sp_str_builder_to_str(&b);
    if (rc != 0 && out.len == 0) {
        if (error) *error = sp_str_lit("curl failed");
        return false;
    }

    if (response) *response = out;
    if (error) *error = sp_str_lit("");
    return true;
}

bool llm_query(sp_str_t prompt, bool with_context, sp_str_t *output, sp_str_t *error) {
    if (output) *output = sp_str_lit("");
    if (error) *error = sp_str_lit("");

    llm_config_t cfg = {0};
    sp_str_t why = sp_str_lit("");
    if (!llm_load_config(&cfg, &why)) {
        if (error) *error = why;
        return false;
    }
    if (prompt.len == 0) {
        if (error) *error = sp_str_lit("empty prompt");
        return false;
    }

    sp_str_t prompt_text = prompt;
    if (with_context) {
        sp_str_t buf = buffer_to_text_limited(6000);
        prompt_text = sp_format(
            "{}\n\n[context]\nfile: {}\nlang: {}\ncursor: {}:{}\nbuffer:\n{}",
            SP_FMT_STR(prompt),
            SP_FMT_STR(E.buffer.filename),
            SP_FMT_STR(E.buffer.lang),
            SP_FMT_U32(E.cursor.row + 1),
            SP_FMT_U32(E.cursor.col + 1),
            SP_FMT_STR(buf));
    }

    sp_str_t sys = sp_str_lit(
        "You are TED editor agent. Give practical editing guidance. Keep response concise and directly actionable.");
    sp_str_t esc_sys = json_escape(sys);
    sp_str_t esc_user = json_escape(prompt_text);
    sp_str_t payload = sp_format(
        "{"
        "\"model\":\"{}\","
        "\"messages\":["
        "{\"role\":\"system\",\"content\":\"{}\"},"
        "{\"role\":\"user\",\"content\":\"{}\"}"
        "],"
        "\"temperature\":0.2,"
        "\"stream\":false"
        "}",
        SP_FMT_CSTR(cfg.model), SP_FMT_STR(esc_sys), SP_FMT_STR(esc_user));

    sp_str_t resp = sp_str_lit("");
    if (!run_curl_json(sp_str_from_cstr(cfg.url), sp_str_from_cstr(cfg.key), payload, &resp, error)) {
        return false;
    }
    if (resp.len == 0) {
        if (error) *error = sp_str_lit("empty response");
        return false;
    }

    // OpenAI-compatible: choices[0].message.content
    sp_str_t content = extract_json_string_field(resp, "content");
    if (content.len == 0) {
        // Responses API / other formats sometimes use "text"
        content = extract_json_string_field(resp, "text");
    }
    if (content.len == 0) {
        sp_str_t emsg = extract_json_string_field(resp, "message");
        if (emsg.len > 0) {
            if (error) *error = emsg;
            return false;
        }
        if (error) *error = sp_str_lit("failed to parse model response");
        return false;
    }

    if (output) *output = content;
    return true;
}

sp_str_t llm_status(void) {
    llm_config_t cfg = {0};
    sp_str_t why = sp_str_lit("");
    if (!llm_load_config(&cfg, &why)) return sp_format("not configured ({})", SP_FMT_STR(why));
    return sp_format("ready [{}]", SP_FMT_CSTR(cfg.model));
}
