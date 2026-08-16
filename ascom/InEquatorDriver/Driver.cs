using System;
using System.Collections;
using System.Globalization;
using System.Runtime.InteropServices;
using ASCOM;
using ASCOM.Utilities;

namespace ASCOM.InEquator
{
    // Custom ASCOM driver for the InEquator single-axis RA tracker.
    // There is no standard ASCOM device class for a one-axis tracker, so this
    // driver exposes a small custom COM interface. Clients use the ProgID
    // "ASCOM.InEquator.Tracker" directly (see the InEquatorTest console app).
    // AutoDispatch: custom COM interface exposed via IDispatch so clients can
    // late-bind through the ProgID (see ascom/InEquatorTest).
    [Guid("6e91f7a4-2c5d-4b8e-9a31-d4c0e5b72f17")]
    [ProgId("ASCOM.InEquator.Tracker")]
    [ClassInterface(ClassInterfaceType.AutoDispatch)]
    public class Tracker
    {
        private ITrackerConnection connection;
        private System.Threading.Mutex mutex = new System.Threading.Mutex();

        private int lastPos = 0;
        private bool lastMoving = false;
        private bool lastTracking = false;
        private int lastJogRate = 80000;
        private int lastJogStepsPerSec = 100;
        private int stepsPerOutputRev = 307200;
        private double trackingRate = 3.5653;

        private long lastUpdate = 0;
        private const long UpdateTicks = 1 * 10000000L;  // 1 s status cache

        // ASCOM identity
        internal static string driverID = "ASCOM.InEquator.Tracker";
        private static string driverDisplayName = "InEquator RA Tracker";
        private static string driverDescription = "ASCOM driver for the InEquator ESP8266 single-axis RA tracker.";
        private const string FirmwareIdentity = "InEquator RA Tracker";

        // Profile keys
        internal static string comPortProfileName = "COM Port";
        internal static string comPortDefault = "COM1";
        internal static string comPortLegacyProfileName = "ComPort";
        internal static string transportProfileName = "Transport";
        internal static string transportDefault = "TCP";
        internal static string tcpHostProfileName = "TcpHost";
        internal static string tcpHostDefault = "192.168.4.1";
        internal static string tcpPortProfileName = "TcpPort";
        internal static string tcpPortDefault = "4030";
        internal static string commandTimeoutProfileName = "CommandTimeoutMs";
        internal static string commandTimeoutDefault = "3000";
        internal static string traceStateProfileName = "Trace Level";
        internal static string traceStateDefault = "false";

        // Configuration
        internal static string comPort;
        internal static string transport;
        internal static string tcpHost;
        internal static int tcpPort;
        internal static int commandTimeoutMs;
        internal static bool traceState;

        private bool connectedState;
        private TraceLogger tl;

        public Tracker()
        {
            ReadProfile();
            tl = new TraceLogger("", "InEquator Tracker");
            tl.Enabled = traceState;
            tl.LogMessage("Tracker", "Starting initialization");
            connectedState = false;
            tl.LogMessage("Tracker", "Completed initialization");
        }

        // ==================== Identity ====================

        public string Description
        {
            get { return driverDescription; }
        }

        public string DriverInfo
        {
            get
            {
                Version version = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
                return "InEquator RA Tracker driver (custom interface). Version: " +
                    String.Format(CultureInfo.InvariantCulture, "{0}.{1}", version.Major, version.Minor);
            }
        }

        public string DriverVersion
        {
            get
            {
                Version version = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
                return String.Format(CultureInfo.InvariantCulture, "{0}.{1}", version.Major, version.Minor);
            }
        }

        public string Name
        {
            get { return driverDisplayName; }
        }

        // ==================== Connection ====================

