using System;
using System.Drawing;
using System.Windows.Forms;

namespace ASCOM.InEquator
{
    // Programmatically built setup dialog (no designer file needed).
    internal sealed class SetupDialogForm : Form
    {
        private ComboBox transportCombo;
        private TextBox hostTextBox;
        private TextBox tcpPortTextBox;
        private TextBox comPortTextBox;
        private TextBox timeoutTextBox;
        private CheckBox traceCheckBox;
        private Button okButton;
        private Button cancelButton;

        public SetupDialogForm()
        {
            InitializeComponent();
            LoadValues();
        }

        private void InitializeComponent()
        {
            this.Text = "InEquator RA Tracker Setup";
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.StartPosition = FormStartPosition.CenterScreen;
            this.ClientSize = new Size(360, 260);
            this.Font = new Font("Segoe UI", 9F);

            Label transportLabel = MakeLabel("Transport", 20, 20);
            transportCombo = new ComboBox();
            transportCombo.Items.AddRange(new object[] { "TCP", "Serial" });
            transportCombo.Location = new Point(140, 17);
            transportCombo.Size = new Size(190, 24);
            transportCombo.DropDownStyle = ComboBoxStyle.DropDownList;

            Label hostLabel = MakeLabel("TCP Host", 20, 52);
            hostTextBox = MakeTextBox(140, 49);

            Label tcpPortLabel = MakeLabel("TCP Port", 20, 84);
            tcpPortTextBox = MakeTextBox(140, 81);

            Label comPortLabel = MakeLabel("COM Port", 20, 116);
            comPortTextBox = MakeTextBox(140, 113);

            Label timeoutLabel = MakeLabel("Timeout (ms)", 20, 148);
            timeoutTextBox = MakeTextBox(140, 145);

            Label traceLabel = MakeLabel("Trace", 20, 180);
            traceCheckBox = new CheckBox();
            traceCheckBox.Location = new Point(140, 176);
            traceCheckBox.Size = new Size(190, 24);

            okButton = new Button();
            okButton.Text = "OK";
            okButton.DialogResult = DialogResult.OK;
            okButton.Location = new Point(140, 216);
            okButton.Size = new Size(90, 30);
            this.AcceptButton = okButton;

            cancelButton = new Button();
            cancelButton.Text = "Cancel";
            cancelButton.DialogResult = DialogResult.Cancel;
            cancelButton.Location = new Point(240, 216);
            cancelButton.Size = new Size(90, 30);
            this.CancelButton = cancelButton;

            this.Controls.AddRange(new Control[] {
                transportLabel, transportCombo,
                hostLabel, hostTextBox,
                tcpPortLabel, tcpPortTextBox,
                comPortLabel, comPortTextBox,
                timeoutLabel, timeoutTextBox,
                traceLabel, traceCheckBox,
                okButton, cancelButton
            });

            okButton.Click += OkButton_Click;
        }

        private Label MakeLabel(string text, int x, int y)
        {
            Label label = new Label();
            label.Text = text;
            label.Location = new Point(x, y);
            label.Size = new Size(110, 20);
            label.TextAlign = ContentAlignment.MiddleLeft;
            return label;
        }

        private TextBox MakeTextBox(int x, int y)
        {
            TextBox textBox = new TextBox();
            textBox.Location = new Point(x, y);
            textBox.Size = new Size(190, 24);
            return textBox;
        }

        private void LoadValues()
        {
            transportCombo.Text = Tracker.transport ?? Tracker.transportDefault;
            hostTextBox.Text = Tracker.tcpHost ?? Tracker.tcpHostDefault;
            tcpPortTextBox.Text = Tracker.tcpPort.ToString();
            comPortTextBox.Text = Tracker.comPort ?? Tracker.comPortDefault;
            timeoutTextBox.Text = Tracker.commandTimeoutMs.ToString();
            traceCheckBox.Checked = Tracker.traceState;
        }

        private void OkButton_Click(object sender, EventArgs e)
        {
            int tcpPort;
            int timeout;
            if (!int.TryParse(tcpPortTextBox.Text.Trim(), out tcpPort) || tcpPort < 1 || tcpPort > 65535)
            {
                MessageBox.Show("TCP port must be a number between 1 and 65535.");
                this.DialogResult = DialogResult.None;
                return;
            }
            if (!int.TryParse(timeoutTextBox.Text.Trim(), out timeout) || timeout < 100 || timeout > 60000)
            {
                MessageBox.Show("Timeout must be a number between 100 and 60000 ms.");
                this.DialogResult = DialogResult.None;
                return;
            }

            Tracker.transport = transportCombo.Text;
            Tracker.tcpHost = hostTextBox.Text.Trim();
            Tracker.tcpPort = tcpPort;
            Tracker.comPort = comPortTextBox.Text.Trim();
            Tracker.commandTimeoutMs = timeout;
            Tracker.traceState = traceCheckBox.Checked;
        }
    }
}
