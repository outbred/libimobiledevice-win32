#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <ctype.h>
#include <time.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/mobilebackup2.h>
#include <libimobiledevice/notification_proxy.h>
#include <libimobiledevice/afc.h>

#include <endianness.h>

#define MOBILEBACKUP2_SERVICE_NAME "com.apple.mobilebackup2"
#define NP_SERVICE_NAME "com.apple.mobile.notification_proxy"

#define LOCK_ATTEMPTS 50
#define LOCK_WAIT 200000

#ifdef WIN32
#include <windows.h>
#include <conio.h>
#include <direct.h>
#define sleep(x) Sleep(x*1000)
#else
#include <termios.h>
#include <sys/statvfs.h>
#endif

#ifndef __func__
# define __func__ __FUNCTION__
#endif

#define CODE_SUCCESS 0x00
#define CODE_ERROR_LOCAL 0x06
#define CODE_ERROR_REMOTE 0x0b
#define CODE_FILE_DATA 0x0c

static int verbose = 2;
static int quit_flag = 0;

#define PRINT_VERBOSE(min_level, ...) if (verbose >= min_level) { printf(__VA_ARGS__); };

enum cmd_mode {
	CMD_BACKUP,
	CMD_RESTORE,
	CMD_INFO,
	CMD_LIST,
	CMD_UNBACK,
	CMD_CHANGEPW,
	CMD_LEAVE
};

enum plist_format_t {
	PLIST_FORMAT_XML,
	PLIST_FORMAT_BINARY
};

enum cmd_flags {
	CMD_FLAG_RESTORE_SYSTEM_FILES       = (1 << 1),
	CMD_FLAG_RESTORE_REBOOT             = (1 << 2),
	CMD_FLAG_RESTORE_COPY_BACKUP        = (1 << 3),
	CMD_FLAG_RESTORE_SETTINGS           = (1 << 4),
	CMD_FLAG_RESTORE_REMOVE_ITEMS       = (1 << 5),
	CMD_FLAG_ENCRYPTION_ENABLE          = (1 << 6),
	CMD_FLAG_ENCRYPTION_DISABLE         = (1 << 7),
	CMD_FLAG_ENCRYPTION_CHANGEPW        = (1 << 8)
};

static int backup_domain_changed = 0;

extern char* build_path(const char* elem, ...);
extern int __mkdir(const char* path, int mode);
extern plist_t mobilebackup_factory_info_plist_new(const char* udid, lockdownd_client_t lockdown, afc_client_t afc);
extern int plist_write_to_filename(plist_t plist, const char *filename, enum plist_format_t format);
extern int plist_read_from_filename(plist_t *plist, const char *filename);
extern void do_post_notification(idevice_t device, const char *notification);
extern void mb2_handle_send_files(mobilebackup2_client_t mobilebackup2, plist_t message, const char *backup_dir);
extern int mb2_handle_receive_files(mobilebackup2_client_t mobilebackup2, plist_t message, const char *backup_dir);
extern void mb2_handle_list_directory(mobilebackup2_client_t mobilebackup2, plist_t message, const char *backup_dir);
extern void mb2_handle_make_directory(mobilebackup2_client_t mobilebackup2, plist_t message, const char *backup_dir);
extern int errno_to_device_error(int errno_value);
extern int win32err_to_errno(int err_value);
extern void mb2_copy_file_by_path(const char *src, const char *dst);
extern void mb2_copy_directory_by_path(const char *src, const char *dst);
extern int mb2_status_check_snapshot_state(const char *path, const char *udid, const char *matches);