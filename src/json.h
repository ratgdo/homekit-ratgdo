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

inline char *start_json(char *s)
{
    return stpcpy(s, "{\n");
}

inline char *end_json(char *s)
{
    s -= 2; // remove the comma newline added by last add_xxxx() function
    return stpcpy(s, "\n}");
}

inline char *add_int(char *s, const char *k, int64_t v)
{
    *s++ = '"';
    s = stpcpy(s, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    s = stpcpy(s, std::to_string(v).c_str());
    *s++ = ',';
    *s++ = '\n';
    *s = 0; // null terminate
    return s;
}

inline char *add_int(char *s, const char *k, uint64_t v)
{
    *s++ = '"';
    s = stpcpy(s, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    s = stpcpy(s, std::to_string(v).c_str());
    *s++ = ',';
    *s++ = '\n';
    *s = 0; // null terminate
    return s;
}

inline char *add_int(char *s, const char *k, int32_t v)
{
    *s++ = '"';
    s = stpcpy(s, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    s = stpcpy(s, std::to_string(v).c_str());
    *s++ = ',';
    *s++ = '\n';
    *s = 0; // null terminate
    return s;
}

inline char *add_int(char *s, const char *k, uint32_t v)
{
    *s++ = '"';
    s = stpcpy(s, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    s = stpcpy(s, std::to_string(v).c_str());
    *s++ = ',';
    *s++ = '\n';
    *s = 0; // null terminate
    return s;
}

inline char *add_str(char *s, const char *k, const char *v)
{
    *s++ = '"';
    s = stpcpy(s, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    *s++ = '"';
    s = stpcpy(s, v);
    *s++ = '"';
    *s++ = ',';
    *s++ = '\n';
    *s = 0; // null terminate
    return s;
}

inline char *add_bool(char *s, const char *k, bool v)
{
    *s++ = '"';
    s = stpcpy(s, k);
    *s++ = '"';
    *s++ = ':';
    *s++ = ' ';
    s = stpcpy(s, v ? "true" : "false");
    *s++ = ',';
    *s++ = '\n';
    *s = 0; // null terminate
    return s;
}

#define JSON_START(buf) char *_json_p = start_json(buf)
#define JSON_END() _json_p = end_json(_json_p)
#define JSON_ADD_INT(k, v) _json_p = add_int(_json_p, k, v)
#define JSON_ADD_STR(k, v) _json_p = add_str(_json_p, k, v)
#define JSON_ADD_BOOL(k, v) _json_p = add_bool(_json_p, k, v)

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
