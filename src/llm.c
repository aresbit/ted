/**
 * llm.c - Minimal curl-based LLM bridge for TED
 *
 * Environment variables (priority order):
 * 1) TED_LLM_API_URL + TED_LLM_API_KEY (+ optional TED_LLM_MODEL)
 * 2) DEEPSEEK_API_KEY (+ optional DEEPSEEK_MODEL)
 * 3) KIMI_API_KEY      -> https://api.moonshot.cn/v1/chat/completions
 */

#include "ted.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct {
    const c8 *provider;
    const c8 *url;
    const c8 *key;
    const c8 *model;
} llm_config_t;

static bool llm_load_config(llm_config_t *cfg, sp_str_t *reason) {
    const c8 *url = getenv("TED_LLM_API_URL");
    const c8 *key = getenv("TED_LLM_API_KEY");
    const c8 *model = getenv("TED_LLM_MODEL");

    if (url && url[0] && key && key[0]) {
        cfg->provider = "custom";
        cfg->url = url;
        cfg->key = key;
        cfg->model = (model && model[0]) ? model : "gpt-4.1-mini";
        if (reason) *reason = sp_str_lit("");
        return true;
    }

    const c8 *deepseek = getenv("DEEPSEEK_API_KEY");
    const c8 *deepseek_model = getenv("DEEPSEEK_MODEL");
    if (deepseek && deepseek[0]) {
        cfg->provider = "deepseek";
        cfg->url = "https://api.deepseek.com/v1/chat/completions";
        cfg->key = deepseek;
        cfg->model = (deepseek_model && deepseek_model[0]) ? deepseek_model : "deepseek-chat";
        if (reason) *reason = sp_str_lit("");
        return true;
    }

    const c8 *kimi = getenv("KIMI_API_KEY");
    if (kimi && kimi[0]) {
        cfg->provider = "kimi";
        cfg->url = "https://api.moonshot.cn/v1/chat/completions";
        cfg->key = kimi;
        cfg->model = "kimi-k2-0711-preview";
        if (reason) *reason = sp_str_lit("");
        return true;
    }

    if (reason) {
        *reason = sp_str_lit("set DEEPSEEK_API_KEY for quick start (or TED_LLM_API_URL + TED_LLM_API_KEY / KIMI_API_KEY)");
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

static sp_str_t build_chat_payload(const c8 *model, sp_str_t system_prompt, sp_str_t user_prompt) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);

    sp_str_builder_append_cstr(&b, "{\"model\":\"");
    sp_str_builder_append_cstr(&b, model);
    sp_str_builder_append_cstr(&b, "\",\"messages\":[");
    sp_str_builder_append_cstr(&b, "{\"role\":\"system\",\"content\":\"");
    sp_str_builder_append(&b, system_prompt);
    sp_str_builder_append_cstr(&b, "\"},");
    sp_str_builder_append_cstr(&b, "{\"role\":\"user\",\"content\":\"");
    sp_str_builder_append(&b, user_prompt);
    sp_str_builder_append_cstr(&b, "\"}],\"temperature\":0.2,\"stream\":false}");
    return sp_str_builder_to_str(&b);
}

static sp_str_t build_context_prompt(sp_str_t prompt, sp_str_t filename, sp_str_t lang, u32 row_1b, u32 col_1b, sp_str_t buffer) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);
    c8 num_buf[32];

    sp_str_builder_append(&b, prompt);
    sp_str_builder_append_cstr(&b, "\n\n[context]\nfile: ");
    sp_str_builder_append(&b, filename);
    sp_str_builder_append_cstr(&b, "\nlang: ");
    sp_str_builder_append(&b, lang);
    sp_str_builder_append_cstr(&b, "\ncursor: ");
    snprintf(num_buf, sizeof(num_buf), "%u", row_1b);
    sp_str_builder_append_cstr(&b, num_buf);
    sp_str_builder_append_cstr(&b, ":");
    snprintf(num_buf, sizeof(num_buf), "%u", col_1b);
    sp_str_builder_append_cstr(&b, num_buf);
    sp_str_builder_append_cstr(&b, "\nbuffer:\n");
    sp_str_builder_append(&b, buffer);
    return sp_str_builder_to_str(&b);
}