        public bool Connected
        {
            get { return connectedState; }
            set
            {
                tl.LogMessage("Connected Set", value.ToString());
                if (value == connectedState) return;

                if (value)
                {
                    using (Profile p = new Profile())
                    {
                        p.DeviceType = "Misc";
                        if (!p.IsRegistered(driverID))
                        {
                            p.Register(driverID, driverDisplayName);
                        }
                        transport = GetProfileValue(p, transportProfileName, transportDefault);
                        comPort = GetProfileValue(p, comPortLegacyProfileName, GetProfileValue(p, comPortProfileName, comPortDefault));
                        tcpHost = GetProfileValue(p, tcpHostProfileName, tcpHostDefault);
                        tcpPort = ParseInt(GetProfileValue(p, tcpPortProfileName, tcpPortDefault), 4030);
                        commandTimeoutMs = ParseInt(GetProfileValue(p, commandTimeoutProfileName, commandTimeoutDefault), 3000);

                        try
                        {
                            connection = CreateConnection();
                            tl.LogMessage("Connecting to tracker", connection.EndpointDescription);
                            connection.Connect();
                            connectedState = true;
                            lastUpdate = 0;

                            ValidateDeviceIdentity();

                            TrackerDeviceInfo info = TrackerProtocol.ParseDeviceInfo(ExecuteFirmwareCommand("I#"));
                            if (info.StepsPerOutputRev.HasValue && info.StepsPerOutputRev.Value > 0)
                                stepsPerOutputRev = info.StepsPerOutputRev.Value;
                            if (info.TrackingRate.HasValue && info.TrackingRate.Value > 0)
                                trackingRate = info.TrackingRate.Value;
                            if (info.JogRate.HasValue)
                                lastJogRate = info.JogRate.Value;

                            tl.LogMessage("Firmware Version", ExecuteFirmwareCommand("V#"));
                        }
                        catch (Exception ex)
                        {
                            connectedState = false;
                            if (connection != null)
                            {
                                connection.Dispose();
                                connection = null;
                            }
                            throw new ASCOM.NotConnectedException("InEquator tracker connection error", ex);
                        }
                    }
                }
                else
                {
                    connectedState = false;
                    lastUpdate = 0;
                    if (connection != null)
                    {
                        tl.LogMessage("Connected Set", "Disconnecting from " + connection.EndpointDescription);
                        connection.Dispose();
                        connection = null;
                    }
                }
            }
        }

        public void SetupDialog()
        {
            if (connectedState)
            {
                System.Windows.Forms.MessageBox.Show("Disconnect the tracker before opening setup.");
                return;
            }

            using (SetupDialogForm F = new SetupDialogForm())
            {
                var result = F.ShowDialog();
                if (result == System.Windows.Forms.DialogResult.OK)
                {
                    WriteProfile();
                }
            }
        }

        // ==================== Commands ====================

        public ArrayList SupportedActions
        {
            get { return new ArrayList(); }
        }

        public string Action(string actionName, string actionParameters)
        {
            throw new ASCOM.ActionNotImplementedException(actionName);
        }

        public void CommandBlind(string command, bool raw)
        {
            CheckConnected("CommandBlind");
            this.CommandString(command, raw);
        }

        public bool CommandBool(string command, bool raw)
        {
            CheckConnected("CommandBool");
            string ret = CommandString(command, raw);
            return ret.IndexOf("true", StringComparison.OrdinalIgnoreCase) >= 0 || ret.Trim().StartsWith("1");
        }

        public string CommandString(string command, bool raw)
        {
            CheckConnected("CommandString");
            mutex.WaitOne();
            try
            {
                tl.LogMessage("Sending Command: ", command);
                string response = connection.CommandString(command);
                tl.LogMessage("Got Response: ", response);
                if (TrackerProtocol.IsErrorResponse(response))
                {
                    throw new ASCOM.DriverException("Tracker command failed: " + response);
                }
                return response;
            }
            catch (ASCOM.DriverException)
            {
                throw;
            }
            catch (Exception e)
            {
                tl.LogMessage("Caught exception in CommandString ", e.Message);
                throw new ASCOM.DriverException("Tracker command failed.", e);
            }
            finally
            {
                mutex.ReleaseMutex();
            }
        }

        // ==================== Tracking / Motion API ====================

        public int PositionSteps
        {
            get { UpdateStatusIfNeeded(); return lastPos; }
        }

        public double AngleDegrees
        {
            get
            {
                UpdateStatusIfNeeded();
                long mod = ((lastPos % stepsPerOutputRev) + stepsPerOutputRev) % stepsPerOutputRev;
                return (mod * 360.0) / stepsPerOutputRev;
            }
        }

        public int StepsPerOutputRev
        {
            get { return stepsPerOutputRev; }
        }

