#pragma once

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

#ifdef WIN32
#define BS_CC '\b'
#define my_getch getch
#else
#define BS_CC 0x7f
static int my_getch()
{
	struct termios oldt, newt;
	int ch;
	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	return ch;
}
#endif

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

enum ErrorFlags {
	RESTORE_FAILED									= 1,
	MISSING_PASSWORDS_FOR_CHANGE_PASSWORDS			= (1 << 1),
	NO_BACKUP_DIRECTORY							    = (1 << 2),
	BACKUP_DIRECTORY_DNE							= (1 << 3),
	MISSING_INFO_PLIST								= (1 << 4), // missing Info.plist from backup dir for udid
	MISSING_MANIFEST_PLIST							= (1 << 5), // missing Manifest.plist from backup dir for udid
	CANNOT_CONNECT_TO_DEVICE						= (1 << 6),
	MISSING_PASSWORD_FOR_ENCRYPTED_BACKUP			= (1 << 7),
	COULD_NOT_START_NOTIFICATION_PROXY				= (1 << 8),
	COULD_NOT_START_AFC								= (1 << 9),
	PROTOCOL_VERSION_EXCHANGE_FAILED				= (1 << 10),
	USER_CANCELED									= (1 << 11),
	UNABLE_TO_LOCK_SYNC								= (1 << 12),
	PROTOCOL_VERSION_MISMATCH						= (1 << 13), // backup or restore
	DEVICE_ERROR									= (1 << 14), // backup or restore
	UNKNOWN_ERROR									= (1 << 16), // backup or restore
	BACKUP_ABORTED									= (1 << 17),
	BACKUP_FAILED									= (1 << 18),
	UNBACK_ABORTED									= (1 << 19),
	CHANGE_PASSWORD_FAILED							= (1 << 20),
	COULD_NOT_DISABLE_ENCRYPTION					= (1 << 21),
	COULD_NOT_ENABLE_ENCRYPTION						= (1 << 22)
};

static int backup_domain_changed = 0;

typedef void* (*progress_t)(uint64_t current, uint64_t total);

class iDeviceBackup2
{
public:
	int GetErrorFlags() { return Errors; }
	static bool HasError(ErrorFlags flag, int errors) {
		return (errors & flag) > 0;
	}
	cmd_mode GetRequestedOperation() {
		return RequestedOperation;
	}
	iDeviceBackup2(const char *backupDirectory = NULL, bool backup = false, bool restore = false, const char *input_udid = NULL, 
		const char *input_sourceUdid = NULL, bool restoreSystem = false, bool rebootAfterRestore = false, bool copyBeforeRestore = false, 
		bool restoreSettings = false, bool removeUnusedAfterRestore = false, const char *password = NULL, const char *newPassword = NULL, 
		bool changePassword = false, bool getInfo = false, bool getList = false, bool unback = false, int debugLevel = 0) {

		Initialize();

		idevice_set_debug_level(debugLevel);
		udid = strdup(input_udid);
		source_udid = strdup(input_sourceUdid);
		backup_directory = strdup(backupDirectory);

		if(backup) {
			cmd = CMD_BACKUP;
		}
		else if(restore) {
			cmd = CMD_RESTORE;
		}
		else if(changePassword) {
			cmd = CMD_CHANGEPW;
		}
		else if(getInfo) {
			cmd = CMD_INFO;
		}
		else if(getList) {
			cmd = CMD_LIST;
		}
		else if(unback) {
			cmd = CMD_UNBACK;
		}

		if(restoreSystem) {
			cmd_flags |= CMD_FLAG_RESTORE_SYSTEM_FILES;
		}
		if(restoreSettings) {
			cmd_flags |= CMD_FLAG_RESTORE_SETTINGS;
		}
		if(rebootAfterRestore) {
			cmd_flags |= CMD_FLAG_RESTORE_REBOOT;
		}
		if(copyBeforeRestore) {
			cmd_flags |= CMD_FLAG_RESTORE_COPY_BACKUP;
		}
		if(removeUnusedAfterRestore) {
			cmd_flags |= CMD_FLAG_RESTORE_REMOVE_ITEMS;
		}
		backup_password = strdup(password);
		newpw = strdup(newPassword);
	}

	~iDeviceBackup2(void) {
		m_progressCallback = NULL;
		Cleanup();
	}
	bool ParseArguments(int argc, char** argv);

	bool Runit() {
		return SetupForDeviceInteraction() && StartOperation() && CompleteOperation();
	}
	bool SetupForDeviceInteraction();
	bool StartOperation();
	bool CompleteOperation();
	double GetOverallProgress() { return overall_progress; }
	void SetProgressCallback(progress_t progressCallback) {
		m_progressCallback = progressCallback;
	}
private:
	progress_t m_progressCallback;
	cmd_mode RequestedOperation;
	int Errors;
	void Initialize() {
		Errors = 0;
		ret = IDEVICE_E_UNKNOWN_ERROR;
		udid = NULL;
		source_udid = NULL;
		service = NULL;
		cmd = -1;
		cmd_flags = 0;
		is_full_backup = 0;
		result_code = -1;
		backup_directory = NULL;
		backup_password = NULL;
		newpw = NULL;
		info_plist = NULL;
		opts = NULL;
		mobilebackup2 = NULL;
		lockdown = NULL;
		afc = NULL;
		device = NULL;
		is_encrypted = 0;
		np = NULL;
		willEncrypt = 0;
		lockfile = 0;
		overall_progress = 0;
	}

