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

#define START_JSON(s)     \
    {                     \
        s[0] = 0;         \
        strcat(s, "{\n"); \
    }
#define END_JSON(s)           \
    {                         \
        s[strlen(s) - 2] = 0; \
        strcat(s, "\n}");     \
    }
#define ADD_INT(s, k, v)                      \
    {                                         \
        strcat(s, "\"");                      \
        strcat(s, (k));                       \
        strcat(s, "\": ");                    \
        strcat(s, std::to_string(v).c_str()); \
        strcat(s, ",\n");                     \
    }
#define ADD_STR(s, k, v)     \
    {                        \
        strcat(s, "\"");     \
        strcat(s, (k));      \
        strcat(s, "\": \""); \
        strcat(s, (v));      \
        strcat(s, "\",\n");  \
    }
#define ADD_BOOL(s, k, v)                  \
    {                                      \
        strcat(s, "\"");                   \
        strcat(s, (k));                    \
        strcat(s, "\": ");                 \
        strcat(s, (v) ? "true" : "false"); \
        strcat(s, ",\n");                  \
    }
#define ADD_INT_C(s, k, v, ov) \
    {                          \
        if (v != ov)           \
        {                      \
            ov = v;            \
            ADD_INT(s, k, v)   \
        }                      \
    }
#define ADD_BOOL_C(s, k, v, ov) \
    {                           \
        if (v != ov)            \
        {                       \
            ov = v;             \
            ADD_BOOL(s, k, v)   \
        }                       \
    }
#define ADD_STR_C(s, k, v, nv, ov) \
    {                              \
        if (nv != ov)              \
        {                          \
            ov = nv;               \
            ADD_STR(s, k, v)       \
        }                          \
    }
// #define REMOVE_NL(s) for (char *p = (char *)s; (p = strchr(p, '\n')) != NULL; *p = ' ') {}
#define REMOVE_NL(s)                    \
    for (unsigned int i = 0; i < strlen(s); i++) \
    {                                   \
        if (s[i] == '\n')               \
            s[i] = ' ';                 \
    }
