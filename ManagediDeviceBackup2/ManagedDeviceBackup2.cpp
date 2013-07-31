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

	void ManagedDeviceBackup2::Backup(String^ uUid, String^ password, String^ backupDirectoryRoot, Action<Double, Double>^ progressCallback) {
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
}