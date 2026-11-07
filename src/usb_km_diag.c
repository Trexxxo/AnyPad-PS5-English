/* Experimental, read-only USB keyboard/mouse diagnostics.
 * Reads USB descriptors but never detaches a driver or claims an interface.
 * Create /data/anypad/km_capture (or km_capture.txt) for bounded raw reports.
 * Raw keyboard reports can reveal typed keys; use only harmless test input.
 */
#include "usb_km_diag.h"
#include "log.h"

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define STATE_DIR "/data/anypad"
#define CAPTURE_FLAG STATE_DIR "/km_capture"
#define CAPTURE_FLAG_TXT STATE_DIR "/km_capture.txt"
#define CAPTURE_LOG STATE_DIR "/km_capture.log"
#define MAX_NODES 128
#define MAX_REPORT 64
#define MAX_REPORTS_PER_POLL 4
#define MAX_CAPTURE_BYTES (256L * 1024L)

typedef struct {
    char name[64];
    int fd;
    int attempted;
} km_node;

static km_node g_nodes[MAX_NODES];
static int g_count;
static long g_scan_at;
static long g_last_hint;
static unsigned g_reports;
static int g_capture_state = -1;
static int g_no_hid_hint;
static FILE *g_capture_log;

static int interesting(const char *name)
{
    return strncmp(name, "ugen", 4) == 0 ||
           strncmp(name, "ukbd", 4) == 0 ||
           strncmp(name, "ums", 3) == 0 ||
           strncmp(name, "uhid", 4) == 0;
}

static int readable_candidate(const char *name)
{
    return strncmp(name, "ukbd", 4) == 0 ||
           strncmp(name, "ums", 3) == 0 ||
           strncmp(name, "uhid", 4) == 0;
}

static int find_node(const char *name)
{
    int i;
    for (i = 0; i < g_count; i++)
        if (strcmp(g_nodes[i].name, name) == 0) return i;
    return -1;
}

/* USB_GET_DEVICEINFO / USB_GET_FULL_DESC are read-only queries. They let us
 * distinguish actual HID interfaces from the generic ugen device nodes.
 */
static void inspect_ugen(const char *name)
{
    char path[80];
    int fd, iface_index = 0, current_hid = 0, off, total;
    struct usb_device_descriptor dd;
    struct usb_device_info di;
    struct usb_gen_descriptor gd;
    unsigned char cfg[1024];

    snprintf(path, sizeof path, "/dev/%s", name);
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        log_line("km_diag: descriptor open %s failed errno=%d", path, errno);
        return;
    }

    memset(&dd, 0, sizeof dd);
    if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0) {
        log_line("km_diag: device %s id=%04x:%04x class=%02x/%02x/%02x usb=%04x",
                 name, UGETW(dd.idVendor), UGETW(dd.idProduct), dd.bDeviceClass,
                 dd.bDeviceSubClass, dd.bDeviceProtocol, UGETW(dd.bcdUSB));
    } else {
        log_line("km_diag: USB_GET_DEVICE_DESC %s failed errno=%d", name, errno);
    }

    memset(&di, 0, sizeof di);
    if (ioctl(fd, USB_GET_DEVICEINFO, &di) == 0) {
        log_line("km_diag: product %s vendor %s bus=%u addr=%u config=%u",
                 di.udi_product, di.udi_vendor, di.udi_bus, di.udi_addr,
                 di.udi_config_no);
    } else {
        log_line("km_diag: USB_GET_DEVICEINFO %s failed errno=%d", name, errno);
    }

    memset(&gd, 0, sizeof gd);
    gd.ugd_data = cfg;
    gd.ugd_maxlen = sizeof cfg;
    gd.ugd_config_index = 0xFF;
    if (ioctl(fd, USB_GET_FULL_DESC, &gd) != 0) {
        log_line("km_diag: USB_GET_FULL_DESC %s failed errno=%d", name, errno);
        close(fd);
        return;
    }

    total = gd.ugd_actlen < (int)sizeof cfg ? gd.ugd_actlen : (int)sizeof cfg;
    log_line("km_diag: descriptors %s config_bytes=%d", name, total);
    for (off = 0; off + 2 <= total;) {
        int len = cfg[off];
        int type = cfg[off + 1];
        if (len < 2 || off + len > total) {
            log_line("km_diag: malformed descriptor %s offset=%d len=%d", name, off, len);
            break;
        }
        if (type == 0x04 && len >= 9) {
            int number = cfg[off + 2];
            int cls = cfg[off + 5];
            int sub = cfg[off + 6];
            int proto = cfg[off + 7];
            char driver[64];
            struct usb_gen_descriptor id;

            current_hid = cls == 0x03;
            log_line("km_diag: interface dev=%s index=%d number=%d class=%02x subclass=%02x protocol=%02x endpoints=%u%s",
                     name, iface_index, number, cls, sub, proto, cfg[off + 4],
                     current_hid ? " HID" : "");
            memset(&id, 0, sizeof id);
            memset(driver, 0, sizeof driver);
            id.ugd_data = driver;
            id.ugd_maxlen = sizeof driver - 1;
            id.ugd_iface_index = (uint8_t)iface_index;
            if (ioctl(fd, USB_GET_IFACE_DRIVER, &id) == 0)
                log_line("km_diag: interface-driver dev=%s index=%d name=%s",
                         name, iface_index, driver);
            else
                log_line("km_diag: interface-driver dev=%s index=%d unavailable errno=%d",
                         name, iface_index, errno);
            iface_index++;
        } else if (type == 0x05 && len >= 7 && current_hid) {
            unsigned addr = cfg[off + 2];
            unsigned attr = cfg[off + 3] & 3;
            unsigned mps = cfg[off + 4] | (cfg[off + 5] << 8);
            log_line("km_diag: HID endpoint dev=%s address=%02x transfer=%u max_packet=%u interval=%u",
                     name, addr, attr, mps, cfg[off + 6]);
        }
        off += len;
    }
    close(fd);
}