	void Cleanup() {
		if(backup_password) {
			free(backup_password);
			backup_password = NULL;
		}
		if(newpw) {
			free(newpw);
			newpw = NULL;
		}
		if(backup_directory) {
			free(backup_directory);
			backup_directory = NULL;
		}
		if(udid) {
			free(udid);
			udid = NULL;
		}
		if(device) {
			idevice_free(device);
			device = NULL;
		}

		if (service) {
			lockdownd_service_descriptor_free(service);
			service = NULL;
		}
		if (info_plist) {
			plist_free(info_plist);
			info_plist = NULL;
		}
		if(source_udid) {
			free(source_udid);
			source_udid = NULL;
		}
		if (lockdown) {
			lockdownd_client_free(lockdown);
			lockdown = NULL;
		}
		if (mobilebackup2) {
			mobilebackup2_client_free(mobilebackup2);
			mobilebackup2 = NULL;
		}				
		if (afc) {
			afc_client_free(afc);
			afc = NULL;
		}

		if (np) {
			np_client_free(np);
			np = NULL;
		}
	}

	void ProcessMessage(plist_t message, int *operation_ok);
	void CopyItem(plist_t message, int *errcode, const char *errdesc);
	void RemoveItems(plist_t message, mobilebackup2_error_t *err, int *errcode, const char *errdesc, char *dlmsg);
	void MoveItems(plist_t message, mobilebackup2_error_t *err, char *dlmsg, int *errcode, const char *errdesc);

	// BEGIN
	static void notify_cb(const char *notification, void *userdata);
	void free_dictionary(char **dictionary);
	void mobilebackup_afc_get_file_contents(afc_client_t afc, const char *filename, char **data, uint64_t *size);
	char *str_toupper(char* str);
	int __mkdir(const char* path, int mode);
	int mkdir_with_parents(const char *dir, int mode);
	char* build_path(const char* elem, ...);
	char* format_size_for_display(uint64_t size);
	plist_t mobilebackup_factory_info_plist_new(const char* udid, lockdownd_client_t lockdown, afc_client_t afc);
	void buffer_read_from_filename(const char *filename, char **buffer, uint64_t *length);
	void buffer_write_to_filename(const char *filename, const char *buffer, uint64_t length);
	int plist_read_from_filename(plist_t *plist, const char *filename);
	int plist_write_to_filename(plist_t plist, const char *filename, enum plist_format_t format);
	int mb2_status_check_snapshot_state(const char *path, const char *udid, const char *matches);
	void do_post_notification(idevice_t device, const char *notification);
	void print_progress_real(double progress, int flush);
	virtual void print_progress(uint64_t current, uint64_t total);
	void mb2_set_overall_progress(double progress);
	void mb2_set_overall_progress_from_message(plist_t message, char* identifier);
	void mb2_multi_status_add_file_error(plist_t status_dict, const char *path, int error_code, const char *error_message);
	int errno_to_device_error(int errno_value);
#ifdef WIN32
	int win32err_to_errno(int err_value)
	{
		switch (err_value) {
			case ERROR_FILE_NOT_FOUND:
				return ENOENT;
			case ERROR_ALREADY_EXISTS:
				return EEXIST;
			default:
				return EFAULT;
		}
	}
#endif
	int mb2_handle_send_file(mobilebackup2_client_t mobilebackup2, const char *backup_dir, const char *path, plist_t *errplist);
	void mb2_handle_send_files(mobilebackup2_client_t mobilebackup2, plist_t message, const char *backup_dir);
	int mb2_receive_filename(mobilebackup2_client_t mobilebackup2, char** filename);
	int mb2_handle_receive_files(mobilebackup2_client_t mobilebackup2, plist_t message, const char *backup_dir);
	void mb2_handle_list_directory(mobilebackup2_client_t mobilebackup2, plist_t message, const char *backup_dir);
	void mb2_handle_make_directory(mobilebackup2_client_t mobilebackup2, plist_t message, const char *backup_dir);
	void mb2_copy_file_by_path(const char *src, const char *dst);
	void mb2_copy_directory_by_path(const char *src, const char *dst);
	// END

	double overall_progress;
	idevice_error_t ret;
	char* udid;
	char* source_udid;
	lockdownd_service_descriptor_t service;
	int cmd;
	int cmd_flags;
	int is_full_backup;
	int result_code;
	char* backup_directory;
	char* backup_password;
	char* newpw;
	struct stat st;
	plist_t info_plist;
	plist_t opts;
	mobilebackup2_error_t err;
	mobilebackup2_client_t mobilebackup2;
	lockdownd_client_t lockdown;
	afc_client_t afc;
	idevice_t device;
	uint8_t is_encrypted;
	np_client_t np;
	uint8_t willEncrypt;
	uint64_t lockfile;
};

