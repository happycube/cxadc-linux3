## Install FFmpeg/SoX/FLAC inside windows

These commands will install these in `PATH` so the tools are available system-wide.

Open a PowerShell Terminal as administrator.

Install Choco (chocolatey package manager) 

    Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

Then you can install the following system-wide without any hassle.

Install FFmpeg

    choco install ffmpeg

Install FLAC

    choco install flac

Install SoX

    choco install sox.portable