/****************************************************************************
 * RATGDO HomeKit
 * https://ratcloud.llc
 * https://github.com/PaulWieland/ratgdo
 *
 * Copyright (c) 2023-25 David A Kerr... https://github.com/dkerr64/
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 *
 * Contributions acknowledged from
 * Brandon Matthews... https://github.com/thenewwazoo
 * Jonathan Stroud...  https://github.com/jgstroud
 *
 */

#pragma once

#include <cstring>
#include <cstdint>
#include <string>

// Keep room for JSON_END ("\n}\0") so a truncated object is still valid JSON.
constexpr size_t JSON_END_RESERVE = 3;

inline bool json_has_room(const char *s, const char *end, size_t n)
{
    return s && end && (end > s) && ((size_t)(end - s) > n + JSON_END_RESERVE);
}

inline char *json_puts(char *s, const char *end, const char *v)
{
    if (!s || !v)
        return s;
    while (*v && json_has_room(s, end, 1))
    {
        *s++ = *v++;
    }
    if (s && end && s < end)
        *s = 0;
    return s;
}

inline char *start_json(char *s, const char *end)
{
    return json_puts(s, end, "{\n");
}

inline char *remove_comma_nl(char *s)
{
    // remove the comma newline added by last add_xxxx() function
    if (s && *(s - 1) == '\n')
        s--;
    if (s && *(s - 1) == ',')
        s--;
    return s;
}

inline char *end_json(char *s, const char *end, bool remove_nl = true)
{
    if (remove_nl)
        s = remove_comma_nl(s);
    return json_puts(s, end, "\n}");
}

inline char *add_int(char *s, const char *end, const char *k, int64_t v)
{
    std::string num = std::to_string(v);
    size_t need = 1 + strlen(k) + 3 + num.size() + 2; // "k": num,\n
    if (!json_has_room(s, end, need))
        return s;
    *s++ = '"';
    s = json_puts(s, end, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    s = json_puts(s, end, num.c_str());
    *s++ = ',';
    *s++ = '\n';
    *s = 0;
    return s;
}

inline char *add_int(char *s, const char *end, const char *k, uint64_t v)
{
    std::string num = std::to_string(v);
    size_t need = 1 + strlen(k) + 3 + num.size() + 2;
    if (!json_has_room(s, end, need))
        return s;
    *s++ = '"';
    s = json_puts(s, end, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    s = json_puts(s, end, num.c_str());
    *s++ = ',';
    *s++ = '\n';
    *s = 0;
    return s;
}

inline char *add_int(char *s, const char *end, const char *k, int32_t v)
{
    return add_int(s, end, k, (int64_t)v);
}

inline char *add_int(char *s, const char *end, const char *k, uint32_t v)
{
    return add_int(s, end, k, (int64_t)v);
}

inline char *add_str(char *s, const char *end, const char *k, const char *v, bool raw = false, bool comma_nl = true)
{
    size_t vlen = v ? strlen(v) : 0;
    size_t klen = k ? strlen(k) : 0;
    // Worst case: every value char is escaped.
    size_t need = (k ? klen + 4 : 0) + (raw ? vlen : vlen * 2 + 2) + (comma_nl ? 2 : 0);
    if (!json_has_room(s, end, need))
        return s;

    if (k) // if key provided add it
    {
        *s++ = '"';
        s = json_puts(s, end, k);
        *s++ = '"';
        *s++ = ':';
        *s++ = ' ';
    }
    else
    {
        s = remove_comma_nl(s);
    }
    if (v) // if value provided add it
    {
        if (raw)
        {
            // Do not wrap the value in quotes
            s = json_puts(s, end, v);
        }
        else
        {
            // wrap the value in quotes and escape any quotes or backslashes in the value
            *s++ = '"';
            while (*v)
            {
                if (*v == '"' || *v == '\\')
                {
                    if (!json_has_room(s, end, 2))
                        break;
                    *s++ = '\\';
                }
                if (!json_has_room(s, end, 1))
                    break;
                *s++ = *v++;
            }
            *s++ = '"';
        }
    }
    if (comma_nl)
    {
        *s++ = ',';
        *s++ = '\n';
    }
    *s = 0;
    return s;
}

inline char *add_bool(char *s, const char *end, const char *k, bool v)
{
    const char *b = v ? "true" : "false";
    size_t need = 1 + strlen(k) + 3 + strlen(b) + 2;
    if (!json_has_room(s, end, need))
        return s;
    *s++ = '"';
    s = json_puts(s, end, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    s = json_puts(s, end, b);
    *s++ = ',';
    *s++ = '\n';
    *s = 0;
    return s;
}

#ifndef JSON_BUFFER_SIZE
#ifdef STATUS_JSON_BUFFER_SIZE
#define JSON_BUFFER_SIZE STATUS_JSON_BUFFER_SIZE
#else
#define JSON_BUFFER_SIZE 2048
#endif
#endif

#define JSON_START(buf)                                   \
    char *_json_buf = (buf);                              \
    const char *_json_end = _json_buf + JSON_BUFFER_SIZE; \
    char *_json_p = start_json(_json_buf, _json_end)
#define JSON_END() end_json(_json_p, _json_end)
#define JSON_ADD_INT(k, v) _json_p = add_int(_json_p, _json_end, k, v)
#define JSON_ADD_STR(k, v) _json_p = add_str(_json_p, _json_end, k, v)
#define JSON_ADD_BOOL(k, v) _json_p = add_bool(_json_p, _json_end, k, v)
#define JSON_ADD_RAW(k, v) _json_p = add_str(_json_p, _json_end, k, v, true)               // value added without surrounding quotes
#define JSON_INSERT_COMMA_NL() _json_p = add_str(_json_p, _json_end, nullptr, ",\n", true) // insert a comma newline
#define JSON_START_OBJ(k) _json_p = add_str(_json_p, _json_end, k, "{\n", true, false)     // added without surrounding quotes and no comma newline
#define JSON_END_OBJ() _json_p = add_str(_json_p, _json_end, nullptr, "\n}", true)         // close curly without quotes and with comma newline
#define JSON_START_ARRAY(k) _json_p = add_str(_json_p, _json_end, k, "[\n", true, false)   // added without surrounding quotes and no comma newline
#define JSON_END_ARRAY() _json_p = add_str(_json_p, _json_end, nullptr, "\n]", true)       // close array without quotes and with comma newline

#define JSON_ADD_INT_C(k, v, ov) \
    {                            \
        if (v != ov)             \
        {                        \
            ov = v;              \
            JSON_ADD_INT(k, v);  \
        }                        \
    }
#define JSON_ADD_BOOL_C(k, v, ov) \
    {                             \
        if (v != ov)              \
        {                         \
            ov = v;               \
            JSON_ADD_BOOL(k, v);  \
        }                         \
    }
#define JSON_ADD_STR_C(k, v, nv, ov) \
    {                                \
        if (nv != ov)                \
        {                            \
            ov = nv;                 \
            JSON_ADD_STR(k, v);      \
        }                            \
    }

#define JSON_REMOVE_NL(s)                        \
    for (unsigned int i = 0; i < strlen(s); i++) \
    {                                            \
        if (s[i] == '\n')                        \
            s[i] = ' ';                          \
    }
