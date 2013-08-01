#include "ManagedDeviceBackup2.h"
#include "msclr\marshal.h"

using namespace msclr::interop;

namespace BackupWrapper {

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
			
			_deviceBackup = new iDeviceBackup2(context->marshal_as<const char*>(backupDirectory), false, false, NULL, context->marshal_as<const char*>(udid), false, false, false, false, false, NULL, NULL, NULL, false, false, true);
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
}