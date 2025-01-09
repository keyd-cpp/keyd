/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
/* Build with make vkbd-stdout. */

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>

#include "../vkbd.h"
#include "../keys.h"

struct vkbd {};

std::shared_ptr<vkbd> vkbd_init(const char *name)
{
	return nullptr;
}

void vkbd_mouse_scroll(struct vkbd* vkbd, int x, int y)
{
	printf("mouse scroll: x: %d, y: %d\n", x, y);
}

void vkbd_mouse_move(struct vkbd* vkbd, int x, int y)
{
	printf("mouse movement: x: %d, y: %d\n", x, y);
}

void vkbd_mouse_move_abs(struct vkbd* vkbd, int x, int y)
{
	printf("absolute mouse movement: x: %d, y: %d\n", x, y);
}

void vkbd_send_key(struct vkbd* vkbd, uint16_t code, int state)
{
	printf("key: %s, state: %d\n", KEY_NAME(code), state);
}

void vkbd_flush(struct vkbd*)
{
}
