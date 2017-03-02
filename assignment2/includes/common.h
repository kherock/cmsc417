/* $Id: common.h,v 1.1 2000/03/01 14:08:20 bobby Exp $
 * Common headers
 */
#ifndef _COMMON_H_
#define _COMMON_H_

#define DefaultConfigFile "config"

#define UNUSED(x) (void)(x)

typedef unsigned int node;
typedef unsigned int cost;

typedef enum {
    false,
    true
} bool;

#endif
