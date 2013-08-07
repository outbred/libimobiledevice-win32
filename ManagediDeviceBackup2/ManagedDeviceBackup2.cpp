#include "ManagedDeviceBackup2.h"
#include "msclr\marshal.h"

using namespace msclr::interop;

namespace BackupWrapper {

	System::Collections::Generic::List<String^>^ ManagedDeviceBackup2::GetConnectedDevicesUdids() {
		System::Collections::Generic::List<String^>^ result = gcnew System::Collections::Generic::List<String^>();
		marshal_context^ context = gcnew marshal_context();
		char **dev_list = NULL;
		int count = 0;
		if (idevice_get_device_list(&dev_list, &count) < 0) {
			fprintf(stderr, "ERROR: Unable to retrieve device list!\n");
			return nullptr;
		}
		for (int i = 0; i < count; i++) {
			printf("%s\n", dev_list[i]);
			result->Add(context->marshal_as<String^>(dev_list[i]));
		}
		idevice_device_list_free(dev_list);
		return result;
	}

	bool ManagedDeviceBackup2::HasConnectedDevice() {
		char **dev_list = NULL;
		int count = 0;
		if (idevice_get_device_list(&dev_list, &count) < 0) {
			fprintf(stderr, "ERROR: Unable to retrieve device list!\n");
			return false;
		}
		return count > 0;
	}

	String^ ManagedDeviceBackup2::UnbackBackup(String^ backupDirectory, String^ udid) {
		if(String::IsNullOrWhiteSpace(backupDirectory)) {
			throw gcnew ArgumentNullException("backupDirectory");
		}

		if(!IO::Directory::Exists(backupDirectory)) {
			throw gcnew IO::DirectoryNotFoundException(String::Format("Directory {0} does not exist.", backupDirectory));
		}

		String^ unbackDir = IO::Path::Combine(backupDirectory, "_unback_");
		if(IO::Directory::Exists(unbackDir)) {
			throw gcnew ArgumentException(String::Format("{0} already exists. Aborting unback.", unbackDir));
		}

		try {
			marshal_context^ context = gcnew marshal_context();
			Initialize();
			
			_deviceBackup = new iDeviceBackup2(context->marshal_as<const char*>(backupDirectory), false, false, context->marshal_as<const char*>(udid),
				context->marshal_as<const char*>(udid), false, false, false, false, false, NULL, NULL, NULL, false, false, true);
			_deviceBackup->Runit();

			delete context;
			if(IO::Directory::Exists(unbackDir)) {
				return unbackDir;
			}

			throw gcnew Exception("Unable to unback backup.  Unknown exception.");
		}
		catch(...) {
			throw gcnew Exception("Got a native exception.  Badness.");
		}
		finally {
			Cleanup();
			DecodeErrors();
		}
	}

	// TODO: return a managed class that represents all this serialized info
	String^ ManagedDeviceBackup2::GetInfoFromLastBackup(String^ backupDirectory) {
		marshal_context^ context = gcnew marshal_context();
		if(String::IsNullOrWhiteSpace(backupDirectory)) {
			throw gcnew ArgumentNullException("backupDirectory");
		}

		if(!IO::Directory::Exists(backupDirectory)) {
			throw gcnew IO::DirectoryNotFoundException(String::Format("Directory {0} does not exist.", backupDirectory));
		}

		try {
			Initialize();
			
			_deviceBackup = new iDeviceBackup2(context->marshal_as<const char*>(backupDirectory), false, false, NULL, NULL, false, false, false, false, false, NULL, NULL, NULL, false, true, false);
			_deviceBackup->Runit();
			const char *info = _deviceBackup->GetInfo();
			return context->marshal_as<String^>(info);

		}
		catch(...) {
			throw gcnew Exception("Got a native exception.  Badness.");
			return nullptr;
		}
		finally {
			delete context;
			Cleanup();
			DecodeErrors();
		}
	}

