#include <sys/lock.h>

#if defined(_RETARGETABLE_LOCKING)
struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___malloc_recursive_mutex;

void __retarget_lock_init(_LOCK_T lock) {
  (void)lock;
}

void __retarget_lock_init_recursive(_LOCK_T lock) {
  (void)lock;
}

void __retarget_lock_close(_LOCK_T lock) {
  (void)lock;
}

void __retarget_lock_close_recursive(_LOCK_T lock) {
  (void)lock;
}

void __retarget_lock_acquire(_LOCK_T lock) {
  (void)lock;
}

void __retarget_lock_acquire_recursive(_LOCK_T lock) {
  (void)lock;
}

int __retarget_lock_try_acquire(_LOCK_T lock) {
  (void)lock;
  return 1;
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock) {
  (void)lock;
  return 1;
}

void __retarget_lock_release(_LOCK_T lock) {
  (void)lock;
}

void __retarget_lock_release_recursive(_LOCK_T lock) {
  (void)lock;
}
#endif
