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


## Manually Build FLAC 


For Windows users replace the x86 binary parts from the current [release builds](https://github.com/xiph/flac/releases/tag/1.5.0).

For Ubuntu/Mint users this should get you sorted as the current packages use an older build.

For MacOS users it should be available on Brew soon.


### Installation and Build Steps

1. **Remove Existing FLAC Installation**:
   
```
sudo apt-get remove flac libflac-dev
```

2. **Clone the FLAC Repository**:

```
git clone https://github.com/xiph/flac.git
cd flac
git checkout tags/1.5.0 -b 1.5.0
```

3. **Install Required Dependencies**:

```
sudo apt-get install build-essential autoconf automake libtool pkg-config cmake curl pandoc graphviz
```

4. **Create a Build Directory**:

```
mkdir build
cd build
```

5. **Configure the Build with `cmake`**:
   
- If you want to include man pages:
```
cmake ..
```
   - If you want to build without man pages:
```
cmake -DINSTALL_MANPAGES=OFF ..
```

6. **Compile the Code**:

``` 
make
```

7. **Install FLAC**:

```
sudo make install
```