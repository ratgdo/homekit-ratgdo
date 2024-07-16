Import("env")

# General options that are passed to the C and C++ compilers
#env.Append(CCFLAGS=["-Wno-unused-variable"])
#env.Append(CCFLAGS=["flag1", "flag2"])

# General options that are passed to the C compiler (C only; not C++).
#env.Append(CFLAGS=["flag1", "flag2"])

# General options that are passed to the C++ compiler
env.Append(CXXFLAGS=["-fconcepts-ts"])