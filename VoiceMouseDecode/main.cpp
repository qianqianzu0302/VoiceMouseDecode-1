#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <cstring>
#include "sbc.h"
#include "PCMServer.h"
#include <time.h>
#include "denoise.h"
#include "hidapi.h"

int main()
{
    int res;
    unsigned char buf[64];

    // 初始化 HIDAPI
    if (hid_init()) {
        printf("Failed to initialize HIDAPI\n");
        return 1;
    }

    // 打开设备，替换为你的 VID/PID
    struct hid_device_info *devs, *cur_dev;
    const char *path_to_open = NULL;
    hid_device * handle = NULL;
    unsigned short vendor_id = 0x248a;
    unsigned short product_id = 0xca10;

    /* register_global_error: global error is reset by hid_enumerate/hid_init */
    devs = hid_enumerate(vendor_id, product_id);
    if (devs == NULL) {
        /* register_global_error: global error is already set by hid_enumerate */
        return NULL;
    }

    cur_dev = devs;
    while (cur_dev) {
        printf("Device Path: %s\n", cur_dev->path);
        printf("Usage Page: 0x%hx, Usage: 0x%hx\n\n", cur_dev->usage_page, cur_dev->usage);
        if (cur_dev->vendor_id == vendor_id &&
            cur_dev->product_id == product_id &&
            cur_dev->usage_page == 0xff02 &&
            cur_dev->usage == 0x0)
        {
            path_to_open = cur_dev->path;
            printf("Found device, Usage Page: 0x%hx, Usage: 0x%hx, path: %s\n\n", cur_dev->usage_page, cur_dev->usage, cur_dev->path);
            break;
        }
        cur_dev = cur_dev->next;  // 一定要在 while 内部
    }


    if (path_to_open) {
        printf("will open ff02\n");
        handle = hid_open_path(path_to_open);
    } else {
        printf("Device with requested VID/PID/(SerialNumber) not found");
    }

    hid_free_enumeration(devs);

    // 读取数据
    while (1) {
        res = hid_read(handle, buf, sizeof(buf));
        printf("Start to read data, expected data length:%d\n", sizeof(buf));
        if (res > 0) {
            printf("Read %d bytes: ", res);
            for (int i = 0; i < res; i++)
                printf("%02X ", buf[i]);
            printf("\n");
        }
        // 可加入延时，避免 CPU 占用过高
    }

    // 关闭设备
    hid_close(handle);
    hid_exit();

    return 0;
}
