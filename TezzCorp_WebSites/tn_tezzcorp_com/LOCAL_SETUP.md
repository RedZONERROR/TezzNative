# Local Setup

1. Start WAMP services (`Apache` and `MySQL/MariaDB`).
2. Run database setup:
```powershell
powershell -ExecutionPolicy Bypass -File database/setup-local.ps1
```
This imports base SQL and applies all SQL files from `database/*.sql` in name order.
3. Run worker tick (automation + retry + webhooks):
```powershell
powershell -ExecutionPolicy Bypass -File workers/run-worker.ps1
```
For continuous worker mode:
```powershell
powershell -ExecutionPolicy Bypass -File workers/run-worker.ps1 -Loop -Sleep 30 -Limit 25
```
This launcher auto-selects a PHP runtime with `pdo_mysql` so it does not use broken `C:\php\php.exe` setups.
4. Open:
- `http://localhost/`
- `http://localhost/crm.tezzcorp.com/`

## Optional CRM Subdomain (`crm.localhost`)
To use CRM on true subdomain locally, run your terminal as **Administrator** and append to:
`C:\Windows\System32\drivers\etc\hosts`

```txt
127.0.0.1 crm.localhost
::1 crm.localhost
```

Then set in `.env.local`:
```env
CRM_URL=http://crm.localhost
```
If you use different hosts for website and CRM locally, configure cookie domain carefully or login sessions will not be shared.

## Local Login
- Email: `bthdevelopers@gmail.com`
- Password: `Admin@12345`

## CRM Content Management
After login, manage public website content from:
- `http://localhost/crm.tezzcorp.com/website-content.php`

## Environment
Local runtime now uses `.env.local` automatically (if present), so production `.env` remains untouched.
