/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef VIRTUAL_KEYBOARD_H
#define VIRTUAL_KEYBOARD_H

#include <stdint.h>
#include <memory>

struct vkbd;

std::shared_ptr<vkbd> vkbd_init(const char *name);

void vkbd_mouse_move(struct vkbd* vkbd, int x, int y);
void vkbd_mouse_move_abs(struct vkbd* vkbd, int x, int y);
void vkbd_mouse_scroll(struct vkbd* vkbd, int x, int y);

void vkbd_send_key(struct vkbd* vkbd, uint16_t code, int state);
void vkbd_flush(struct vkbd* vkbd);
#endif
