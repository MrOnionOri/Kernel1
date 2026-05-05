#ifndef KERNEL_SHELL_H
#define KERNEL_SHELL_H

void shell_initialize(void);
void shell_put_char(char character);
void shell_complete(void);
void shell_cursor_left(void);
void shell_cursor_right(void);
void shell_cursor_home(void);
void shell_cursor_end(void);
void shell_delete_char(void);
void shell_history_previous(void);
void shell_history_next(void);
void shell_poll(void);

#endif
