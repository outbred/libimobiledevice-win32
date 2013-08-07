using System;
using System.Collections.Generic;
using System.Configuration;
using System.Data;
using System.Diagnostics;
using System.IO;
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
			//G:\iPhone Backup\Brent\Backup\d3e3e8f72853263b496af5c0df545afd998ce238
			//backuper.UnbackBackup(@"G:\iPhone Backup\Brent\Backup", "d3e3e8f72853263b496af5c0df545afd998ce238");
			//backuper.Restore(@"G:\iPhone Backup\Brent\Backup", null, null, false, true, false, true, (current, total) =>
			//	{
			//		Debug.WriteLine("Current  is {0}%; total is {1}%", current, total);
			//	}, true);
			string screenshot;
			backuper.GetScreenshotofDevice(@"G:\trash", null, out screenshot);
			if (File.Exists(@"G:\trash\temp.plist"))
			{
				File.Delete(@"G:\trash\temp.plist");
			}
			backuper.GetInfoForConnectedDevice(null, @"G:\trash\temp.plist", false);
			//if (!File.Exists(plist))
			//{
			//	Trace.TraceError("nope");
			//}
			//backuper.Dispose();
			//Thread.Sleep(1000);
			//backuper = new ManagedDeviceBackup2();
			//backuper.Backup(null, null, @"G:\trash");
			//Thread.Sleep(1000);
			//backuper.Backup(null, null, @"G:\trash");
		}
	}
}
