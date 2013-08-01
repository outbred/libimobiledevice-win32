#include "iDeviceBackup2.h"

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
	public ref class DeviceErrorException : public Exception 
	{
	public:
		DeviceErrorException() : Exception("Error communicating with device.") {}
		DeviceErrorException(Exception^ ex) : Exception("Error communicating with device.", ex) {}
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

	[Serializable]
	public ref class RestoreDeviceException : public Exception 
	{
	public:
		RestoreDeviceException(String^ message) : Exception(message) {}
		RestoreDeviceException(String^ message, Exception^ ex) : Exception(message, ex) {}
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
			ManagedDeviceBackup2() {
				Initialize();
			}

			~ManagedDeviceBackup2() {
				Cleanup();
			}

			/*
			<MBStatus: version=2.4, date=2013-07-29 20:17:10 +0000, backupState=new, snapshotState=finished, fullBackup=0>
			<MBProperties: version=8.0, systemDomainsVersion=3.0, date=2013-07-29 20:16:58 +0000, encrypted=0, passcodeSet=0, lockdownKey
			s={ProductType: "iPod2,1", BuildVersion: "8C148", DeviceName: "MPETESTING", UniqueDeviceID: "253ad710236d7228eb42fbbb5c3fc184
			4716384e", com.apple.Accessibility: {}, com.apple.MobileDeviceCrashCopy: {}, ProductVersion: "4.2.1", com.apple.mobile.data_s
			ync: {}, com.apple.TerminalFlashr: {}, com.apple.mobile.iTunes.accessories: {}}>
			<MBDatabaseIndex: version=2.0, count=113>
			<MBDatabase: version=5.0>

			 Count   Size (B) Domain name
				45    1523726 HomeDomain
				 2      18985 KeychainDomain
				 8      70463 MediaDomain
				 2      41002 RootDomain
				 6       3034 SystemPreferencesDomain
				 2      28738 WirelessDomain
				65    1685948 Total
			*/
			String^ GetInfoFromLastBackup(String^ backupDirectory);

			String^ UnbackBackup(String^ backupDirectory, [System::Runtime::InteropServices::Optional] String^ udid);

			void Backup(
				[System::Runtime::InteropServices::Optional]
				String^ uUid,
				[System::Runtime::InteropServices::Optional]
				String^ password,
				[System::Runtime::InteropServices::Optional]
				String^ backupDirectoryRoot,
				[System::Runtime::InteropServices::Optional]
				Action<Double, Double>^ progressCallback);

			void Restore(
				String^ backupDirectory,
				[System::Runtime::InteropServices::Optional]
				String^ sourceUuid,
				[System::Runtime::InteropServices::Optional]
				String^ password,
				[System::Runtime::InteropServices::Optional]
				bool^ copyFirst, 
				[System::Runtime::InteropServices::Optional]
				bool^ rebootWhenDone, 
				[System::Runtime::InteropServices::Optional]
				bool^ removeItemsNotRestored, 
				[System::Runtime::InteropServices::Optional]
				bool^ restoreSystemFiles, 
				[System::Runtime::InteropServices::Optional]
				Action<Double, Double>^ progressCallback,
				[System::Runtime::InteropServices::Optional]
				bool^ restoreSettings);

			Boolean^ ChangePassword(String^ currentPassword, String^ newPassword) { return false; }
			//TODO: make into a property
			String^ LastBackupDetails() { return nullptr; }
		private:
			delegate void ProgressCallbackWrapper(uint64_t current, uint64_t total);
			int Errors;
			cmd_mode RequestedOperation;
			void Cleanup() {
				if(_deviceBackup) {
					Errors = _deviceBackup->GetErrorFlags();
					RequestedOperation = _deviceBackup->GetRequestedOperation();
					delete _deviceBackup;
					_deviceBackup = NULL;
				}
				cancelTask = nullptr;
				_progressCallback = nullptr;
			}

			void Initialize() {
				Errors = 0;
				_progressCallback = nullptr;
				progressWrapper = gcnew ProgressCallbackWrapper(this, &ManagedDeviceBackup2::ReportProgress);
			}

			progress_t GetProgressCallback() {
				if(_deviceBackup) {
					IntPtr ip = Marshal::GetFunctionPointerForDelegate(progressWrapper);
					return static_cast<progress_t>(ip.ToPointer());
				}
				return NULL;
			}

			void ReportProgress(uint64_t current, uint64_t total) {
				if(_progressCallback != nullptr) {
					_progressCallback(static_cast<double>(current), static_cast<double>(total));
				}
			}
			void DecodeErrors();

			CancelTaskCallBack^ cancelTask;
			ProgressCallbackWrapper^ progressWrapper;
			Action<Double, Double>^ _progressCallback;
			iDeviceBackup2* _deviceBackup;
	};
}