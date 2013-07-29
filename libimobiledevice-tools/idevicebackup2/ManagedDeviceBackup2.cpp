#include "ManagedDeviceBackup2.h"
#include "msclr\marshal.h"

using namespace msclr::interop;

namespace BackupWrapper {

	int ManagedDeviceBackup2::CancelCallback(const char *notification, void *userdata)
	{
		if (!strcmp(notification, NP_SYNC_CANCEL_REQUEST)) {
			// user cancelled
		} else if (!strcmp(notification, NP_BACKUP_DOMAIN_CHANGED)) {
			// wth?
		} else {
			// dunno
		}
		return 0;
	}

	void ManagedDeviceBackup2::Backup(String^ uUid, String^ password, String^ backupDirectoryRoot, Action<Double>^ progressCallback) {
		marshal_context^ context = gcnew marshal_context();
		idevice_error_t ret = IDEVICE_E_UNKNOWN_ERROR;
		int i;
		_progressCallback = progressCallback;
		
		int cmd_flags = 0;
		
		int result_code = -1;
		int interactive_mode = 0;
		struct stat st;
		plist_t opts = NULL;
		mobilebackup2_error_t err;

		try {

			_backup = true;
			if(!String::IsNullOrWhiteSpace(backupDirectoryRoot)) {
				const char* tempRoot = context->marshal_as<const char*>(backupDirectoryRoot);
				backup_directory = new char[strlen(tempRoot) + 1];
				strcpy(backup_directory, tempRoot);
			}

			if(!String::IsNullOrWhiteSpace(uUid)) {
				const char *tempUdid = context->marshal_as<const char*>(uUid);
				_udid = new char[strlen(tempUdid) + 1];
				strcpy(_udid, tempUdid);
			}

			uint64_t lockfile = 0;
			CommonSetup(&lockfile);

			PRINT_VERBOSE(1, "Starting backup...\n");

            /* make sure backup device sub-directory exists */
            char* devbackupdir = build_path(backup_directory, source_udid, NULL);
            __mkdir(devbackupdir, 0755);
            free(devbackupdir);

            if (source_udid &&_udid && strcmp(source_udid,_udid) != 0) {
                /* handle different source backup directory */
                // make sure target backup device sub-directory exists
                devbackupdir = build_path(backup_directory,_udid, NULL);
                __mkdir(devbackupdir, 0755);
                free(devbackupdir);

                // use Info.plist path in target backup folder */
                free(info_path);
                info_path = build_path(backup_directory,_udid, "Info.plist", NULL);
            }

            /* TODO: check domain com.apple.mobile.backup key RequiresEncrypt and WillEncrypt with lockdown */
            /* TODO: verify battery on AC enough battery remaining */	

            /* re-create Info.plist (Device infos, IC-Info.sidb, photos, app_ids, iTunesPrefs) */
            if (info_plist) {
                plist_free(info_plist);
                info_plist = NULL;
            }

			lockdownd_client_t tempLockdown = lockdown;
            info_plist = mobilebackup_factory_info_plist_new(_udid, tempLockdown, afc);
			tempLockdown = NULL;
            remove(info_path);
            plist_write_to_filename(info_plist, info_path, PLIST_FORMAT_XML);
            free(info_path);
			info_path = NULL;

            plist_free(info_plist);
            info_plist = NULL;

            /* request backup from device with manifest from last backup */
            if (willEncrypt) {
                PRINT_VERBOSE(1, "Backup will be encrypted.\n");
            } else {
                PRINT_VERBOSE(1, "Backup will be unencrypted.\n");
            }
            PRINT_VERBOSE(1, "Requesting backup from device...\n");
            err = mobilebackup2_send_request(mobilebackup2, "Backup",_udid, source_udid, NULL);
            if (err == MOBILEBACKUP2_E_SUCCESS) {
                if (is_full_backup) {
                    PRINT_VERBOSE(1, "Full backup mode.\n");
                }	else {
                    PRINT_VERBOSE(1, "Incremental backup mode.\n");
                }
            } else {
				String^ error;
                if (err == MOBILEBACKUP2_E_BAD_VERSION) {
                    printf("ERROR: Could not start backup process: backup protocol version mismatch!\n");
					error = "Could not start backup process: backup protocol version mismatch!";
                } else if (err == MOBILEBACKUP2_E_REPLY_NOT_OK) {
                    printf("ERROR: Could not start backup process: device refused to start the backup process.\n");
					error = "Could not start backup process: device refused to start the backup process.";
                } else {
                    printf("ERROR: Could not start backup process: unspecified error occured\n");
					error = "Could not start backup process: device refused to start the backup process.";
                }
                throw gcnew BackupDeviceException(error);
            }

			FinishOperation();

			if (lockfile) {
				afc_file_lock(afc, lockfile, AFC_LOCK_UN);
				afc_file_close(afc, lockfile);
				lockfile = 0;
				if (_backup)
					do_post_notification(device, NP_SYNC_DID_FINISH);
			}
		}
		catch(Exception^ ex) {
			throw gcnew System::Exception("Unable to complete backup operation. See inner exception for details.", ex);
		}
		catch(...) {
			throw gcnew System::Exception("Unable to complete backup operation. Got some indecipherable native exception.");
		}
		finally {
			Cleanup();
		}
	}

