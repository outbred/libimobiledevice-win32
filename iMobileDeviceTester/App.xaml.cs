using System;
using System.Collections.Generic;
using System.Configuration;
using System.Data;
using System.Diagnostics;
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
			//backuper.Backup(null, null, @"G:\trash", percentage =>
			//	{
			//		Console.WriteLine("Progress is {0}", percentage);
			//	});
			//MessageBox.Show("here");
			backuper.Restore(@"G:\trash", null, null, false, true, false, true, (current, total) =>
				{
					Debug.WriteLine("Current  is {0}%; total is {1}%", current, total);
				}, true);
			//backuper.Dispose();
			//Thread.Sleep(1000);
			//backuper = new ManagedDeviceBackup2();
			//backuper.Backup(null, null, @"G:\trash");
			//Thread.Sleep(1000);
			//backuper.Backup(null, null, @"G:\trash");
		}
	}
}