        public double TrackingRate
        {
            get { return trackingRate; }
        }

        public bool IsMoving
        {
            get { UpdateStatusIfNeeded(); return lastMoving; }
        }

        public bool Tracking
        {
            get { UpdateStatusIfNeeded(); return lastTracking; }
            set
            {
                string response = ExecuteFirmwareCommand("B " + (value ? "1" : "0") + "#");
                lastTracking = value;
                lastUpdate = 0;
            }
        }

        public int JogRate
        {
            get { UpdateStatusIfNeeded(); return lastJogRate; }
            set
            {
                if (value < 100 || value > 1000000)
                    throw new ASCOM.InvalidValueException("JogRate", value.ToString(CultureInfo.InvariantCulture), "100..1000000 (x10000 of sidereal)");
                ExecuteFirmwareCommand("Q " + value.ToString(CultureInfo.InvariantCulture) + "#");
                lastJogRate = value;
                lastUpdate = 0;
            }
        }

        public int JogRateStepsPerSec
        {
            get { UpdateStatusIfNeeded(); return lastJogStepsPerSec; }
            set
            {
                if (value < 1 || value > 10000)
                    throw new ASCOM.InvalidValueException("JogRateStepsPerSec", value.ToString(CultureInfo.InvariantCulture), "1..10000");
                ExecuteFirmwareCommand("Y " + value.ToString(CultureInfo.InvariantCulture) + "#");
                lastJogStepsPerSec = value;
                lastUpdate = 0;
            }
        }

        public void MoveDegrees(double degrees)
        {
            if (degrees < -1000.0 || degrees > 1000.0)
                throw new ASCOM.InvalidValueException("MoveDegrees", degrees.ToString(CultureInfo.InvariantCulture), "-1000..1000");
            long deg1000 = (long)Math.Round(degrees * 1000.0, MidpointRounding.AwayFromZero);
            ExecuteFirmwareCommand("MD " + deg1000.ToString(CultureInfo.InvariantCulture) + "#");
            lastMoving = true;
            lastUpdate = 0;
        }

        public void MoveArcsec(double arcsec)
        {
            if (arcsec < -1296000.0 || arcsec > 1296000.0)
                throw new ASCOM.InvalidValueException("MoveArcsec", arcsec.ToString(CultureInfo.InvariantCulture), "-1296000..1296000");
            long value = (long)Math.Round(arcsec, MidpointRounding.AwayFromZero);
            ExecuteFirmwareCommand("MA " + value.ToString(CultureInfo.InvariantCulture) + "#");
            lastMoving = true;
            lastUpdate = 0;
        }

        public void JogCW()
        {
            ExecuteFirmwareCommand("M+#");
            lastMoving = true;
            lastUpdate = 0;
        }

        public void JogCCW()
        {
            ExecuteFirmwareCommand("M-#");
            lastMoving = true;
            lastUpdate = 0;
        }

        public void MoveBy(int steps)
        {
            if (steps == 0) return;
            ExecuteFirmwareCommand("M " + steps.ToString(CultureInfo.InvariantCulture) + "#");
            lastMoving = true;
            lastUpdate = 0;
        }

        public void Halt()
        {
            ExecuteFirmwareCommand("S#");
            lastMoving = false;
            lastUpdate = 0;
        }

        public void SetPosition(int steps)
        {
            ExecuteFirmwareCommand("P " + steps.ToString(CultureInfo.InvariantCulture) + "#");
            lastPos = steps;
            lastUpdate = 0;
        }

        public bool Reverse
        {
            set
            {
                ExecuteFirmwareCommand("R " + (value ? "1" : "0") + "#");
            }
        }

        public bool Hold
        {
            set
            {
                ExecuteFirmwareCommand("C " + (value ? "1" : "0") + "#");
            }
        }

        public long Ppm
        {
            set
            {
                if (value < -10000 || value > 10000)
                    throw new ASCOM.InvalidValueException("Ppm", value.ToString(CultureInfo.InvariantCulture), "-10000..10000");
                ExecuteFirmwareCommand("D " + value.ToString(CultureInfo.InvariantCulture) + "#");
                lastUpdate = 0;
            }
        }

