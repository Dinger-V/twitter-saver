#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
  #include <direct.h>
  #define get_home() getenv("USERPROFILE")
  #define make_dir(path) _mkdir(path)
#else
  #include <sys/stat.h>
  #define get_home() getenv("HOME")
  #define make_dir(path) mkdir(path, 0755)
#endif

#endif
