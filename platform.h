#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
  #include <direct.h>
  #include <stdlib.h>
  #define get_home() getenv("USERPROFILE")
  #define make_dir(path, mode) _mkdir(path)
#else
  #include <sys/stat.h>
  #include <stdlib.h>
  #define get_home() getenv("HOME")
  #define make_dir(path, mode) mkdir(path, mode)
#endif

#endif
