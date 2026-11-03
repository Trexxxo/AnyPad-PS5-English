/* Experimental, read-only USB keyboard/mouse diagnostics.
 * Does not detach interfaces, issue USB ioctls, or create a virtual pad.
 * To enable bounded raw report capture, create /data/anypad/km_capture.
 * IMPORTANT: raw keyboard reports can reveal typed keys. Remove the flag
 * immediately after testing and do not type passwords while it exists.
 */
#include "usb_km_diag.h"
#include "log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CAPTURE_FLAG "/data/anypad/km_capture"
#define MAX_NODES 128
#define MAX_REPORT 32
#define MAX_REPORTS_PER_POLL 4

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
    }
    closedir(dir);
}

void usb_km_diag_init(void)
{
    int i;
    for (i = 0; i < g_count; i++)
        if (g_nodes[i].fd >= 0) close(g_nodes[i].fd);
    memset(g_nodes, 0, sizeof g_nodes);
    g_count = 0;
    g_scan_at = 0;
    g_last_hint = 0;
    g_reports = 0;
    log_line("km_diag: USB discovery enabled; raw HID reads disabled by default");
    log_line("km_diag: create %s to opt into raw read-only capture", CAPTURE_FLAG);
    scan_nodes();
}

void usb_km_diag_poll(long now)
{
    int i, budget = MAX_REPORTS_PER_POLL;
    int capture = access(CAPTURE_FLAG, F_OK) == 0;
    if (now >= g_scan_at) {
        scan_nodes();
        g_scan_at = now + 2000;
    }
    if (!capture) {
        for (i = 0; i < g_count; i++) {
            if (g_nodes[i].fd >= 0) close(g_nodes[i].fd);
            g_nodes[i].fd = -1;
            g_nodes[i].attempted = 0;
        }
        if (now - g_last_hint >= 30000) {
            log_line("km_diag: %d USB-related nodes; raw capture OFF", g_count);
            g_last_hint = now;
        }
        return;
    }
    for (i = 0; i < g_count && budget > 0; i++) {
        km_node *n = &g_nodes[i];
        unsigned char data[MAX_REPORT];
        ssize_t got;
        char path[80], hex[MAX_REPORT * 3 + 1];
        size_t j, pos = 0;
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
            for (j = 0; j < (size_t)got; j++)
                pos += (size_t)snprintf(hex + pos, sizeof hex - pos,
                                        "%s%02x", j ? " " : "", data[j]);
            log_line("km_diag: report dev=%s len=%ld hex=%s",
                     n->name, (long)got, hex);
            g_reports++;
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
    log_line("km_diag: stopped, captured %u reports", g_reports);
}
