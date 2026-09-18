param(
  [ValidateSet("install", "update", "reinstall", "uninstall", "check")]
  [string]$Mode = "install"
)

$ErrorActionPreference = "Stop"

$installScript = Join-Path $PSScriptRoot "install.ps1"
if (-not (Test-Path $installScript)) {
  throw "install.ps1 not found next to install_gui.ps1"
}

function Invoke-Installer {
  param(
    [string]$ChosenMode,
    [string]$Scope,
    [string]$InstallDir,
    [string]$PathMode
  )
  $env:TEZZ_INSTALL_SCOPE = $Scope
  $env:TEZZ_INSTALL_DIR = $InstallDir
  $env:TEZZ_INSTALL_AUTO_PATH = $PathMode
  & $installScript -Mode $ChosenMode
}

if (-not ("System.Windows.Forms" -as [type])) {
  Add-Type -AssemblyName System.Windows.Forms
  Add-Type -AssemblyName System.Drawing
}

$defaultScope = if ($env:TEZZ_INSTALL_SCOPE) { $env:TEZZ_INSTALL_SCOPE } else { "user" }
$defaultDir = if ($env:TEZZ_INSTALL_DIR) {
  $env:TEZZ_INSTALL_DIR
} elseif ($defaultScope -eq "user") {
  Join-Path $HOME "TezzNative"
} else {
  Join-Path $env:ProgramFiles "TezzNative"
}
$defaultPathMode = if ($env:TEZZ_INSTALL_AUTO_PATH) { $env:TEZZ_INSTALL_AUTO_PATH } else { "auto" }

$form = New-Object System.Windows.Forms.Form
$form.Text = "TezzNative Installer (GUI)"
$form.StartPosition = "CenterScreen"
$form.Width = 660
$form.Height = 360
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.MinimizeBox = $false

$font = New-Object System.Drawing.Font("Segoe UI", 10)
$form.Font = $font

$lblMode = New-Object System.Windows.Forms.Label
$lblMode.Text = "Mode"
$lblMode.Left = 20
$lblMode.Top = 22
$lblMode.Width = 120
$form.Controls.Add($lblMode)

$cmbMode = New-Object System.Windows.Forms.ComboBox
$cmbMode.Left = 160
$cmbMode.Top = 18
$cmbMode.Width = 450
$cmbMode.DropDownStyle = "DropDownList"
[void]$cmbMode.Items.AddRange(@("install","update","reinstall","uninstall","check"))
$cmbMode.SelectedItem = $Mode
$form.Controls.Add($cmbMode)

$lblScope = New-Object System.Windows.Forms.Label
$lblScope.Text = "Install Scope"
$lblScope.Left = 20
$lblScope.Top = 62
$lblScope.Width = 120
$form.Controls.Add($lblScope)

$cmbScope = New-Object System.Windows.Forms.ComboBox
$cmbScope.Left = 160
$cmbScope.Top = 58
$cmbScope.Width = 220
$cmbScope.DropDownStyle = "DropDownList"
[void]$cmbScope.Items.AddRange(@("machine","user"))
$cmbScope.SelectedItem = $defaultScope
$form.Controls.Add($cmbScope)

$lblPath = New-Object System.Windows.Forms.Label
$lblPath.Text = "PATH Setup"
$lblPath.Left = 400
$lblPath.Top = 62
$lblPath.Width = 120
$form.Controls.Add($lblPath)

$cmbPath = New-Object System.Windows.Forms.ComboBox
$cmbPath.Left = 490
$cmbPath.Top = 58
$cmbPath.Width = 120
$cmbPath.DropDownStyle = "DropDownList"
[void]$cmbPath.Items.AddRange(@("auto","system","user","none","prompt"))
$cmbPath.SelectedItem = $defaultPathMode
$form.Controls.Add($cmbPath)

$lblDir = New-Object System.Windows.Forms.Label
$lblDir.Text = "Install Directory"
$lblDir.Left = 20
$lblDir.Top = 102
$lblDir.Width = 120
$form.Controls.Add($lblDir)

$txtDir = New-Object System.Windows.Forms.TextBox
$txtDir.Left = 160
$txtDir.Top = 98
$txtDir.Width = 360
$txtDir.Text = $defaultDir
$form.Controls.Add($txtDir)

$btnBrowse = New-Object System.Windows.Forms.Button
$btnBrowse.Text = "Browse"
$btnBrowse.Left = 530
$btnBrowse.Top = 96
$btnBrowse.Width = 80
$btnBrowse.Add_Click({
  $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
  $dialog.Description = "Select TezzNative installation directory"
  $dialog.SelectedPath = $txtDir.Text
  if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
    $txtDir.Text = $dialog.SelectedPath
  }
})
$form.Controls.Add($btnBrowse)

$info = New-Object System.Windows.Forms.Label
$info.Left = 20
$info.Top = 140
$info.Width = 590
$info.Height = 100
$info.Text = "This GUI wraps install.ps1. Default install is user scope; machine scope asks for administrator permission."
$form.Controls.Add($info)

$btnRun = New-Object System.Windows.Forms.Button
$btnRun.Text = "Run Installer"
$btnRun.Left = 360
$btnRun.Top = 260
$btnRun.Width = 120
$btnRun.DialogResult = [System.Windows.Forms.DialogResult]::OK
$form.Controls.Add($btnRun)

$btnCancel = New-Object System.Windows.Forms.Button
$btnCancel.Text = "Cancel"
$btnCancel.Left = 490
$btnCancel.Top = 260
$btnCancel.Width = 120
$btnCancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$form.Controls.Add($btnCancel)

$result = $form.ShowDialog()
if ($result -ne [System.Windows.Forms.DialogResult]::OK) {
  exit 0
}

$chosenMode = [string]$cmbMode.SelectedItem
$chosenScope = [string]$cmbScope.SelectedItem
$chosenPath = [string]$cmbPath.SelectedItem
$chosenDir = [string]$txtDir.Text

if ([string]::IsNullOrWhiteSpace($chosenDir)) {
  [System.Windows.Forms.MessageBox]::Show("Install directory cannot be empty.", "TezzNative Installer", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
  exit 1
}

try {
  Invoke-Installer -ChosenMode $chosenMode -Scope $chosenScope -InstallDir $chosenDir -PathMode $chosenPath
  [System.Windows.Forms.MessageBox]::Show("TezzNative installer finished successfully.", "TezzNative Installer", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
  exit 0
} catch {
  [System.Windows.Forms.MessageBox]::Show("Installer failed: $($_.Exception.Message)", "TezzNative Installer", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
  exit 1
}
