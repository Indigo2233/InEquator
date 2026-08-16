using System;

namespace ASCOM.InEquator.Test
{
    // Late-bound smoke test for ASCOM.InEquator.Tracker.
    // Register the driver first (tools\register-ascom.ps1), then run.
    internal static class Program
    {
        private static int Main(string[] args)
        {
            try
            {
                Type trackerType = Type.GetTypeFromProgID("ASCOM.InEquator.Tracker", true);
                dynamic tracker = Activator.CreateInstance(trackerType);

                Console.WriteLine("Driver : {0} ({1})", tracker.Name, tracker.DriverVersion);
                Console.WriteLine("Connecting (default profile: TCP 192.168.4.1:4030)...");
                tracker.Connected = true;
                Console.WriteLine("Connected.");

                Console.WriteLine("Position : {0} steps = {1:F3} deg", tracker.PositionSteps, tracker.AngleDegrees);
                Console.WriteLine("Tracking : {0}", tracker.Tracking);
                Console.WriteLine("JogRate  : {0} (x10000)", tracker.JogRate);
                Console.WriteLine("Moving   : {0}", tracker.IsMoving);

                Console.WriteLine("Jog CW for 3 s ...");
                tracker.JogCW();
                System.Threading.Thread.Sleep(1000);
                Console.WriteLine("  t+1s position = {0}", tracker.PositionSteps);
                System.Threading.Thread.Sleep(2000);
                Console.WriteLine("  t+3s position = {0}", tracker.PositionSteps);
                tracker.Halt();
                Console.WriteLine("Halted.");

                Console.WriteLine("Tracking on ...");
                tracker.Tracking = true;
                System.Threading.Thread.Sleep(2000);
                Console.WriteLine("Position after 2 s tracking: {0}", tracker.PositionSteps);

                tracker.Connected = false;
                Console.WriteLine("Disconnected. OK.");
                return 0;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Test failed: " + ex.Message);
                return 1;
            }
        }
    }
}
