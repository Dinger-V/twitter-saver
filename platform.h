#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <direct.h>
  #define get_home() getenv("USERPROFILE")
  #define make_dir(path, mode) _mkdir(path)

  
  #ifndef S_ISDIR
    #define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
  #endif
#else
  #define get_home() getenv("HOME")
  #define make_dir(path, mode) mkdir(path, mode)
#endif

#endif