	void ManagedDeviceBackup2::Backup(String^ uUid, String^ password, String^ backupDirectoryRoot, Action<Double, Double>^ progressCallback) {
		if(String::IsNullOrWhiteSpace(backupDirectoryRoot)) {
			throw gcnew ArgumentNullException("backupDirectory");
		}

		if(!IO::Directory::Exists(backupDirectoryRoot)) {
			throw gcnew IO::DirectoryNotFoundException(String::Format("Directory {0} does not exist.", backupDirectoryRoot));
		}


		try {
			Initialize();

			this->_progressCallback = progressCallback;
			marshal_context^ context = gcnew marshal_context();
			
			_deviceBackup = new iDeviceBackup2(context->marshal_as<const char*>(backupDirectoryRoot), true, false, context->marshal_as<const char*>(uUid), NULL, false,
				false, false, false, false, context->marshal_as<const char*>(password));
			_deviceBackup->SetProgressCallback(this->GetProgressCallback());
			_deviceBackup->Runit();

			delete context;
		}
		catch(...) {
			throw gcnew Exception("Got a native exception.  Badness.");
		}
		finally {
			Cleanup();
			DecodeErrors();
		}
	}

	void ManagedDeviceBackup2::Restore(String^ backupDirectory, String^ sourceUuid, String^ password, Boolean^ copyFirst, Boolean^ rebootWhenDone, 
		Boolean^ removeItemsNotRestored, Boolean^ restoreSystemFiles, Action<Double, Double>^ progressCallback, Boolean^ restoreSettings) {

		if(String::IsNullOrWhiteSpace(backupDirectory)) {
			throw gcnew ArgumentNullException("backupDirectory");
		}

		if(!IO::Directory::Exists(backupDirectory)) {
			throw gcnew IO::DirectoryNotFoundException(String::Format("Directory {0} does not exist.", backupDirectory));
		}

		try {
			Initialize();
			this->_progressCallback = progressCallback;
			marshal_context^ context = gcnew marshal_context();
			
			_deviceBackup = new iDeviceBackup2(context->marshal_as<const char*>(backupDirectory), false, true, context->marshal_as<const char*>(sourceUuid), NULL, 
				*restoreSystemFiles, *rebootWhenDone, *copyFirst, *restoreSettings, *removeItemsNotRestored, context->marshal_as<const char*>(password));
			_deviceBackup->SetProgressCallback(this->GetProgressCallback());
			_deviceBackup->Runit();

			delete context;
		}
		catch(...) {
			throw gcnew Exception("Got a native exception.  Badness.");
		}
		finally {
			Cleanup();
			DecodeErrors();
		}
	}

	void ManagedDeviceBackup2::DecodeErrors() {
		if(Errors == 0) {
			return; 
		}

		if(iDeviceBackup2::HasError(ErrorFlags::USER_CANCELED, Errors)) {
			return;
		}

		if(iDeviceBackup2::HasError(ErrorFlags::MISSING_PASSWORD_FOR_ENCRYPTED_BACKUP, Errors)) {
			throw gcnew BadPasswordException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::MISSING_PASSWORDS_FOR_CHANGE_PASSWORDS, Errors)) {
			throw gcnew BadPasswordException();
		}
			
