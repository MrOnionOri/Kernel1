#ifndef KERNEL_KAPP_H
#define KERNEL_KAPP_H

void kapp_inspect(const char* path);
int kapp_spawn_app(const char* name, const char* args);
int kapp_exists(const char* name);

#endif
