#ifndef __TYPES_H__
#define __TYPES_H__

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned char uint8_t;
typedef char int8_t;

typedef int bool_t;
typedef char boolean_t;

typedef unsigned long u_long;
typedef unsigned char u_char;

/* Physical address type definition */
typedef uint64_t phys_addr_t;

#endif	/* __TYPES_H__ */