static sp_str_t build_curl_command(sp_str_t q_url, sp_str_t q_auth, sp_str_t q_file) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);

    sp_str_builder_append_cstr(&b, "curl -sS -X POST ");
    sp_str_builder_append(&b, q_url);
    sp_str_builder_append_cstr(&b, " --connect-timeout 10 --max-time 90");
    sp_str_builder_append_cstr(&b, " -H 'Content-Type: application/json' -H ");
    sp_str_builder_append(&b, q_auth);
    sp_str_builder_append_cstr(&b, " --data-binary @");
    sp_str_builder_append(&b, q_file);
    sp_str_builder_append_cstr(&b, " 2>&1");
    return sp_str_builder_to_str(&b);
}

static sp_str_t build_llm_status_text(bool ready, const c8 *provider, const c8 *model, sp_str_t why) {
    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t b = sp_str_builder_from_writer(&writer);

    if (!ready) {
        sp_str_builder_append_cstr(&b, "not configured (");
        sp_str_builder_append(&b, why);
        sp_str_builder_append_cstr(&b, ")");
        return sp_str_builder_to_str(&b);
    }

    sp_str_builder_append_cstr(&b, "ready [");
    sp_str_builder_append_cstr(&b, provider);
    sp_str_builder_append_cstr(&b, ":");
    sp_str_builder_append_cstr(&b, model);
    sp_str_builder_append_cstr(&b, "]");
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
    if (json.len == 0 || !json.data) return sp_str_lit("");

    c8 *json_cstr = malloc((size_t)json.len + 1);
    if (!json_cstr) return sp_str_lit("");
    memcpy(json_cstr, json.data, json.len);
    json_cstr[json.len] = '\0';

    sp_io_writer_t writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t out = sp_str_builder_from_writer(&writer);
    c8 pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", field);

    c8 *start = strstr(json_cstr, pat);
    if (!start) {
        free(json_cstr);
        return sp_str_lit("");
    }
    c8 *p = strchr(start, ':');
    if (!p) {
        free(json_cstr);
        return sp_str_lit("");
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') {
        free(json_cstr);
        return sp_str_lit("");
    }
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
    free(json_cstr);
    return sp_str_builder_to_str(&out);
}

static bool run_curl_json(sp_str_t url, sp_str_t key, sp_str_t payload, sp_str_t *response, sp_str_t *error) {
    const c8 *tmpdir = getenv("TMPDIR");
    if (!tmpdir || tmpdir[0] == '\0') tmpdir = "/tmp";

    c8 payload_path[512];
    snprintf(payload_path, sizeof(payload_path), "%s/ted_llm_%d_XXXXXX.json", tmpdir, (int)getpid());
    int fd = mkstemps(payload_path, 5);
    if (fd < 0) {
        if (error) *error = sp_str_lit("failed to create payload temp file");
        return false;
    }

    FILE *pf = fdopen(fd, "wb");
    if (!pf) {
        close(fd);
        remove(payload_path);
        if (error) *error = sp_str_lit("failed to write payload temp file");
        return false;
    }
    fwrite(payload.data, 1, payload.len, pf);
    fclose(pf);

    sp_str_t q_url = shell_single_quote_escape(url);
    sp_io_writer_t auth_writer = sp_io_writer_from_dyn_mem();
    sp_str_builder_t auth_builder = sp_str_builder_from_writer(&auth_writer);
    sp_str_builder_append_cstr(&auth_builder, "Authorization: Bearer ");
    sp_str_builder_append(&auth_builder, key);
    sp_str_t auth = sp_str_builder_to_str(&auth_builder);
    sp_str_t q_auth = shell_single_quote_escape(auth);
    sp_str_t q_file = shell_single_quote_escape(sp_str_from_cstr(payload_path));
    sp_str_t cmd = build_curl_command(q_url, q_auth, q_file);

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
    if (rc != 0) {
        if (error) {
            if (out.len > 0) {
                *error = out;
            } else {
                c8 err_buf[64];
                snprintf(err_buf, sizeof(err_buf), "curl exited with status %d", rc);
                *error = sp_str_from_cstr(err_buf);
            }
        }
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
        prompt_text = build_context_prompt(
            prompt,
            E.buffer.filename,
            E.buffer.lang,
            E.cursor.row + 1,
            E.cursor.col + 1,
            buf);
    }

    sp_str_t sys = sp_str_lit(
        "You are TED editor agent. Give practical editing guidance. Keep response concise and directly actionable.");
    sp_str_t esc_sys = json_escape(sys);
    sp_str_t esc_user = json_escape(prompt_text);
    sp_str_t payload = build_chat_payload(cfg.model, esc_sys, esc_user);

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
    if (!llm_load_config(&cfg, &why)) return build_llm_status_text(false, "", "", why);
    return build_llm_status_text(true, cfg.provider, cfg.model, sp_str_lit(""));
}
