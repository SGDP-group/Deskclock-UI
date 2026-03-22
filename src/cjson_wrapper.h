#ifndef CJSON_WRAPPER_H
#define CJSON_WRAPPER_H

/* Attempt to include cJSON from common install locations. */
#if defined(__has_include)
#  if __has_include(<cjson/cJSON.h>)
#    include <cjson/cJSON.h>
#  elif __has_include(<cjson.h>)
#    include <cjson.h>
#  else
#    error "cJSON header not found. Install cjson (e.g., sudo apt install libcjson-dev)."
#  endif
#else
#  include <cjson.h> /* fallback */
#endif

#endif /* CJSON_WRAPPER_H */
