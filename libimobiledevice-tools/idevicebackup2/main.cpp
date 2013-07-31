/*
 * idevicebackup2.c
 * Command line interface to use the device's backup and restore service
 *
 * Copyright (c) 2009-2010 Martin Szulecki All Rights Reserved.
 * Copyright (c) 2010      Nikias Bassen All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA 
 */

#include "iDeviceBackup2.h"

/**
 * signal handler function for cleaning up properly
 */
static void clean_exit(int sig)
{
	fprintf(stderr, "Exiting...\n");
	quit_flag++;
}

static void print_usage(int argc, char **argv)
{
	char *name = NULL;
	name = strrchr(argv[0], '/');
	printf("Usage: %s [OPTIONS] CMD [CMDOPTIONS] DIRECTORY\n", (name ? name + 1: argv[0]));
	printf("Create or restore backup from the current or specified directory.\n\n");
	printf("commands:\n");
	printf("  backup\tcreate backup for the device\n");
	printf("  restore\trestore last backup to the device\n");
	printf("    --system\t\trestore system files, too.\n");
	printf("    --reboot\t\treboot the system when done.\n");
	printf("    --copy\t\tcreate a copy of backup folder before restoring.\n");
	printf("    --settings\t\trestore device settings from the backup.\n");
	printf("    --remove\t\tremove items which are not being restored\n");
	printf("    --password PWD\tsupply the password of the source backup\n");
	printf("  info\t\tshow details about last completed backup of device\n");
	printf("  list\t\tlist files of last completed backup in CSV format\n");
	printf("  unback\tunpack a completed backup in DIRECTORY/_unback_/\n");
	printf("  encryption on|off [PWD]\tenable or disable backup encryption\n");
	printf("    NOTE: password will be requested in interactive mode if omitted\n");
	printf("  changepw [OLD NEW]  change backup password on target device\n");
	printf("    NOTE: passwords will be requested in interactive mode if omitted\n");
	printf("\n");
	printf("options:\n");
	printf("  -d, --debug\t\tenable communication debugging\n");
	printf("  -u, --udid UDID\ttarget specific device by its 40-digit device UDID\n");
	printf("  -s, --source UDID\tuse backup data from device specified by UDID\n");
	printf("  -i, --interactive\trequest passwords interactively\n");
	printf("  -h, --help\t\tprints usage information\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	/* we need to exit cleanly on running backups and restores or we cause havok */
	signal(SIGINT, clean_exit);
	signal(SIGTERM, clean_exit);
#ifndef WIN32	
	signal(SIGQUIT, clean_exit);
	signal(SIGPIPE, SIG_IGN);
#else 
	signal(SIGBREAK, clean_exit);
	signal(SIGSEGV, clean_exit);
#endif

	iDeviceBackup2* backuper = new iDeviceBackup2();
	if(!backuper->ParseArguments(argc, argv)) {
		print_usage(argc, argv);
		return -1;
	}

	if(!backuper->SetupForDeviceInteraction()) {
		print_usage(argc, argv);
		return -1;
	}
	if(!backuper->StartOperation()) {
		print_usage(argc, argv);
		return -1;
	}
	if(!backuper->CompleteOperation()) {
		print_usage(argc, argv);
		return -1;
	}
}
