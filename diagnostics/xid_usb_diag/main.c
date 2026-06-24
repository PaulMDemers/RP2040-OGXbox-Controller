#include <hal/debug.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/tcpip.h>
#include <nxdk/net.h>
#include <usbh_lib.h>
#include <xid_driver.h>

#define MAX_EVENTS 12
#define MAX_REPORT_BYTES 32
#define UDP_DIAG_PORT 49036
#define UDP_DIAG_INTERVAL_MS 500

typedef struct EventLine {
    char text[96];
} EventLine;

static EventLine events[MAX_EVENTS];
static unsigned int event_head = 0;
static unsigned int event_count = 0;

static volatile unsigned int usb_connects = 0;
static volatile unsigned int usb_disconnects = 0;
static volatile unsigned int xid_connects = 0;
static volatile unsigned int xid_disconnects = 0;
static volatile unsigned int xid_reports = 0;
static volatile int last_read_status = 0;
static volatile uint32_t last_report_tick = 0;

static xid_dev_t *active_xid = NULL;
static uint8_t last_report[MAX_REPORT_BYTES];
static uint32_t last_report_len = 0;
static bool read_queued = false;
static int udp_sock = -1;
static struct sockaddr_in udp_dest;
static uint32_t last_udp_tick = 0;
static char last_event_text[96];

extern UDEV_T *g_udev_list;
extern struct netif *g_pnetif;

static const char *speed_name(SPEED_E speed)
{
    switch (speed) {
        case SPEED_LOW:
            return "low";
        case SPEED_FULL:
            return "full";
        case SPEED_HIGH:
            return "high";
        default:
            return "?";
    }
}

static const char *xid_type_name(xid_type type)
{
    switch (type) {
        case GAMECONTROLLER_S:
            return "Controller S";
        case GAMECONTROLLER_DUKE:
            return "Duke";
        case GAMECONTROLLER_WHEEL:
            return "Wheel";
        case GAMECONTROLLER_ARCADESTICK:
            return "Arcade";
        case XREMOTE:
            return "Remote";
        case STEEL_BATTALION:
            return "Steel Battalion";
        default:
            return "Unknown";
    }
}

static void add_event(const char *fmt, ...)
{
    va_list args;
    EventLine *line = &events[event_head % MAX_EVENTS];
    va_start(args, fmt);
    vsnprintf(line->text, sizeof(line->text), fmt, args);
    va_end(args);

    snprintf(last_event_text, sizeof(last_event_text), "%s", line->text);
    debugPrint("%s\n", line->text);
    event_head++;
    if (event_count < MAX_EVENTS) {
        event_count++;
    }
}

static void append_text(char *dst, size_t dst_size, size_t *offset, const char *fmt, ...)
{
    if (*offset >= dst_size) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(dst + *offset, dst_size - *offset, fmt, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    *offset += (size_t)written;
    if (*offset >= dst_size) {
        *offset = dst_size - 1;
        dst[*offset] = '\0';
    }
}

static unsigned int count_usb_devices(void)
{
    unsigned int count = 0;
    for (UDEV_T *udev = g_udev_list; udev != NULL; udev = udev->next) {
        count++;
    }
    return count;
}

static unsigned int count_xid_devices(void)
{
    unsigned int count = 0;
    for (xid_dev_t *xid = usbh_xid_get_device_list(); xid != NULL; xid = xid->next) {
        count++;
    }
    return count;
}

static void udp_init(void)
{
    int yes = 1;

    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) {
        add_event("UDP socket failed");
        return;
    }

    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    memset(&udp_dest, 0, sizeof(udp_dest));
    udp_dest.sin_family = AF_INET;
    udp_dest.sin_port = htons(UDP_DIAG_PORT);
    udp_dest.sin_addr.s_addr = inet_addr("255.255.255.255");

    add_event("UDP broadcast ready port=%u ip=%s",
              UDP_DIAG_PORT,
              ip4addr_ntoa(netif_ip4_addr(g_pnetif)));
}

