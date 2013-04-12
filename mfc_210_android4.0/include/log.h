#ifndef MY_LOG_FUNCTION_H
#define MY_LOG_FUNCTION_H

#include <stdio.h>

#define LOGE(fmt, ...)                         \
	do {                                            \
		printf("ERROR -- "                             \
			"%s: " fmt, __func__, ##__VA_ARGS__);   \
	} while (0)

#define LOGV(fmt, ...)                         \
	do {                                            \
		printf("VERBOSE -- "                             \
			"%s: " fmt, __func__, ##__VA_ARGS__);   \
	} while (0)

#endif