/* FreeBSD exposes the control endpoint as /dev/ugenX.Y and endpoint
 * nodes under /dev/usb/X.Y.N. Inventory those nodes without opening them:
 * Sony's usb_hid driver owns these interfaces, so endpoint reads stay off
 * until we confirm which nodes can be accessed without disturbing it.
 */
static void list_ugen_endpoint_nodes(const char *name)
{
    DIR *dir;
    struct dirent *entry;
    char prefix[64];
    int found = 0;

    if (strncmp(name, "ugen", 4) != 0) return;
    snprintf(prefix, sizeof prefix, "%s.", name + 4);
    dir = opendir("/dev/usb");
    if (!dir) {
        log_line("km_diag: endpoint-node directory /dev/usb unavailable errno=%d", errno);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[128], target[128];
        struct stat st, target_st;
        ssize_t n;
        int have_target;

        if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0) continue;
        snprintf(path, sizeof path, "/dev/usb/%s", entry->d_name);
        if (lstat(path, &st) != 0) continue;
        target[0] = '\\0';
        n = readlink(path, target, sizeof target - 1);
        if (n > 0) target[n] = '\\0';
        have_target = stat(path, &target_st) == 0;
        log_line("km_diag: endpoint-node %s type=%s mode=%04o target=%s target_mode=%04o",
                 path, S_ISLNK(st.st_mode) ? "symlink" :
                 (S_ISCHR(st.st_mode) ? "char" : "other"),
                 (unsigned)(st.st_mode & 07777), target[0] ? target : "-",
                 have_target ? (unsigned)(target_st.st_mode & 07777) : 0);
        found++;
    }
    closedir(dir);
    log_line("km_diag: endpoint-node inventory %s: %d matching nodes", name, found);
}

static void scan_nodes(void)
{
    DIR *dir = opendir("/dev");
    struct dirent *entry;
    if (!dir) {
        log_line("km_diag: opendir(/dev) failed errno=%d", errno);
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        km_node *node;
        if (!interesting(entry->d_name) || find_node(entry->d_name) >= 0 ||
            g_count >= MAX_NODES) continue;
        node = &g_nodes[g_count++];
        memset(node, 0, sizeof *node);
        snprintf(node->name, sizeof node->name, "%s", entry->d_name);
        node->fd = -1;
        log_line("km_diag: USB node /dev/%s discovered", node->name);
        if (strncmp(node->name, "ugen", 4) == 0) {
            inspect_ugen(node->name);
            list_ugen_endpoint_nodes(node->name);
        }
    }
    closedir(dir);
}

static int capture_flag(const char **which, int *err)
{
    struct stat st;
    if (stat(CAPTURE_FLAG, &st) == 0 && S_ISREG(st.st_mode)) {
        *which = CAPTURE_FLAG;
        *err = 0;
        return 1;
    }
    *err = errno;
    if (stat(CAPTURE_FLAG_TXT, &st) == 0 && S_ISREG(st.st_mode)) {
        *which = CAPTURE_FLAG_TXT;
        *err = 0;
        return 1;
    }
    *err = errno;
    *which = CAPTURE_FLAG;
    return 0;
}

static void list_capture_files(void)
{
    DIR *dir = opendir(STATE_DIR);
    struct dirent *entry;
    if (!dir) {
        log_line("km_diag: cannot list %s errno=%d", STATE_DIR, errno);
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "km_capture", 10) == 0) {
            char path[128];
            struct stat st;
            snprintf(path, sizeof path, "%s/%s", STATE_DIR, entry->d_name);
            if (stat(path, &st) == 0)
                log_line("km_diag: capture-related file %s size=%ld regular=%d",
                         entry->d_name, (long)st.st_size, S_ISREG(st.st_mode) ? 1 : 0);
        }
    }
    closedir(dir);
}

static void close_capture_log(void)
{
    if (g_capture_log) fclose(g_capture_log);
    g_capture_log = NULL;
}