static void udp_send_diag(unsigned int frame, bool force)
{
    uint32_t now = GetTickCount();
    if (udp_sock < 0) {
        return;
    }
    if (!force && now - last_udp_tick < UDP_DIAG_INTERVAL_MS) {
        return;
    }

    char msg[1200];
    size_t off = 0;
    last_udp_tick = now;

    append_text(msg, sizeof(msg), &off,
                "xbdiag frame=%u tick=%lu ip=%s usbC=%u usbD=%u xidC=%u xidD=%u reports=%u readSt=%d reportLen=%lu",
                frame,
                now,
                ip4addr_ntoa(netif_ip4_addr(g_pnetif)),
                usb_connects,
                usb_disconnects,
                xid_connects,
                xid_disconnects,
                xid_reports,
                last_read_status,
                last_report_len);

    append_text(msg, sizeof(msg), &off, " usbList=");
    if (g_udev_list == NULL) {
        append_text(msg, sizeof(msg), &off, "none");
    }
    for (UDEV_T *udev = g_udev_list; udev != NULL; udev = udev->next) {
        append_text(msg, sizeof(msg), &off,
                    "[p%u d%u %s %04x:%04x cfg%d",
                    udev->port_num,
                    udev->dev_num,
                    speed_name(udev->speed),
                    udev->descriptor.idVendor,
                    udev->descriptor.idProduct,
                    udev->cur_conf);
        for (IFACE_T *iface = udev->iface_list; iface != NULL; iface = iface->next) {
            DESC_IF_T *ifd = iface->aif != NULL ? iface->aif->ifd : NULL;
            if (ifd != NULL) {
                append_text(msg, sizeof(msg), &off,
                            " if%u:%02x/%02x/%02x eps%u drv%s",
                            ifd->bInterfaceNumber,
                            ifd->bInterfaceClass,
                            ifd->bInterfaceSubClass,
                            ifd->bInterfaceProtocol,
                            ifd->bNumEndpoints,
                            iface->driver != NULL ? "Y" : "N");
            }
        }
        append_text(msg, sizeof(msg), &off, "]");
    }

    append_text(msg, sizeof(msg), &off, " xidList=");
    xid_dev_t *xid = usbh_xid_get_device_list();
    if (xid == NULL) {
        append_text(msg, sizeof(msg), &off, "none");
    }
    while (xid != NULL) {
        append_text(msg, sizeof(msg), &off,
                    "[uid%lu %s %04x:%04x xid=%u/%02x/%04x/%02x/%02x in%u out%u]",
                    xid->uid,
                    xid_type_name(usbh_xid_get_type(xid)),
                    xid->idVendor,
                    xid->idProduct,
                    xid->xid_desc.bLength,
                    xid->xid_desc.bDescriptorType,
                    xid->xid_desc.bcdXid,
                    xid->xid_desc.bType,
                    xid->xid_desc.bSubType,
                    xid->xid_desc.bMaxInputReportSize,
                    xid->xid_desc.bMaxOutputReportSize);
        xid = xid->next;
    }

    append_text(msg, sizeof(msg), &off, " rpt=");
    if (last_report_len == 0) {
        append_text(msg, sizeof(msg), &off, "none");
    }
    for (uint32_t i = 0; i < last_report_len && i < sizeof(last_report); ++i) {
        append_text(msg, sizeof(msg), &off, "%02x", last_report[i]);
    }

    append_text(msg, sizeof(msg), &off, " counts=usb%u/xid%u event=\"%s\"\n",
                count_usb_devices(),
                count_xid_devices(),
                last_event_text[0] ? last_event_text : "none");

    sendto(udp_sock, msg, (int)strlen(msg), 0, (struct sockaddr *)&udp_dest, sizeof(udp_dest));
}

static void device_connection_callback(UDEV_T *udev, int status)
{
    usb_connects++;
    add_event("USB connect st=%d port=%u dev=%u speed=%s vid=%04x pid=%04x cfg=%d",
              status,
              udev->port_num,
              udev->dev_num,
              speed_name(udev->speed),
              udev->descriptor.idVendor,
              udev->descriptor.idProduct,
              udev->cur_conf);
    udp_send_diag(0, true);
}

