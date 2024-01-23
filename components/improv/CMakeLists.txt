set(srcs "./sdk-cpp/src/improv.cpp")

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS sdk-cpp/src
                      )
