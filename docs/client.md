# Client

`tools/m4client.py` (standard library only) signs requests for you:

    python tools/m4client.py --host 192.168.1.50 info
    python tools/m4client.py --host 192.168.1.50 pair          # press UP first
    python tools/m4client.py --host 192.168.1.50 get /api/status
    python tools/m4client.py --host 192.168.1.50 post /api/led '{"on": true}'

It stores credentials in `.m4-credentials.json`, which is gitignored.

On Windows, run these from PowerShell or cmd. Git Bash rewrites a leading `/` in
an argument into a Windows path, so `/api/status` arrives as
`C:/Program Files/Git/api/status`. Prefix the command with `MSYS_NO_PATHCONV=1`
if you want to use Git Bash anyway.