	void ManagedDeviceBackup2::ReportProgress(plist_t message, char* identifier) {
		plist_t node = NULL;
		double progress = 0.0;

		if (!strcmp(identifier, "DLMessageDownloadFiles")) {
			node = plist_array_get_item(message, 3);
		} else if (!strcmp(identifier, "DLMessageUploadFiles")) {
			node = plist_array_get_item(message, 2);
		} else if (!strcmp(identifier, "DLMessageMoveFiles") || !strcmp(identifier, "DLMessageMoveItems")) {
			node = plist_array_get_item(message, 3);
		} else if (!strcmp(identifier, "DLMessageRemoveFiles") || !strcmp(identifier, "DLMessageRemoveItems")) {
			node = plist_array_get_item(message, 3);
		}

		if (node != NULL) {
			plist_get_real_val(node, &progress);
			if(_progressCallback != nullptr) {
				_progressCallback(progress);
			}
		}
	}

	/*
		If this method gets too big, it will result in an Invalid Program exception in Release mode, so keep it small!
	*/
	bool ManagedDeviceBackup2::FinishOperation() {
		/* reset operation success status */
		int operation_ok = 0;
		plist_t message = NULL;
		plist_t node_tmp = NULL;
		char *dlmsg = NULL;
		int file_count = 0;
		int errcode = 0;
		const char *errdesc = NULL;
		struct stat st;
		mobilebackup2_error_t err;
		int result_code;

		/* process series of DLMessage* operations */
		do {
			if (dlmsg) {
				free(dlmsg);
				dlmsg = NULL;
			}

			mobilebackup2_client_t tempClient = mobilebackup2;
			mobilebackup2_receive_message(tempClient, &message, &dlmsg);
			tempClient = NULL;
			if (!message || !dlmsg) {
				PRINT_VERBOSE(1, "Device is not ready yet. Going to try again in 2 seconds...\n");
				sleep(2);
				continue;
			}

			if (!strcmp(dlmsg, "DLMessageDownloadFiles")) {
				/* device wants to download files from the computer */
				ReportProgress(message, dlmsg);
				mb2_handle_send_files(mobilebackup2, message, backup_directory);
			} else if (!strcmp(dlmsg, "DLMessageUploadFiles")) {
				/* device wants to send files to the computer */
				ReportProgress(message, dlmsg);
				file_count += mb2_handle_receive_files(mobilebackup2, message, backup_directory);
			} else if (!strcmp(dlmsg, "DLMessageGetFreeDiskSpace")) {
				/* device wants to know how much disk space is available on the computer */
				uint64_t freespace = 0;
				int res = -1;
	#ifdef WIN32
				if (GetDiskFreeSpaceEx(backup_directory, (PULARGE_INTEGER)&freespace, NULL, NULL)) {
					res = 0;
				}
	#else
				struct statvfs fs;
				memset(&fs, '\0', sizeof(fs));
				res = statvfs(backup_directory, &fs);
				if (res == 0) {
					freespace = (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize;
				}
	#endif
				plist_t freespace_item = plist_new_uint(freespace);
				mobilebackup2_send_status_response(mobilebackup2, res, NULL, freespace_item);
				plist_free(freespace_item);
			} else if (!strcmp(dlmsg, "DLContentsOfDirectory")) {
				/* list directory contents */
				mb2_handle_list_directory(mobilebackup2, message, backup_directory);
			} else if (!strcmp(dlmsg, "DLMessageCreateDirectory")) {
				/* make a directory */
				mb2_handle_make_directory(mobilebackup2, message, backup_directory);
			} else if (!strcmp(dlmsg, "DLMessageMoveFiles") || !strcmp(dlmsg, "DLMessageMoveItems")) {
				/* perform a series of rename operations */
				ReportProgress(message, dlmsg);
				plist_t moves = plist_array_get_item(message, 1);
				uint32_t cnt = plist_dict_get_size(moves);
				PRINT_VERBOSE(1, "Moving %d file%s\n", cnt, (cnt == 1) ? "" : "s");
				plist_dict_iter iter = NULL;
				plist_dict_new_iter(moves, &iter);
				errcode = 0;
				errdesc = NULL;
				if (iter) {
					char *key = NULL;
					plist_t val = NULL;
					do {
						plist_dict_next_item(moves, iter, &key, &val);
						if (key && (plist_get_node_type(val) == PLIST_STRING)) {
							char *str = NULL;
							plist_get_string_val(val, &str);
							if (str) {
								char *newpath = build_path(backup_directory, str, NULL);
								free(str);
								char *oldpath = build_path(backup_directory, key, NULL);

	#ifdef WIN32
								if ((stat(newpath, &st) == 0) && S_ISDIR(st.st_mode))
									RemoveDirectory(newpath);
								else
									DeleteFile(newpath);
	#else
								remove(newpath);
	#endif
								if (rename(oldpath, newpath) < 0) {
									printf("Renaming '%s' to '%s' failed: %s (%d)\n", oldpath, newpath, strerror(errno), errno);
									errcode = errno_to_device_error(errno);
									errdesc = strerror(errno);
									break;
								}
								free(oldpath);
								free(newpath);
							}
							free(key);
							key = NULL;
						}
					} while (val);
					free(iter);
				} else {
					errcode = -1;
					errdesc = "Could not create dict iterator";
					printf("Could not create dict iterator\n");
				}
				plist_t empty_dict = plist_new_dict();
				err = mobilebackup2_send_status_response(mobilebackup2, errcode, errdesc, empty_dict);
				plist_free(empty_dict);
				if (err != MOBILEBACKUP2_E_SUCCESS) {
					printf("Could not send status response, error %d\n", err);
				}
			} else if (!strcmp(dlmsg, "DLMessageRemoveFiles") || !strcmp(dlmsg, "DLMessageRemoveItems")) {
				ReportProgress(message, dlmsg);
				plist_t removes = plist_array_get_item(message, 1);
				uint32_t cnt = plist_array_get_size(removes);
				PRINT_VERBOSE(1, "Removing %d file%s\n", cnt, (cnt == 1) ? "" : "s");
				uint32_t ii = 0;
				errcode = 0;
				errdesc = NULL;
				for (ii = 0; ii < cnt; ii++) {
					plist_t val = plist_array_get_item(removes, ii);
					if (plist_get_node_type(val) == PLIST_STRING) {
						char *str = NULL;
						plist_get_string_val(val, &str);
						if (str) {
							const char *checkfile = strchr(str, '/');
							int suppress_warning = 0;
							if (checkfile) {
								if (strcmp(checkfile+1, "Manifest.mbdx") == 0) {
									suppress_warning = 1;
								}
							}
							char *newpath = build_path(backup_directory, str, NULL);
							free(str);
	#ifdef WIN32
							int res = 0;
							if ((stat(newpath, &st) == 0) && S_ISDIR(st.st_mode))
								res = RemoveDirectory(newpath);
							else
								res = DeleteFile(newpath);
							if (!res) {
								int e = win32err_to_errno(GetLastError());
								if (!suppress_warning)
									printf("Could not remove '%s': %s (%d)\n", newpath, strerror(e), e);
								errcode = errno_to_device_error(e);
								errdesc = strerror(e);
							}
	#else
							if (remove(newpath) < 0) {
								if (!suppress_warning)
									printf("Could not remove '%s': %s (%d)\n", newpath, strerror(errno), errno);
								errcode = errno_to_device_error(errno);
								errdesc = strerror(errno);
							}
	#endif
							free(newpath);
						}
					}
				}
				plist_t empty_dict = plist_new_dict();
				err = mobilebackup2_send_status_response(mobilebackup2, errcode, errdesc, empty_dict);
				plist_free(empty_dict);
				if (err != MOBILEBACKUP2_E_SUCCESS) {
					printf("Could not send status response, error %d\n", err);
				}
			} else if (!strcmp(dlmsg, "DLMessageCopyItem")) {
				CopyItem(message);
			} else if (!strcmp(dlmsg, "DLMessageDisconnect")) {
				break;
			} else if (!strcmp(dlmsg, "DLMessageProcessMessage")) {
				ProcessMessage(message, &result_code);

				break;
			}

			if (message)
				plist_free(message);
			message = NULL;

		} while (1);

		
		if(node_tmp) {
			plist_free(node_tmp);
			node_tmp = NULL;
		}

		//if(dlmsg) {
		//	free(dlmsg);
		//	dlmsg = NULL;
		//}

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

		idevice_free(device);
		device = NULL;

		if (_udid) {
			free(_udid);
			_udid = NULL;
		}
		if (source_udid) {
			free(source_udid);
			source_udid = NULL;
		}

		return result_code == 0;
	}

	void ManagedDeviceBackup2::CopyItem(plist_t message) {
		plist_t srcpath = plist_array_get_item(message, 1);
		plist_t dstpath = plist_array_get_item(message, 2);
		struct stat st;

		int errcode = 0;
		const char* errdesc = NULL;
		if ((plist_get_node_type(srcpath) == PLIST_STRING) && (plist_get_node_type(dstpath) == PLIST_STRING)) {
			char *src = NULL;
			char *dst = NULL;
			plist_get_string_val(srcpath, &src);
			plist_get_string_val(dstpath, &dst);
			if (src && dst) {
				char *oldpath = build_path(backup_directory, src, NULL);
				char *newpath = build_path(backup_directory, dst, NULL);

				PRINT_VERBOSE(1, "Copying '%s' to '%s'\n", src, dst);

				/* check that src exists */
				if ((stat(oldpath, &st) == 0) && S_ISDIR(st.st_mode)) {
					mb2_copy_directory_by_path(oldpath, newpath);
				} else if ((stat(oldpath, &st) == 0) && S_ISREG(st.st_mode)) {
					mb2_copy_file_by_path(oldpath, newpath);
				}

				free(newpath);
				free(oldpath);
			}
			free(src);
			free(dst);
		}
		plist_t empty_dict = plist_new_dict();
		mobilebackup2_error_t err = mobilebackup2_send_status_response(mobilebackup2, errcode, errdesc, empty_dict);
		plist_free(empty_dict);
		if (err != MOBILEBACKUP2_E_SUCCESS) {
			printf("Could not send status response, error %d\n", err);
		}
	}

	void ManagedDeviceBackup2::ProcessMessage(plist_t message, int *result_code) {
		plist_t node_tmp = plist_array_get_item(message, 1);
		if (plist_get_node_type(node_tmp) != PLIST_DICT) {
			printf("Unknown message received!\n");
		}
		plist_t nn;
		int error_code = -1;
		nn = plist_dict_get_item(node_tmp, "ErrorCode");
		if (nn && (plist_get_node_type(nn) == PLIST_UINT)) {
			uint64_t ec = 0;
			plist_get_uint_val(nn, &ec);
			error_code = (uint32_t)ec;
			if (error_code == 0) {
				*result_code = 0;
			} else {
				*result_code = -error_code;
			}
		}
		nn = plist_dict_get_item(node_tmp, "ErrorDescription");
		char *str = NULL;
		if (nn && (plist_get_node_type(nn) == PLIST_STRING)) {
			plist_get_string_val(nn, &str);
		}
		if (error_code != 0) {
			if (str) {
				printf("ErrorCode %d: %s\n", error_code, str);
			} else {
				printf("ErrorCode %d: (Unknown)\n", error_code);
			}
		}
		if (str) {
			free(str);
		}
		nn = plist_dict_get_item(node_tmp, "Content");
		if (nn && (plist_get_node_type(nn) == PLIST_STRING)) {
			str = NULL;
			plist_get_string_val(nn, &str);
			PRINT_VERBOSE(1, "Content:\n");
			printf("%s", str);
			free(str);
		}
	}

	void ManagedDeviceBackup2::CommonSetup(uint64_t* lockfile) {
		marshal_context^ context = gcnew marshal_context();
		struct stat st;
		idevice_error_t ret = IDEVICE_E_UNKNOWN_ERROR;
		plist_t node_tmp = NULL;


		if (_changePassword) {
			backup_directory = strdup(".this_folder_is_not_present_on_purpose");
		} else {
			if (backup_directory == NULL) {
				throw gcnew System::IO::DirectoryNotFoundException("A backup directory was not specfied");
			}

			/* verify if passed backup directory exists */
			if (stat(backup_directory, &st) != 0) {
				printf("ERROR: Backup directory \"%s\" does not exist!\n", backup_directory);
				String^ backup = context->marshal_as<String^>(backup_directory);
				throw gcnew System::IO::DirectoryNotFoundException(String::Format("Directory {0} does not exist on the system", backup));
			}
		}

		idevice_t tempDevice;
		if(_udid) {
			ret = idevice_new(&tempDevice,_udid);
			if (ret != IDEVICE_E_SUCCESS) {
				throw gcnew NoDeviceFoundException(); 
			}
			
		}
		else {
			ret = idevice_new(&tempDevice, NULL);
			if (ret != IDEVICE_E_SUCCESS) {
				printf("No device found, is it plugged in?\n");
				throw gcnew NoDeviceFoundException();
			}

			char *udid;
			idevice_get_udid(tempDevice, &udid);
			_udid = udid;
		}
		device = tempDevice;
		tempDevice = NULL;

		source_udid = strdup(_udid);

		uint8_t is_encrypted = 0;
			
		if (_changePassword) {
			if (!backup_password || !new_password) {
				printf("ERROR: Can't get password input in non-interactive mode. Either pass password(s) on the command line, or enable interactive mode with -i or --interactive.\n");
				throw gcnew BadPasswordException();
			}
		} else {
			/* backup directory must contain an Info.plist */
			info_path = build_path(backup_directory, source_udid, "Info.plist", NULL);
			if (_restore) {
				if (stat(info_path, &st) != 0) {
					free(info_path);
					info_path = NULL;
					printf("ERROR: Backup directory \"%s\" is invalid. No Info.plist found for UDID %s.\n", backup_directory, source_udid);
					throw gcnew NoInfoPlistFoundException();
				}
				char* manifest_path = build_path(backup_directory, source_udid, "Manifest.plist", NULL);
				if (stat(manifest_path, &st) != 0) {
					free(info_path);
					info_path = NULL;
				}
				plist_t manifest_plist = NULL;
				plist_read_from_filename(&manifest_plist, manifest_path);
				if (!manifest_plist) {
					free(info_path);
					info_path = NULL;
					free(manifest_path);
					printf("ERROR: Backup directory \"%s\" is invalid. No Manifest.plist found for UDID %s.\n", backup_directory, source_udid);
					throw gcnew NoManifestPlistFoundException();
				}
				node_tmp = plist_dict_get_item(manifest_plist, "IsEncrypted");
				if (node_tmp && (plist_get_node_type(node_tmp) == PLIST_BOOLEAN)) {
					plist_get_bool_val(node_tmp, &is_encrypted);
				}
				plist_free(manifest_plist);
				free(manifest_path);
			}
			PRINT_VERBOSE(1, "Backup directory is \"%s\"\n", backup_directory);
		}

		if (is_encrypted) {
			PRINT_VERBOSE(1, "This is an encrypted backup.\n");
			if (!backup_password || (strlen(backup_password) == 0)) {
				if (backup_password) {
					free(backup_password);
				}
				idevice_free(device);
				printf("ERROR: a backup password is required to restore an encrypted backup. Cannot continue.\n");
				throw gcnew BadPasswordException();
			}
		}
		lockdownd_client_t tempLockdown;
		if (LOCKDOWN_E_SUCCESS != lockdownd_client_new_with_handshake(device, &tempLockdown, "idevicebackup")) {
			idevice_free(device);
			throw gcnew ErrorCommunicatingWithDeviceException();
		}

		lockdown = tempLockdown;

		/* start notification_proxy */
		lockdownd_service_descriptor_t tempService2 = NULL;
		ret = lockdownd_start_service(tempLockdown, NP_SERVICE_NAME, &tempService2);
		service = tempService2;
		tempLockdown = NULL;

		if ((ret == LOCKDOWN_E_SUCCESS) && service && service->port) {
			np_client_t tempNp;
			np_client_new(device, service, &tempNp);
			np = tempNp; // to free when we're all done

			// convert our managed delegate to an unmanaged function pointer
			IntPtr ip = Marshal::GetFunctionPointerForDelegate(cancelTask);
			np_notify_cb_t nativeCb = static_cast<np_notify_cb_t>(ip.ToPointer());

			np_set_notify_callback(tempNp, nativeCb, NULL);
			const char *noties[5] = {
				NP_SYNC_CANCEL_REQUEST,
				NP_SYNC_SUSPEND_REQUEST,
				NP_SYNC_RESUME_REQUEST,
				NP_BACKUP_DOMAIN_CHANGED,
				NULL
			};
			np_observe_notifications(tempNp, noties);
			tempNp = NULL;
		} else {
			printf("ERROR: Could not start service %s.\n", NP_SERVICE_NAME);
			throw gcnew UnableToStartServiceException(NP_SERVICE_NAME);
		}

		if (_backup) {
			/* start AFC, we need this for the lock file */
			service->port = 0;
			service->ssl_enabled = 0;
			tempService2 = service;
			ret = lockdownd_start_service(lockdown, "com.apple.afc", &tempService2);
			if ((ret == LOCKDOWN_E_SUCCESS) && service->port) {
				afc_client_t tempAfc;
				afc_client_new(device, service, &tempAfc);
				afc = tempAfc;
				tempAfc = NULL;
			}
		}

		if (service) {
			lockdownd_service_descriptor_free(service);
			service = NULL;
			tempService2 = NULL;
		}

		/* start mobilebackup service and retrieve port */
		ret = lockdownd_start_service(lockdown, MOBILEBACKUP2_SERVICE_NAME, &tempService2);
		service = tempService2;
		if ((ret == LOCKDOWN_E_SUCCESS) && service && service->port) {
			PRINT_VERBOSE(1, "Started \"%s\" service on port %d.\n", MOBILEBACKUP2_SERVICE_NAME, service->port);
			mobilebackup2_client_t tempClient = NULL;
			mobilebackup2_client_new(device, service, &tempClient);
			mobilebackup2 = tempClient;
			tempClient = NULL;

			if (service) {
				lockdownd_service_descriptor_free(service);
				service = NULL;
				tempService2 = NULL;
			}

			/* send Hello message */
			double local_versions[2] = {2.0, 2.1};
			double remote_version = 0.0;
			mobilebackup2_error_t err = mobilebackup2_version_exchange(mobilebackup2, local_versions, 2, &remote_version);
			if (err != MOBILEBACKUP2_E_SUCCESS) {
				printf("Could not perform backup protocol version exchange, error code %d\n", err);
				throw gcnew UnableToPerformBackupProtocalExchangeException();
			}

			PRINT_VERBOSE(1, "Negotiated Protocol Version %.1f\n", remote_version);

			/* verify existing Info.plist */
			if (info_path && (stat(info_path, &st) == 0)) {
				PRINT_VERBOSE(1, "Reading Info.plist from backup.\n");
				plist_t tempInfo = NULL;
				plist_read_from_filename(&tempInfo, info_path);
				info_plist = tempInfo;

				if (!info_plist) {
					printf("Could not read Info.plist\n");
					is_full_backup = true;
				}
			} else {
				if (_restore) {
					printf("Aborting restore. Info.plist is missing.\n");
					throw gcnew NoInfoPlistFoundException();
				} else {
					is_full_backup = true;
				}
			}

			if (_backup) {
				do_post_notification(device, NP_SYNC_WILL_START);
				afc_file_open(afc, "/com.apple.itunes.lock_sync", AFC_FOPEN_RW, lockfile);
			}
			if (lockfile && *lockfile) {
				afc_error_t aerr;
				do_post_notification(device, NP_SYNC_LOCK_REQUEST);
				int i = 0;
				for (; i < LOCK_ATTEMPTS; i++) {
					aerr = afc_file_lock(afc, *lockfile, AFC_LOCK_EX);
					if (aerr == AFC_E_SUCCESS) {
						do_post_notification(device, NP_SYNC_DID_START);
						break;
					} else if (aerr == AFC_E_OP_WOULD_BLOCK) {
						sleep(LOCK_WAIT);
						continue;
					} else {
						fprintf(stderr, "ERROR: could not lock file! error code: %d\n", aerr);
						afc_file_close(afc, *lockfile);
						lockfile = 0;
						throw gcnew ErrorCommunicatingWithDeviceException();
					}
				}
				if (i == LOCK_ATTEMPTS) {
					fprintf(stderr, "ERROR: timeout while locking for sync\n");
					afc_file_close(afc, *lockfile);
					*lockfile = 0;
					throw gcnew ErrorCommunicatingWithDeviceException();
				}
			}

			uint8_t tempWillEncrypt = 0;
			node_tmp = NULL;
			lockdownd_get_value(lockdown, "com.apple.mobile.backup", "WillEncrypt", &node_tmp);
			if (node_tmp) {
				if (plist_get_node_type(node_tmp) == PLIST_BOOLEAN) {
					plist_get_bool_val(node_tmp, &tempWillEncrypt);
				}
				willEncrypt = tempWillEncrypt;
			}
		}

		if(node_tmp) {
			plist_free(node_tmp);
			node_tmp = NULL;
		}

		delete context;
	}
}