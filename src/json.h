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

inline char *remove_comma_nl(char *s)
{
    // remove the comma newline added by last add_xxxx() function
    if (*(s - 1) == '\n')
        s--;
    if (*(s - 1) == ',')
        s--;
    return s;
}

inline char *end_json(char *s, bool remove_nl = true)
{
    if (remove_nl)
        s = remove_comma_nl(s);
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

inline char *add_str(char *s, const char *k, const char *v, bool raw = false, bool comma_nl = true)
{
    if (k) // if key provided add it
    {
        *s++ = '"';
        s = stpcpy(s, k);
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
            s = stpcpy(s, v);
        }
        else
        {
            // wrap the value in quotes and escape any quotes or backslashes in the value
            *s++ = '"';
            while (*v)
            {
                if (*v == '"' || *v == '\\')
                {
                    *s++ = '\\';
                }
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
#define JSON_END() end_json(_json_p)
#define JSON_ADD_INT(k, v) _json_p = add_int(_json_p, k, v)
#define JSON_ADD_STR(k, v) _json_p = add_str(_json_p, k, v)
#define JSON_ADD_BOOL(k, v) _json_p = add_bool(_json_p, k, v)
#define JSON_ADD_RAW(k, v) _json_p = add_str(_json_p, k, v, true)               // value added without surrounding quotes
#define JSON_INSERT_COMMA_NL() _json_p = add_str(_json_p, nullptr, ",\n", true) // insert a comma newline
#define JSON_START_OBJ(k) _json_p = add_str(_json_p, k, "{\n", true, false)     // added without surrounding quotes and no comma newline
#define JSON_END_OBJ() _json_p = add_str(_json_p, nullptr, "\n}", true)         // close curly without quotes and with comma newline
#define JSON_START_ARRAY(k) _json_p = add_str(_json_p, k, "[\n", true, false)   // added without surrounding quotes and no comma newline
#define JSON_END_ARRAY() _json_p = add_str(_json_p, nullptr, "\n]", true)       // close array without quotes and with comma newline

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
