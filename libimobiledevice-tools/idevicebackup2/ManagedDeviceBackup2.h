#include "idevicebackup2.h"

using namespace System;
using namespace System::Runtime::InteropServices;

namespace BackupWrapper {

	[Serializable]
	public ref class NoDeviceFoundException : public Exception 
	{
	public:
		NoDeviceFoundException() : Exception("No device detected.  Plugged in and turned on?") {}
		NoDeviceFoundException(Exception^ ex) : Exception("No device detected.  Plugged in and turned on?", ex) {}
	};

	[Serializable]
	public ref class BadPasswordException : public Exception 
	{
	public:
		BadPasswordException() : Exception("The password(s) supplied are invalid.") {}
		BadPasswordException(Exception^ ex) : Exception("The password(s) supplied are invalid.", ex) {}
	};

	[Serializable]
	public ref class NoInfoPlistFoundException : public Exception 
	{
	public:
		NoInfoPlistFoundException() : Exception("There was no info.plist at the specified directory. Are you sure that's a backup location for the connected device?") {}
		NoInfoPlistFoundException(Exception^ ex) : Exception("There was no info.plist at the specified directory. Are you sure that's a backup location for the connected device?", ex) {}
	};

	[Serializable]
	public ref class NoManifestPlistFoundException : public Exception 
	{
	public:
		NoManifestPlistFoundException() : Exception("There was no manifest.plist at the specified directory. Are you sure that's a backup location for the connected device?") {}
		NoManifestPlistFoundException(Exception^ ex) : Exception("There was no manifest.plist at the specified directory. Are you sure that's a backup location for the connected device?", ex) {}
	};

	[Serializable]
	public ref class UnableToStartServiceException : public Exception 
	{
	public:
		UnableToStartServiceException(String^ message) : Exception(String::Format("Unable to start service {0}.", message)) {}
		UnableToStartServiceException(String^ message, Exception^ ex) : Exception(String::Format("Unable to start service {0}.", message), ex) {}
	};

	[Serializable]
	public ref class UnableToPerformBackupProtocalExchangeException : public Exception 
	{
	public:
		UnableToPerformBackupProtocalExchangeException() : Exception("Unable to perform backup protocol version exchange") {}
		UnableToPerformBackupProtocalExchangeException(Exception^ ex) : Exception("Unable to perform backup protocol version exchange", ex) {}
	};

	[Serializable]
	public ref class ErrorCommunicatingWithDeviceException : public Exception 
	{
	public:
		ErrorCommunicatingWithDeviceException() : Exception("Unable to communicate with the device") {}
		ErrorCommunicatingWithDeviceException(Exception^ ex) : Exception("Unable to communicate with the device", ex) {}
	};

	[Serializable]
	public ref class BackupDeviceException : public Exception 
	{
	public:
		BackupDeviceException(String^ message) : Exception(message) {}
		BackupDeviceException(String^ message, Exception^ ex) : Exception(message, ex) {}
	};

	public delegate int CancelTaskCallBack(const char *notification, void *userdata);

	///<summary>
	///Wrapper around all the unamanged code that does all the hard work of backup/restore from/to an iOS device
	///</summary>
	public ref class ManagedDeviceBackup2
	{
		protected: 
			!ManagedDeviceBackup2() {
					Cleanup();
				}
		public:
			enum class CancellationType { UserCancelled, BackupDomainChanged, Unhandled };

			static property Int32^ DebugLevel { Int32^ get() { return _debugLevel; } void set(Int32^ val) { _debugLevel = val; } }

			ManagedDeviceBackup2() {
				_changePassword = false;
				_udid = NULL;
				backup_directory = NULL;
				backup_password = NULL;
				device = NULL;
				service = NULL;
				info_plist = NULL;
				source_udid = NULL;
				info_path = NULL; 
				lockdown = NULL;
				afc = NULL;
				is_full_backup = false;
				_restore = false;
				_backup = false;
				new_password = NULL;
				mobilebackup2 = NULL;
				willEncrypt = 0;
				np = NULL;
				cancelTask = gcnew CancelTaskCallBack(this, &ManagedDeviceBackup2::CancelCallback);
				_progressCallback = nullptr;
				is_encrypted = 0;
			}

			~ManagedDeviceBackup2() {
				Cleanup();
			}

			void Cleanup() {
				if(backup_password) {
					free(backup_password);
					backup_password = NULL;
				}
				if(new_password) {
					free(new_password);
					new_password = NULL;
				}
				if(backup_directory) {
					free(backup_directory);
					backup_directory = NULL;
				}
				if(_udid) {
					free(_udid);
					_udid = NULL;
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
				if(info_path) {
					free(info_path);
					info_path = NULL;
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
				cancelTask = nullptr;
				_progressCallback = nullptr;
			}
			
			void Backup(
				[System::Runtime::InteropServices::Optional]
				String^ uUid,
				[System::Runtime::InteropServices::Optional]
				String^ password,
				[System::Runtime::InteropServices::Optional]
				String^ backupDirectoryRoot,
				[System::Runtime::InteropServices::Optional]
				Action<Double>^ progressCallback);

			void Restore(
				String^ backupDirectory,
				[System::Runtime::InteropServices::Optional]
				String^ sourceUuid,
				[System::Runtime::InteropServices::Optional]
				String^ password,
				[System::Runtime::InteropServices::Optional]
				Boolean^ copyFirst, 
				[System::Runtime::InteropServices::Optional]
				Boolean^ rebootWhenDone, 
				[System::Runtime::InteropServices::Optional]
				Boolean^ removeItemsNotRestored, 
				[System::Runtime::InteropServices::Optional]
				Boolean^ restoreSystemFiles, 
				[System::Runtime::InteropServices::Optional]
				Action<Double>^ progressCallback,
				[System::Runtime::InteropServices::Optional]
				Boolean^ restoreSettings);

			Boolean^ ChangePassword(String^ currentPassword, String^ newPassword) { return false; }
			//TODO: make into a property
			String^ LastBackupDetails() { return nullptr; }
		private:
			static Int32^ _debugLevel = 0;
			void CommonSetup(uint64_t* lockfile);
			bool FinishOperation();
			void ReportProgress(plist_t message, char* identifier);
			void ProcessMessage(plist_t message, int* error_code);
			void CopyItem(plist_t message);
			void RemoveItem(plist_t message, char *dlmsg);
			int CancelCallback(const char *notification, void *userdata);
			CancelTaskCallBack^ cancelTask;

			Action<Double>^ _progressCallback;
			bool _changePassword;
			bool _restore;
			char* backup_password;
			char* new_password;
			char* backup_directory;
			char* _udid;
			idevice_t device;
			lockdownd_service_descriptor_t service;
			plist_t info_plist;
			char* source_udid;
			char* info_path;
			lockdownd_client_t lockdown;
			afc_client_t afc;
			bool is_full_backup;
			bool _backup;
			uint8_t is_encrypted;
			mobilebackup2_client_t mobilebackup2;
			uint8_t willEncrypt;
			np_client_t np;
	};
}