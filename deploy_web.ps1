# deploy_web.ps1 — Manual web deploy for tn.tezzcorp.com
# Run: powershell -ExecutionPolicy Bypass -File deploy_web.ps1

python -c @'
import paramiko, os, sys

host, port, user, password = "195.35.5.93", 65002, "u190073748", "Rohit@8084"
remote_base = "/home/u190073748/domains/tezzcorp.com/tn.tezzcorp.com"
local_web   = r"TezzCorp_WebSites\tn_tezzcorp_com"

files = [
    (f"{local_web}/index.php",             f"{remote_base}/index.php"),
    (f"{local_web}/community.php",         f"{remote_base}/community.php"),
    (f"{local_web}/support.php",           f"{remote_base}/support.php"),
    (f"{local_web}/versions.php",          f"{remote_base}/versions.php"),
    (f"{local_web}/.htaccess",             f"{remote_base}/.htaccess"),
    (f"{local_web}/assets/app.css",        f"{remote_base}/assets/app.css"),
    (f"{local_web}/assets/app.js",         f"{remote_base}/assets/app.js"),
    (f"{local_web}/includes/header.php",   f"{remote_base}/includes/header.php"),
    (f"{local_web}/includes/footer.php",   f"{remote_base}/includes/footer.php"),
    (f"{local_web}/includes/nav.php",      f"{remote_base}/includes/nav.php"),
    (f"{local_web}/lib/index.php",         f"{remote_base}/lib/index.php"),
    ("tezz.lock",                          f"{remote_base}/tezz.lock"),
    ("registry.tnx",                       f"{remote_base}/registry.tnx"),
]

try:
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(host, port=port, username=user, password=password, timeout=60, banner_timeout=60)
    sftp = ssh.open_sftp()
    for local, remote in files:
        if os.path.isfile(local):
            sftp.put(local, remote)
            print(f"  OK  {local}")
        else:
            print(f"  --  {local} (skipped, not found)")
    sftp.close()
    ssh.close()
    print("Deploy complete.")
except Exception as e:
    print(f"ERROR: {e}", file=sys.stderr)
    sys.exit(1)
'@
