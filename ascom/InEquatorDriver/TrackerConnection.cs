using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using ASCOM.Utilities;

namespace ASCOM.InEquator
{
    internal interface ITrackerConnection : IDisposable
    {
        bool IsConnected { get; }
        string EndpointDescription { get; }
        void Connect();
        void Disconnect();
        string CommandString(string command);
    }

    internal sealed class SerialTrackerConnection : ITrackerConnection
    {
        private readonly string portName;
        private readonly int timeoutMs;
        private ASCOM.Utilities.Serial serialPort;

        public SerialTrackerConnection(string portName, int timeoutMs)
        {
            this.portName = portName;
            this.timeoutMs = timeoutMs;
        }

        public bool IsConnected
        {
            get { return serialPort != null && serialPort.Connected; }
        }

        public string EndpointDescription
        {
            get { return portName; }
        }

        public void Connect()
        {
            serialPort = new ASCOM.Utilities.Serial();
            serialPort.PortName = portName;
            serialPort.Speed = SerialSpeed.ps9600;
            serialPort.StopBits = SerialStopBits.One;
            serialPort.Parity = SerialParity.None;
            serialPort.DataBits = 8;
            serialPort.DTREnable = false;
            serialPort.ReceiveTimeoutMs = timeoutMs;
            serialPort.Connected = true;
            System.Threading.Thread.Sleep(2200);
            serialPort.ClearBuffers();
        }

        public void Disconnect()
        {
            if (serialPort == null) return;
            if (serialPort.Connected)
            {
                serialPort.Connected = false;
            }
            serialPort.Dispose();
            serialPort = null;
        }

        public string CommandString(string command)
        {
            if (!IsConnected)
                throw new InvalidOperationException("Serial tracker connection is closed.");

            if (!command.EndsWith("#"))
                command += "#";

            serialPort.ClearBuffers();
            serialPort.Transmit(command);
            string response = serialPort.ReceiveTerminated("#");
            serialPort.ClearBuffers();
            return response;
        }

        public void Dispose()
        {
            Disconnect();
        }
    }

    internal sealed class TcpTrackerConnection : ITrackerConnection
    {
        private readonly string host;
        private readonly int port;
        private readonly int timeoutMs;
        private TcpClient client;
        private NetworkStream stream;

        public TcpTrackerConnection(string host, int port, int timeoutMs)
        {
            this.host = host;
            this.port = port;
            this.timeoutMs = timeoutMs;
        }

        public bool IsConnected
        {
            get
            {
                if (client == null || !client.Connected || client.Client == null)
                    return false;

                try
                {
                    return !(client.Client.Poll(0, SelectMode.SelectRead) && client.Client.Available == 0);
                }
                catch (SocketException)
                {
                    return false;
                }
                catch (ObjectDisposedException)
                {
                    return false;
                }
            }
        }

        public string EndpointDescription
        {
            get { return host + ":" + port.ToString(); }
        }

        public void Connect()
        {
            client = new TcpClient();
            client.NoDelay = true;
            client.SendTimeout = timeoutMs;
            client.ReceiveTimeout = timeoutMs;
            IAsyncResult connectResult = client.BeginConnect(host, port, null, null);
            using (connectResult.AsyncWaitHandle)
            {
                if (!connectResult.AsyncWaitHandle.WaitOne(timeoutMs))
                {
                    client.Close();
                    client = null;
                    throw new TimeoutException("TCP connection to " + EndpointDescription + " timed out.");
                }
                client.EndConnect(connectResult);
            }
            stream = client.GetStream();
            stream.ReadTimeout = timeoutMs;
            stream.WriteTimeout = timeoutMs;
        }

        public void Disconnect()
        {
            if (stream != null)
            {
                stream.Dispose();
                stream = null;
            }
            if (client != null)
            {
                client.Close();
                client = null;
            }
        }

        public string CommandString(string command)
        {
            if (!IsConnected || stream == null)
                throw new InvalidOperationException("TCP tracker connection is closed.");

            if (!command.EndsWith("#"))
                command += "#";

            byte[] request = Encoding.ASCII.GetBytes(command);
            stream.Write(request, 0, request.Length);
            stream.Flush();

            StringBuilder response = new StringBuilder();
            byte[] buffer = new byte[1];
            while (true)
            {
                int read = stream.Read(buffer, 0, 1);
                if (read == 0)
                    throw new IOException("Connection closed while waiting for response.");

                response.Append((char)buffer[0]);
                if (buffer[0] == (byte)'#')
                    break;
            }
            return response.ToString();
        }

        public void Dispose()
        {
            Disconnect();
        }
    }
}