static void capture_log_report(const char *dev, const unsigned char *data, size_t len)
{
    size_t i;
    long pos;
    if (!g_capture_log) return;
    pos = ftell(g_capture_log);
    if (pos < 0 || pos >= MAX_CAPTURE_BYTES) {
        close_capture_log();
        log_line("km_diag: capture log reached its %ld-byte limit; stop capture", MAX_CAPTURE_BYTES);
        return;
    }
    fprintf(g_capture_log, "dev=%s len=%lu hex=", dev, (unsigned long)len);
    for (i = 0; i < len; i++) fprintf(g_capture_log, "%s%02x", i ? " " : "", data[i]);
    fputc('\n', g_capture_log);
    fflush(g_capture_log);
}

void usb_km_diag_init(void)
{
    int i;
    for (i = 0; i < g_count; i++)
        if (g_nodes[i].fd >= 0) close(g_nodes[i].fd);
    close_capture_log();
    memset(g_nodes, 0, sizeof g_nodes);
    g_count = 0;
    g_scan_at = 0;
    g_last_hint = 0;
    g_reports = 0;
    g_capture_state = -1;
    g_no_hid_hint = 0;
    log_line("km_diag: USB discovery enabled; descriptor queries are read-only");
    log_line("km_diag: create %s (or %s) to capture; reports go to %s",
             CAPTURE_FLAG, CAPTURE_FLAG_TXT, CAPTURE_LOG);
    scan_nodes();
    list_capture_files();
}

void usb_km_diag_poll(long now)
{
    int i, budget = MAX_REPORTS_PER_POLL, flag_errno = 0, capture;
    const char *flag_path = NULL;

    if (now >= g_scan_at) {
        scan_nodes();
        g_scan_at = now + 2000;
    }

    capture = capture_flag(&flag_path, &flag_errno);
    if (capture != g_capture_state) {
        log_line("km_diag: capture %s via %s (check errno=%d)",
                 capture ? "ON" : "OFF", flag_path, flag_errno);
        g_capture_state = capture;
        list_capture_files();
        if (capture) {
            g_capture_log = fopen(CAPTURE_LOG, "a");
            if (!g_capture_log)
                log_line("km_diag: cannot open %s errno=%d", CAPTURE_LOG, errno);
            else
                log_line("km_diag: raw report output is %s", CAPTURE_LOG);
        } else {
            close_capture_log();
        }
    }

    if (!capture) {
        for (i = 0; i < g_count; i++) {
            if (g_nodes[i].fd >= 0) close(g_nodes[i].fd);
            g_nodes[i].fd = -1;
            g_nodes[i].attempted = 0;
        }
        if (now - g_last_hint >= 30000) {
            log_line("km_diag: %d USB-related nodes; capture OFF (flag not found; errno=%d)",
                     g_count, flag_errno);
            list_capture_files();
            g_last_hint = now;
        }
        return;
    }

    {
        int candidates = 0;
        for (i = 0; i < g_count; i++)
            if (readable_candidate(g_nodes[i].name)) candidates++;
        if (!candidates && !g_no_hid_hint) {
            log_line("km_diag: no /dev/ukbd*, /dev/ums*, or /dev/uhid* nodes exist");
            log_line("km_diag: only generic ugen nodes are visible; raw character-device capture cannot read these");
            g_no_hid_hint = 1;
        }
    }

    for (i = 0; i < g_count && budget > 0; i++) {
        km_node *n = &g_nodes[i];
        unsigned char data[MAX_REPORT];
        ssize_t got;
        char path[80];

        if (!readable_candidate(n->name)) continue;
        if (n->fd < 0 && !n->attempted) {
            snprintf(path, sizeof path, "/dev/%s", n->name);
            n->fd = open(path, O_RDONLY | O_NONBLOCK);
            n->attempted = 1;
            log_line("km_diag: open %s %s errno=%d", path,
                     n->fd >= 0 ? "OK" : "FAILED", n->fd >= 0 ? 0 : errno);
        }
        if (n->fd < 0) continue;
        got = read(n->fd, data, sizeof data);
        if (got > 0) {
            if (g_reports < 2048) {
                capture_log_report(n->name, data, (size_t)got);
                if (g_reports == 0)
                    log_line("km_diag: first raw report received from %s (%ld bytes)",
                             n->name, (long)got);
                g_reports++;
            } else if (g_reports == 2048) {
                log_line("km_diag: report-count limit reached; remove capture flag");
                g_reports++;
            }
            budget--;
        } else if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            log_line("km_diag: read /dev/%s ended/error=%d", n->name,
                     got == 0 ? 0 : errno);
            close(n->fd);
            n->fd = -1;
        }
    }
}

void usb_km_diag_close(void)
{
    int i;
    for (i = 0; i < g_count; i++)
        if (g_nodes[i].fd >= 0) close(g_nodes[i].fd);
    close_capture_log();
    log_line("km_diag: stopped, captured %u reports", g_reports);
}