static void device_disconnect_callback(UDEV_T *udev, int status)
{
    usb_disconnects++;
    add_event("USB disconnect st=%d port=%u dev=%u vid=%04x pid=%04x",
              status,
              udev->port_num,
              udev->dev_num,
              udev->descriptor.idVendor,
              udev->descriptor.idProduct);
    udp_send_diag(0, true);
}

static void xid_connection_callback(xid_dev_t *xid, int status)
{
    xid_connects++;
    if (active_xid == NULL || (xid->idVendor == 0x045e && xid->idProduct == 0x0202)) {
        active_xid = xid;
        read_queued = false;
        memset(last_report, 0, sizeof(last_report));
        last_report_len = 0;
    }
    add_event("XID connect st=%d uid=%lu vid=%04x pid=%04x type=%02x sub=%02x in=%u out=%u",
              status,
              xid->uid,
              xid->idVendor,
              xid->idProduct,
              xid->xid_desc.bType,
              xid->xid_desc.bSubType,
              xid->xid_desc.bMaxInputReportSize,
              xid->xid_desc.bMaxOutputReportSize);
    udp_send_diag(0, true);
}

static void xid_disconnect_callback(xid_dev_t *xid, int status)
{
    xid_disconnects++;
    add_event("XID disconnect st=%d uid=%lu vid=%04x pid=%04x",
              status,
              xid->uid,
              xid->idVendor,
              xid->idProduct);
    if (active_xid == xid) {
        active_xid = NULL;
        read_queued = false;
    }
    udp_send_diag(0, true);
}

static void xid_read_callback(UTR_T *utr)
{
    uint32_t copy_len = utr->xfer_len;
    if (copy_len > sizeof(last_report)) {
        copy_len = sizeof(last_report);
    }

    last_read_status = utr->status;
    last_report_len = copy_len;
    if (copy_len > 0 && utr->buff != NULL) {
        memcpy(last_report, utr->buff, copy_len);
    }
    xid_reports++;
    last_report_tick = GetTickCount();
    read_queued = false;
    udp_send_diag(0, true);
}

static void maybe_queue_xid_read(void)
{
    if (active_xid == NULL || read_queued) {
        return;
    }

    int32_t ret = usbh_xid_read(active_xid, 0, xid_read_callback);
    if (ret == USBH_OK || ret == HID_RET_XFER_IS_RUNNING) {
        read_queued = true;
    } else {
        last_read_status = ret;
    }
}

static void print_hex_line(uint8_t const *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        pb_print("%02x ", data[i]);
    }
}

static void print_interface_summary(IFACE_T *iface)
{
    DESC_IF_T *ifd = iface->aif != NULL ? iface->aif->ifd : NULL;
    if (ifd == NULL) {
        pb_print("    if%u: no active descriptor\n", iface->if_num);
        return;
    }

    pb_print("    if%u cls=%02x sub=%02x proto=%02x eps=%u drv=%s\n",
             ifd->bInterfaceNumber,
             ifd->bInterfaceClass,
             ifd->bInterfaceSubClass,
             ifd->bInterfaceProtocol,
             ifd->bNumEndpoints,
             iface->driver != NULL ? "yes" : "no");

    for (uint8_t i = 0; i < ifd->bNumEndpoints && i < MAX_EP_PER_IFACE; ++i) {
        EP_INFO_T *ep = &iface->aif->ep[i];
        pb_print("      ep=%02x attr=%02x max=%u int=%u\n",
                 ep->bEndpointAddress,
                 ep->bmAttributes,
                 ep->wMaxPacketSize,
                 ep->bInterval);
    }
}

