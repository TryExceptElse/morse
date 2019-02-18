#ifndef MORSE_H_
#define MORSE_H_

#include <stdbool.h>
#include <stdint.h>


#define MORSE_MAX_LEN 1024

extern void (*morse_cb)(bool value);


void morse(const char *s, bool repeat);
void morse_update(uint32_t elapsed_ms);
void morse_stop(void);
void morse_interrupt(void);


#endif  // MORSE_H_
