#ifndef KERNEL_KAPP_H
#define KERNEL_KAPP_H

struct task;

void kapp_inspect(const char* path);
struct task* kapp_spawn_app(const char* name, const char* args);
int kapp_exists(const char* name);

#endif
