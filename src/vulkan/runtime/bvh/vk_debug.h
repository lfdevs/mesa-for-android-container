
#ifndef BVH_VK_DEBUG_H
#define BVH_VK_DEBUG_H

#ifndef NDEBUG

#extension GL_EXT_debug_printf : enable

#define printf debugPrintfEXT
#define assert(expr, message) do { if (!(expr)) printf(message); } while (false)

#else

#define printf
#define assert(expr, message)

#endif

#endif