		if(iDeviceBackup2::HasError(ErrorFlags::NO_BACKUP_DIRECTORY, Errors)) {
			throw gcnew System::IO::DirectoryNotFoundException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::BACKUP_DIRECTORY_DNE, Errors)) {
			throw gcnew System::IO::DirectoryNotFoundException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::MISSING_INFO_PLIST, Errors)) {
			throw gcnew NoInfoPlistFoundException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::MISSING_MANIFEST_PLIST, Errors)) {
			throw gcnew NoManifestPlistFoundException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::CANNOT_CONNECT_TO_DEVICE, Errors)) {
			throw gcnew ErrorCommunicatingWithDeviceException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::COULD_NOT_START_NOTIFICATION_PROXY, Errors)) {
			throw gcnew UnableToStartServiceException("Notification Proxy");
		}
		if(iDeviceBackup2::HasError(ErrorFlags::COULD_NOT_START_AFC, Errors)) {
			throw gcnew UnableToStartServiceException("AFC");
		}
		if(iDeviceBackup2::HasError(ErrorFlags::PROTOCOL_VERSION_EXCHANGE_FAILED, Errors)) {
			throw gcnew UnableToPerformBackupProtocalExchangeException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::UNABLE_TO_LOCK_SYNC, Errors)) {
			throw gcnew ErrorCommunicatingWithDeviceException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::PROTOCOL_VERSION_MISMATCH, Errors)) {
			throw gcnew UnableToPerformBackupProtocalExchangeException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::UNABLE_TO_LOCK_SYNC, Errors)) {
			throw gcnew ErrorCommunicatingWithDeviceException();
		}
		if(iDeviceBackup2::HasError(ErrorFlags::DEVICE_ERROR, Errors)) {
			throw gcnew DeviceErrorException();
		}

		// If we get here...we got an error, but it's unclear what it is
		throw gcnew Exception("Unable to complete the requested operation.");
	}

	// TODO: ?  Requires developer disk image to be mounted first...
	bool ManagedDeviceBackup2::GetScreenshotofDevice(String^ backupDirectory, String^ udid, [Out] String^% filePath ) {
		idevice_t device = NULL;
		lockdownd_client_t lckd = NULL;
		screenshotr_client_t shotr = NULL;
		lockdownd_service_descriptor_t service = NULL;
		int i;
		marshal_context^ context = gcnew marshal_context();
		filePath = nullptr;

		try {
			if (IDEVICE_E_SUCCESS != idevice_new(&device, context->marshal_as<const char*>(udid))) {
				throw gcnew NoDeviceFoundException();
			}

			if (LOCKDOWN_E_SUCCESS != lockdownd_client_new_with_handshake(device, &lckd, NULL)) {
				idevice_free(device);
				throw gcnew ErrorCommunicatingWithDeviceException();
			}

			lockdownd_start_service(lckd, "com.apple.mobile.screenshotr", &service);
			lockdownd_client_free(lckd);
			if (service->port > 0) {
				if (screenshotr_client_new(device, service, &shotr) != SCREENSHOTR_E_SUCCESS) {
					printf("Could not connect to screenshotr!\n");
				} else {
					char *imgdata = NULL;
					char filename[36];
					uint64_t imgsize = 0;
					time_t now = time(NULL);
					strftime(filename, 36, "screenshot-%Y-%m-%d-%H-%M-%S.tiff", gmtime(&now));
					filePath = Path::Combine(backupDirectory, context->marshal_as<String^>(filename));
					if (screenshotr_take_screenshot(shotr, &imgdata, &imgsize) == SCREENSHOTR_E_SUCCESS) {
						FILE *f = fopen(context->marshal_as<const char*>(filePath), "wb");
						if (f) {
							if (fwrite(imgdata, 1, (size_t)imgsize, f) == (size_t)imgsize) {
								printf("Screenshot saved to %s\n", filename);
								return System::IO::File::Exists(filePath);
							} else {
								printf("Could not save screenshot to file %s!\n", filename);
							}
							fclose(f);
						} else {
							printf("Could not open %s for writing: %s\n", filename, strerror(errno));
						}
					} else {
						printf("Could not get screenshot!\n");
					}
					screenshotr_client_free(shotr);
				}
			} else {
				printf("Could not start screenshotr service! Remember that you have to mount the Developer disk image on your device if you want to use the screenshotr service.\n");
			}
			return false;
		}
		finally {
			if (service) {
				lockdownd_service_descriptor_free(service);
			}

			idevice_free(device);
		}
	}

	// BEGIN DeviceInfo
	bool^ ManagedDeviceBackup2::GetInfoForConnectedDevice(String^ udid, String^ locationForPlist, bool^ simple) {
		idevice_t device;
		lockdownd_client_t client = NULL;
		int format = FORMAT_KEY_VALUE;
		char *domain = NULL;
		char *key = NULL;
		char *xml_doc = NULL;
		uint32_t xml_length;
		plist_t node = NULL;
		plist_type node_type;
		marshal_context^ context = gcnew marshal_context();

		try {
			if(String::IsNullOrWhiteSpace(locationForPlist)) {
				throw gcnew ArgumentNullException("locationForPlist");
			}
			if(!System::IO::Directory::Exists(System::IO::Path::GetDirectoryName(locationForPlist))) {
				throw gcnew System::IO::DirectoryNotFoundException(System::IO::Path::GetDirectoryName(locationForPlist));
			}
			if(System::IO::File::Exists(locationForPlist)) {
				// TODO: throw a FileExists exception
				throw gcnew System::IO::FileLoadException(locationForPlist);
			}


			idevice_error_t ret = idevice_new(&device, context->marshal_as<const char*>(udid));
			if (ret != IDEVICE_E_SUCCESS) {
				throw gcnew NoDeviceFoundException();
			}

			if (LOCKDOWN_E_SUCCESS != (*simple ?
					lockdownd_client_new(device, &client, "ideviceinfo"):
					lockdownd_client_new_with_handshake(device, &client, "ideviceinfo"))) {
				idevice_free(device);
				throw gcnew ErrorCommunicatingWithDeviceException();
			}

			/* run query and output information */
			if(lockdownd_get_value(client, domain, key, &node) == LOCKDOWN_E_SUCCESS) {
				if (node) {
					iDeviceBackup2::plist_write_to_filename(node, context->marshal_as<const char*>(locationForPlist), PLIST_FORMAT_XML);
					return System::IO::File::Exists(locationForPlist);
				}
			}
		}
		finally {
			if(node) {
				plist_free(node);
				node = NULL;
			}
			if (domain != NULL) {
				free(domain);
				domain = NULL;
			}
			if (client) {
				lockdownd_client_free(client);
				client = NULL;
			}
			if (device) {
				idevice_free(device);
				device = NULL;
			}
		}
		return false;
	}

	char *ManagedDeviceBackup2::base64encode(const unsigned char *buf, size_t size)
	{
		if (!buf || !(size > 0)) return NULL;
		int outlen = (size / 3) * 4;
		char *outbuf = (char*)malloc(outlen+5); // 4 spare bytes + 1 for '\0'
		size_t n = 0;
		size_t m = 0;
		unsigned char input[3];
		unsigned int output[4];
		while (n < size) {
			input[0] = buf[n];
			input[1] = (n+1 < size) ? buf[n+1] : 0;
			input[2] = (n+2 < size) ? buf[n+2] : 0;
			output[0] = input[0] >> 2;
			output[1] = ((input[0] & 3) << 4) + (input[1] >> 4);
			output[2] = ((input[1] & 15) << 2) + (input[2] >> 6);
			output[3] = input[2] & 63;
			outbuf[m++] = base64_str[(int)output[0]];
			outbuf[m++] = base64_str[(int)output[1]];
			outbuf[m++] = (n+1 < size) ? base64_str[(int)output[2]] : base64_pad;
			outbuf[m++] = (n+2 < size) ? base64_str[(int)output[3]] : base64_pad;
			n+=3;
		}
		outbuf[m] = 0; // 0-termination!
		return outbuf;
	}

	int ManagedDeviceBackup2::is_domain_known(char *domain)
	{
		int i = 0;
		while (domains[i] != NULL) {
			if (strstr(domain, domains[i++])) {
				return 1;
			}
		}
		return 0;
	}

	// END DeviceInfo
}