#ifndef XSM_CONSTANTS_H

#define XSM_CONSTANTS_H

#include <stdbool.h>

#define XSM_WORD_SIZE 16
#define XSM_MEMORY_NUMPAGES 144
#define XSM_PAGE_SIZE 512
#define XSM_NUM_CORES 2

#define XSM_REGSIZE XSM_WORD_SIZE
#define XSM_NUM_REG 34

#define XSM_ILLINSTR -1
#define XSM_SUCCESS 1
#define XSM_FAILURE 0

#define XSM_INSTRUCTION_SIZE 2

/* Disk states */
#define XSM_DISK_IDLE 0
#define XSM_DISK_BUSY 1

/* Console states */
#define XSM_CONSOLE_IDLE 0
#define XSM_CONSOLE_BUSY 1

/* Core flags */
#define PRIMARY_CORE 0
#define SECONDARY_CORE 1

#define RESET_MODE 0
#define ACTIVE_MODE 1

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* Interrupts */
#define XSM_INTERRUPT_EXCEPTION 0
#define XSM_INTERRUPT_TIMER 1
#define XSM_INTERRUPT_DISK 2
#define XSM_INTERRUPT_CONSOLE 3

static const char *XSM_DEFAULT_DISK = "../nexfs-interface/disk.xfs";

#endif