static void draw_screen(unsigned int frame)
{
    pb_wait_for_vbl();
    pb_target_back_buffer();
    pb_reset();
    pb_fill(0, 0, 640, 480, 0x00001018);
    pb_erase_text_screen();

    pb_print("xbtest 136 XID USB diag   frame=%u tick=%lu\n", frame, GetTickCount());
    pb_print("UDP broadcast: %s:%u\n", udp_sock >= 0 ? ip4addr_ntoa(netif_ip4_addr(g_pnetif)) : "off", UDP_DIAG_PORT);
    pb_print("USB events: connect=%u disconnect=%u    XID: connect=%u disconnect=%u reports=%u\n",
             usb_connects, usb_disconnects, xid_connects, xid_disconnects, xid_reports);
    pb_print("\n");

    pb_print("Low-level USB device list:\n");
    UDEV_T *udev = g_udev_list;
    if (udev == NULL) {
        pb_print("  none - if RP2040 is plugged in, check Xbox-side 5V/GND/D+/D-\n");
    }
    while (udev != NULL) {
        pb_print("  port=%u dev=%u speed=%s vid=%04x pid=%04x bcdUSB=%04x bcdDev=%04x cfg=%d ep0=%u\n",
                 udev->port_num,
                 udev->dev_num,
                 speed_name(udev->speed),
                 udev->descriptor.idVendor,
                 udev->descriptor.idProduct,
                 udev->descriptor.bcdUSB,
                 udev->descriptor.bcdDevice,
                 udev->cur_conf,
                 udev->ep0.wMaxPacketSize);
        for (IFACE_T *iface = udev->iface_list; iface != NULL; iface = iface->next) {
            print_interface_summary(iface);
        }
        udev = udev->next;
    }

    pb_print("\nXID device list:\n");
    xid_dev_t *xid = usbh_xid_get_device_list();
    if (xid == NULL) {
        pb_print("  none - USB may enumerate, but XID class/probe did not bind\n");
    }
    while (xid != NULL) {
        pb_print("  uid=%lu %s vid=%04x pid=%04x xidLen=%u descType=%02x bcd=%04x type=%02x sub=%02x maxIn=%u maxOut=%u\n",
                 xid->uid,
                 xid_type_name(usbh_xid_get_type(xid)),
                 xid->idVendor,
                 xid->idProduct,
                 xid->xid_desc.bLength,
                 xid->xid_desc.bDescriptorType,
                 xid->xid_desc.bcdXid,
                 xid->xid_desc.bType,
                 xid->xid_desc.bSubType,
                 xid->xid_desc.bMaxInputReportSize,
                 xid->xid_desc.bMaxOutputReportSize);
        xid = xid->next;
    }

    pb_print("\nLast XID read: status=%d len=%lu age=%lu ms\n",
             last_read_status,
             last_report_len,
             last_report_tick == 0 ? 0 : GetTickCount() - last_report_tick);
    pb_print("  ");
    print_hex_line(last_report, last_report_len);
    pb_print("\n");

    if (last_report_len >= sizeof(xid_gamepad_in)) {
        xid_gamepad_in const *pad = (xid_gamepad_in const *)last_report;
        pb_print("  parsed: len=%u buttons=%04x A=%u B=%u X=%u Y=%u Blk=%u Wht=%u L=%u R=%u\n",
                 pad->bLength,
                 pad->dButtons,
                 pad->a,
                 pad->b,
                 pad->x,
                 pad->y,
                 pad->black,
                 pad->white,
                 pad->l,
                 pad->r);
        pb_print("          LS=(%d,%d) RS=(%d,%d)\n",
                 pad->leftStickX,
                 pad->leftStickY,
                 pad->rightStickX,
                 pad->rightStickY);
    }

    pb_print("\nRecent events:\n");
    unsigned int start = event_head > event_count ? event_head - event_count : 0;
    for (unsigned int i = 0; i < event_count; ++i) {
        pb_print("  %s\n", events[(start + i) % MAX_EVENTS].text);
    }

    pb_draw_text_screen();
    while (pb_busy()) {
    }
    while (pb_finished()) {
    }
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    if (pb_init() != 0) {
        Sleep(5000);
        return 1;
    }

    pb_show_front_screen();
    add_event("Starting USB/XID diag");

    nxNetInit(NULL);
    udp_init();

    usbh_core_init();
    usbh_xid_init();
    usbh_install_conn_callback(device_connection_callback, device_disconnect_callback);
    usbh_install_xid_conn_callback(xid_connection_callback, xid_disconnect_callback);

    unsigned int frame = 0;
    while (1) {
        usbh_pooling_hubs();
        maybe_queue_xid_read();
        udp_send_diag(frame, false);
        draw_screen(frame++);
    }

    usbh_core_deinit();
    pb_kill();
    return 0;
}
