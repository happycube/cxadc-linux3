:: Create FLAC file from an Modified CX Card 20msps (10-bit) 16-bit sample capture .u16 file.
pushd %~dp0
echo Encoding Unsinged 16-bit to FLAC Compressed ... 
flac.exe -f "%~1" --best --sample-rate=20000 --sign=unsigned --channels=1 --endian=little --bps=16 "%~n1.flac"
echo Done. 
PAUSE