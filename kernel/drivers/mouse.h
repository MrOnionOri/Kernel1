#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

void mouse_initialize(void);
void mouse_handle_irq(void);
void mouse_print_status(void);

#endif
