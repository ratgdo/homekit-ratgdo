set(srcs "secplus/src/secplus.c")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS secplus/src
                      )