        public void Dispose()
        {
            connectedState = false;
            if (connection != null)
            {
                connection.Dispose();
                connection = null;
            }
            if (tl != null)
            {
                tl.Enabled = false;
                tl.Dispose();
                tl = null;
            }
            if (mutex != null)
            {
                mutex.Dispose();
            }
        }

        // ==================== Internals ====================

        private void CheckConnected(string message)
        {
            if (!connectedState || connection == null || !connection.IsConnected)
                throw new ASCOM.NotConnectedException(message);
        }

        private string ExecuteFirmwareCommand(string command)
        {
            CheckConnected("ExecuteFirmwareCommand");
            mutex.WaitOne();
            try
            {
                string response = connection.CommandString(command);
                if (TrackerProtocol.IsErrorResponse(response))
                    throw new ASCOM.DriverException("Tracker command failed: " + response);
                return response;
            }
            finally
            {
                mutex.ReleaseMutex();
            }
        }

        private void UpdateStatusIfNeeded()
        {
            long now = DateTime.Now.Ticks;
            if (now - lastUpdate < UpdateTicks && lastUpdate != 0) return;

            string response = ExecuteFirmwareCommand("G#");
            int position;
            bool tracking;
            int jogRate;
            int jogStepsPerSec;
            bool moving;
            if (!TrackerProtocol.TryParseMotionStatus(response, out position, out tracking, out jogRate, out jogStepsPerSec, out moving))
                throw new ASCOM.DriverException("Unrecognized tracker status response: " + response);

            lastPos = position;
            lastTracking = tracking;
            lastJogRate = jogRate;
            lastJogStepsPerSec = jogStepsPerSec;
            lastMoving = moving;
            lastUpdate = now;
        }

        private void ValidateDeviceIdentity()
        {
            string response = ExecuteFirmwareCommand("#");
            if (string.IsNullOrWhiteSpace(response)
                || !response.TrimStart().StartsWith(FirmwareIdentity, StringComparison.OrdinalIgnoreCase))
            {
                throw new ASCOM.DriverException(
                    "Connected device is not an InEquator tracker. Identification response: " + response);
            }
        }

        private ITrackerConnection CreateConnection()
        {
            if (string.Equals(transport, "Serial", StringComparison.OrdinalIgnoreCase))
            {
                return new SerialTrackerConnection(comPort, commandTimeoutMs);
            }
            return new TcpTrackerConnection(tcpHost, tcpPort, commandTimeoutMs);
        }

        private void ReadProfile()
        {
            using (Profile p = new Profile())
            {
                p.DeviceType = "Misc";
                transport = GetProfileValue(p, transportProfileName, transportDefault);
                comPort = GetProfileValue(p, comPortLegacyProfileName, GetProfileValue(p, comPortProfileName, comPortDefault));
                tcpHost = GetProfileValue(p, tcpHostProfileName, tcpHostDefault);
                tcpPort = ParseInt(GetProfileValue(p, tcpPortProfileName, tcpPortDefault), 4030);
                commandTimeoutMs = ParseInt(GetProfileValue(p, commandTimeoutProfileName, commandTimeoutDefault), 3000);
                traceState = GetProfileValue(p, traceStateProfileName, traceStateDefault).ToLowerInvariant().Equals("true");
            }
        }

        private void WriteProfile()
        {
            using (Profile p = new Profile())
            {
                p.DeviceType = "Misc";
                p.WriteValue(driverID, transportProfileName, transport);
                p.WriteValue(driverID, comPortProfileName, comPort);
                p.WriteValue(driverID, tcpHostProfileName, tcpHost);
                p.WriteValue(driverID, tcpPortProfileName, tcpPort.ToString(CultureInfo.InvariantCulture));
                p.WriteValue(driverID, commandTimeoutProfileName, commandTimeoutMs.ToString(CultureInfo.InvariantCulture));
                p.WriteValue(driverID, traceStateProfileName, traceState.ToString());
            }
        }

        private static string GetProfileValue(Profile p, string name, string defaultValue)
        {
            return p.GetValue(driverID, name, string.Empty, defaultValue);
        }

        private static int ParseInt(string value, int defaultValue)
        {
            int parsed;
            return int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) ? parsed : defaultValue;
        }
    }
}
