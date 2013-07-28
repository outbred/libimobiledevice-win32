using System;
using System.Collections.Generic;
using System.Configuration;
using System.Data;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using BackupWrapper;

namespace iMobileDeviceTester
{
	/// <summary>
	/// Interaction logic for App.xaml
	/// </summary>
	public partial class App : Application
	{
		protected override void OnStartup(StartupEventArgs e)
		{
			base.OnStartup(e);
			var backuper = new ManagedDeviceBackup2();
			backuper.Backup(null, null, @"G:\trash");
			backuper.Dispose();
			Thread.Sleep(1000);
			backuper = new ManagedDeviceBackup2();
			backuper.Backup(null, null, @"G:\trash");
			Thread.Sleep(1000);
			backuper.Backup(null, null, @"G:\trash");
		}
	}
}
