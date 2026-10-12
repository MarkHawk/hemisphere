@echo off
rem rdp-to-console.bat  -  leave Remote Desktop without blanking the dome
rem
rem Run this INSIDE a Remote Desktop session, as administrator (right-click,
rem Run as administrator), instead of closing the RDP window. It ends the
rem remote connection and hands your session straight back to the PC's own
rem screens, still signed in, so Hemisphere keeps drawing on the WorldEye.
rem
rem Closing RDP the normal way locks the session and the dome goes dark
rem until someone signs in at the PC.

for /f "tokens=3" %%s in ('query user %USERNAME% ^| findstr /b ">"') do (
    %windir%\System32\tscon.exe %%s /dest:console
    goto :eof
)
echo Could not find your session. Run this from inside Remote Desktop, as administrator.
pause
