#ifndef __DEBUG_H_
#define __DEBUG_H_

#ifdef _IS_DEBUG
#define DEBUG(fmt, arg...) \
  do { \
    fprintf (stderr, fmt "\r\n", ##arg); \
  } while (0)
#else
#define DEBUG(fmt, arg...) do { } while (0)
#endif

#endif
